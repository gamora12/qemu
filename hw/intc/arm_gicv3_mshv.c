/*
 * ARM Generic Interrupt Controller using MSHV in-kernel support
 *
 * Copyright Microsoft, Corp. 2026
 * Based on vGICv3 KVM code by Pavel Fedin
 *
 * Authors:
 *      Aastha Rawat <aastharawat@microsoft.com>
 *      Anirudh Rayabharam (Microsoft) <anirudh@anirudhrb.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/memalign.h"
#include "hw/arm/bsa.h"
#include "hw/core/cpu.h"
#include "hw/intc/arm_gicv3_common.h"
#include "migration/blocker.h"
#include "migration/vmstate.h"
#include "target/arm/cpregs.h"
#include "hw/hyperv/hvgdk_mini.h"
#include "system/mshv.h"
#include "system/mshv_int.h"
#include "linux/mshv.h"

struct MSHVARMGICv3Class {
    ARMGICv3CommonClass parent_class;
    DeviceRealize parent_realize;
    ResettablePhases parent_phases;
};

typedef struct MSHVARMGICv3Class MSHVARMGICv3Class;

/*
 * MSHV models the GICv3 entirely inside the hypervisor and exposes it as two
 * opaque blobs rather than a per-register window like KVM provides.
 *
 * The per-VP blob (MSHV_VP_STATE_LAPIC, the architecture-neutral name for the
 * local interrupt controller state -- see the "either arch" comment on the
 * enum in linux/mshv.h) carries the CPU interface plus the banked interrupts,
 * which on GICv3 means only SGIs 0-15 and PPIs 16-31.
 *
 * The partition-wide blob (MSHV_VP_STATE_GIC_GLOBAL) carries the distributor
 * state: the GICD_CTLR group enable and one descriptor per shared peripheral
 * interrupt (SPI, INTID 32 and above). Every device interrupt in the machine
 * -- UART, RTC, virtio, PCIe -- is an SPI, so this blob must be migrated or
 * the destination boots with an empty distributor and silently drops every
 * device interrupt.
 *
 * The GIC state must not be migrated through GET/SET_VP_REGISTERS instead:
 * the ICC and ICH system registers describe only the CPU interface and give no
 * stable representation of the distributor/redistributor state or of the
 * hypervisor-internal interrupt delivery queues.
 *
 * Both blobs are only meaningful once every VP is suspended, which is the case
 * for a vmstate pre_save: migration completes the stop-the-guest handshake
 * before the non-iterative device state is written. Likewise post_load runs
 * before any destination VP is started.
 */
typedef struct MshvGICv3State {
    GICv3State parent_obj;
    uint32_t vp_state_size;
    uint8_t *vp_state;
    uint32_t global_state_size;
    uint8_t *global_state;
    Error *migration_blocker;
} MshvGICv3State;

DECLARE_OBJ_CHECKERS(MshvGICv3State, MSHVARMGICv3Class,
                     MSHV_GICV3, TYPE_MSHV_GICV3)

/* The hypervisor requires the per-VP state buffer to be page aligned. */
#define MSHV_GIC_VP_STATE_SIZE HV_HYP_PAGE_SIZE

typedef struct MshvGICStateHeader {
    uint8_t version;
    uint8_t gic_version;
} MshvGICStateHeader;

typedef struct MshvGICInterruptState {
    uint8_t flags;
    uint8_t configured_priority;
    uint8_t active_priority;
    uint8_t reserved;
} MshvGICInterruptState;

typedef struct MshvGICState {
    uint8_t version;
    uint8_t gic_version;
    uint8_t eoi_mode;
    uint8_t reserved[5];
    uint64_t igrpen1;
    uint64_t enable_lpis;
    uint64_t bpr1;
    uint64_t pmr;
    uint64_t propbaser;
    uint64_t pendbaser;
    uint32_t active_priorities[4];
    MshvGICInterruptState banked_interrupts[32];
} MshvGICState;

/* One descriptor per shared peripheral interrupt in the partition-wide blob. */
typedef struct MshvGICGlobalInterruptState {
    uint32_t interrupt_id;
    uint32_t active_vp_index;
    uint32_t target_vp;         /* MPIDR or VP index depending on routing */
    MshvGICInterruptState interrupt_state;
} MshvGICGlobalInterruptState;

typedef struct MshvGICGlobalState {
    uint8_t version;
    uint8_t gic_version;
    uint8_t reserved0[2];
    uint32_t num_interrupts;
    uint64_t gicd_ctlr_enable_grp1a;
    MshvGICGlobalInterruptState interrupts[];
} MshvGICGlobalState;

QEMU_BUILD_BUG_ON(sizeof(MshvGICGlobalInterruptState) != 16);
/* Flexible array member, so this is the header size: the array starts at 16. */
QEMU_BUILD_BUG_ON(sizeof(MshvGICGlobalState) != 16);

#define MSHV_GIC_INT_ENABLED      BIT(0)
#define MSHV_GIC_INT_ASSERTED     BIT(2)
#define MSHV_GIC_INT_SET_PENDING  BIT(3)
#define MSHV_GIC_INT_ACTIVE       BIT(4)

#define MSHV_GIC_STATE_MIN_VERSION 1
#define MSHV_GIC_VERSION_3         3

static bool mshv_gic_state_is_valid(const void *state)
{
    const MshvGICStateHeader *header = state;

    return header->version >= MSHV_GIC_STATE_MIN_VERSION &&
           header->gic_version == MSHV_GIC_VERSION_3;
}

/*
 * The partition-wide blob is variable sized and the hypervisor offers no way
 * to query the size up front: an undersized buffer is simply rejected.
 *
 * The hypervisor reports its own SPI capacity rather than the machine's, and
 * that is wider than QEMU's view -- a GICv3 partition reports 960 entries,
 * one for each of INTIDs 32..991 -- so sizing from num_irq alone is not
 * enough. Estimate from whichever is larger and keep a bounded doubling retry
 * as a safety net.
 */
#define MSHV_GIC_GLOBAL_STATE_MIN_INTS 1024
#define MSHV_GIC_GLOBAL_STATE_MAX_SIZE (16 * HV_HYP_PAGE_SIZE)

static size_t mshv_gic_global_state_size(const GICv3State *s)
{
    size_t ints = MAX(s->num_irq, MSHV_GIC_GLOBAL_STATE_MIN_INTS);
    size_t sz = sizeof(MshvGICGlobalState) +
                ints * sizeof(MshvGICGlobalInterruptState);

    return ROUND_UP(sz, HV_HYP_PAGE_SIZE);
}

static bool mshv_gic_global_state_is_valid(const void *state, size_t sz)
{
    const MshvGICGlobalState *global = state;
    size_t needed;

    if (global->version < MSHV_GIC_STATE_MIN_VERSION ||
        global->gic_version != MSHV_GIC_VERSION_3) {
        return false;
    }

    needed = sizeof(*global) + (size_t)global->num_interrupts *
                               sizeof(MshvGICGlobalInterruptState);

    return needed <= sz;
}

/*
 * Number of bytes the blob actually occupies, rounded up to the page
 * granularity the ioctl requires.
 *
 * Unlike the get, the set rejects a buffer that spans more pages than
 * num_interrupts accounts for: the hypervisor derives its repetition count
 * from the page count and has nothing to put in the surplus pages. The
 * estimate used for the get is deliberately generous, so the blob must be
 * trimmed to this size before it is handed back.
 */
static size_t mshv_gic_global_state_used_size(const void *state)
{
    const MshvGICGlobalState *global = state;
    size_t used = sizeof(*global) + (size_t)global->num_interrupts *
                                    sizeof(MshvGICGlobalInterruptState);

    return ROUND_UP(used, HV_HYP_PAGE_SIZE);
}

/*
 * Read the partition-wide GIC state into a freshly allocated page-aligned
 * buffer, growing the buffer if the hypervisor reports more interrupts than
 * the initial estimate can hold. On success *sz holds the buffer size that
 * was accepted; the caller owns the buffer and frees it with qemu_vfree().
 */
static void *mshv_gic_global_state_read(CPUState *cpu, size_t *sz)
{
    size_t attempt;

    for (attempt = *sz; attempt <= MSHV_GIC_GLOBAL_STATE_MAX_SIZE;
         attempt *= 2) {
        void *buf = qemu_memalign(HV_HYP_PAGE_SIZE, attempt);

        memset(buf, 0, attempt);
        if (mshv_get_vp_state(cpu, MSHV_VP_STATE_GIC_GLOBAL, buf,
                              attempt) == 0) {
            *sz = attempt;
            return buf;
        }

        qemu_vfree(buf);
    }

    return NULL;
}

static void mshv_gic_log_timer_ppi(const char *side, int cpu,
                                   const void *state)
{
    const MshvGICState *gic = state;
    const MshvGICInterruptState *ppi =
        &gic->banked_interrupts[ARCH_TIMER_VIRT_IRQ];

    info_report("MSHV ARM GIC %s CPU %d: PPI %d flags=0x%02x "
                "enabled=%d asserted=%d pending=%d active=%d "
                "configured_priority=0x%02x active_priority=0x%02x",
                side, cpu, ARCH_TIMER_VIRT_IRQ, ppi->flags,
                !!(ppi->flags & MSHV_GIC_INT_ENABLED),
                !!(ppi->flags & MSHV_GIC_INT_ASSERTED),
                !!(ppi->flags & MSHV_GIC_INT_SET_PENDING),
                !!(ppi->flags & MSHV_GIC_INT_ACTIVE),
                ppi->configured_priority, ppi->active_priority);
}

/*
 * Re-arm the virtual timer PPI on a migrated vCPU.
 *
 * Hyper-V drops the virtual timer registers written through SET_VP_REGISTERS on
 * a stopped VP: the setters update the hardware timer but never the VP's
 * inactive context, and the partial context save on the way out of precise work
 * deliberately skips the timer. The restored deadline is therefore lost and
 * PPI 27 is never re-asserted, so a vCPU that migrated while idle parks in WFI
 * with nothing left to wake it.
 *
 * The GIC state path does not share that defect. Unlike
 * HVCALL_ASSERT_VIRTUAL_INTERRUPT, which the hypervisor ignores for the timer
 * because it is a physically backed "implemented" PPI, the state restore applies
 * Asserted/SetPending to any PPI and then queues real delivery. Re-arming the
 * PPI here therefore makes the destination take one timer interrupt on resume.
 *
 * That is enough to heal the timer permanently: the interrupt ends the WFI, and
 * Linux reprograms CNTV_CVAL/CNTV_CTL with ordinary guest register writes, which
 * Hyper-V handles correctly. This holds even if the guest treats the interrupt
 * as spurious because the deadline had not yet passed -- an idle NO_HZ guest
 * migrates with its next tick seconds away -- since leaving the idle loop is
 * itself what reprograms the tick.
 */
static void mshv_gicv3_get(GICv3State *s){
    /*
     * QEMU keeps no decoded shadow of the GICv3 register file under MSHV: the
     * authoritative state lives in the hypervisor and travels as the opaque
     * per-VP and partition-wide blobs migrated by vmstate_gicv3_mshv.
     */
}

static void mshv_gicv3_put(GICv3State *s)
{
}

static void mshv_gicv3_reset_hold(Object *obj, ResetType type)
{
    GICv3State *s = ARM_GICV3_COMMON(obj);
    MSHVARMGICv3Class *mgc = MSHV_GICV3_GET_CLASS(s);

    if (mgc->parent_phases.hold) {
        mgc->parent_phases.hold(obj, type);
    }

    mshv_gicv3_put(s);
}

static void mshv_gicv3_set_irq(void *opaque, int irq, int level)
{
    int ret;
    GICv3State *s = (GICv3State *)opaque;
    int vm_fd = mshv_state->vm;
    struct hv_input_assert_virtual_interrupt arg = {0};
    struct mshv_root_hvcall args = {0};
    union hv_interrupt_control control = {
        .interrupt_type = HV_ARM64_INTERRUPT_TYPE_FIXED,
        .rsvd1 = 0,
        .asserted = level,
        .rsvd2 = 0
    };

    if (irq >= s->num_irq) {
        return;
    }

    arg.control = control;
    arg.vector = GIC_INTERNAL + irq;

    args.code   = HVCALL_ASSERT_VIRTUAL_INTERRUPT;
    args.in_sz  = sizeof(arg);
    args.in_ptr = (uint64_t)&arg;

    ret = mshv_hvcall(vm_fd, &args);
    if (ret < 0) {
        error_report("Failed to set GICv3 IRQ %d to level %d", irq, level);
    }
}

static void mshv_gicv3_realize(DeviceState *dev, Error **errp)
{
    ERRP_GUARD();
    GICv3State *s = ARM_GICV3_COMMON(dev);
    MSHVARMGICv3Class *mgc = MSHV_GICV3_GET_CLASS(s);
    MshvGICv3State *mgs = MSHV_GICV3(dev);
    void *probe;
    int i, ret;

    mgc->parent_realize(dev, errp);
    if (*errp) {
        return;
    }

    if (s->revision != 3) {
        error_setg(errp, "unsupported GIC revision %d for platform GIC",
                   s->revision);
        return;
    }

    if (s->security_extn) {
        error_setg(errp, "the platform vGICv3 does not implement the "
                   "security extensions");
        return;
    }

    if (s->nmi_support) {
        error_setg(errp, "NMI is not supported with the platform GIC");
        return;
    }

    if (s->nb_redist_regions > 1) {
        error_setg(errp, "Multiple VGICv3 redistributor regions are not "
                   "supported by MSHV");
        error_append_hint(errp, "A maximum of %d VCPUs can be used",
                          s->redist_region_count[0]);
        return;
    }

    gicv3_init_irqs_and_mmio(s, mshv_gicv3_set_irq, NULL);

    for (i = 0; i < s->num_cpu; i++) {
        CPUState *cpu_state = qemu_get_cpu(i);

        hv_register_assoc gicr_base = {
            .name = HV_ARM64_REGISTER_GICR_BASE_GPA,
            .value = {
                .reg64 = 0x080A0000 + (GICV3_REDIST_SIZE * i)
            }
        };

        ret = mshv_set_generic_regs(cpu_state, &gicr_base, 1);
        if (ret < 0) {
            error_setg(errp, "Failed to set GICR base for CPU %d", i);
            return;
        }
    }

    if (s->maint_irq) {
        error_setg(errp,
               "Nested virtualisation not currently supported by MSHV");
        return;
    }

    /*
     * Probe the per-VP state interface up front. The GIC state is only
     * reachable through MSHV_[GET,SET]_VP_STATE, and some hosts do not
     * implement it (the ioctls are compiled out of the mshv driver behind
     * HV_SUPPORTS_VP_STATE, in which case they fail with ENOTTY). Without it
     * the controller state cannot be transferred, so block migration here
     * rather than letting it fail -- or, worse, appear to succeed -- once the
     * guest has already been stopped.
     */
    probe = qemu_memalign(MSHV_GIC_VP_STATE_SIZE, MSHV_GIC_VP_STATE_SIZE);
    memset(probe, 0, MSHV_GIC_VP_STATE_SIZE);
    ret = mshv_get_vp_state(qemu_get_cpu(0), MSHV_VP_STATE_LAPIC, probe,
                            MSHV_GIC_VP_STATE_SIZE);

    if (ret < 0) {
        error_setg(&mgs->migration_blocker,
                   "This host cannot save the MSHV GICv3 state: the "
                   "MSHV_GET_VP_STATE ioctl is unavailable, so the interrupt "
                   "controller state cannot be migrated");
    } else if (!mshv_gic_state_is_valid(probe)) {
        const MshvGICStateHeader *header = probe;

        error_setg(&mgs->migration_blocker,
                   "This host returned an invalid MSHV GICv3 state: "
                   "(version %u, gic_version %u)",
                   header->version, header->gic_version);
    }

    qemu_vfree(probe);

    if (!mgs->migration_blocker) {
        size_t global_sz = mshv_gic_global_state_size(s);

        probe = mshv_gic_global_state_read(qemu_get_cpu(0), &global_sz);
        if (!probe) {
            error_setg(&mgs->migration_blocker,
                       "This host cannot save the partition-wide MSHV GICv3 "
                       "state, so the distributor and SPI state cannot be "
                       "migrated");
        } else {
            if (!mshv_gic_global_state_is_valid(probe, global_sz)) {
                const MshvGICGlobalState *header = probe;

                error_setg(&mgs->migration_blocker,
                           "This host returned an invalid partition-wide MSHV "
                           "GICv3 state: (version %u, gic_version %u, "
                           "num_interrupts %u)",
                           header->version, header->gic_version,
                           header->num_interrupts);
            }
            qemu_vfree(probe);
        }
    }

    if (mgs->migration_blocker) {
        if (migrate_add_blocker(&mgs->migration_blocker, errp) < 0) {
            return;
        }
        warn_report("mshv: vgic: invalid or unavailable GIC state; "
                    "migration disabled");
    }
}

/*
 * Save/restore the opaque GICv3 state. MSHV exposes the GIC as two blobs:
 * a page-sized per-VP blob holding the CPU interface and the banked SGIs and
 * PPIs (MSHV_VP_STATE_LAPIC), and a variable-sized partition-wide blob holding
 * the distributor and every SPI (MSHV_VP_STATE_GIC_GLOBAL). Both are
 * retrieved/applied via MSHV_GET_VP_STATE/MSHV_SET_VP_STATE. The hypervisor
 * requires the transfer buffer to be page aligned, so we bounce each blob
 * through an aligned allocation.
 */

static int mshv_gic_opaque_state_save(void *opaque)
{
    MshvGICv3State *mgs = opaque;
    GICv3State *s = ARM_GICV3_COMMON(opaque);
    size_t page = MSHV_GIC_VP_STATE_SIZE;
    size_t global_sz = mshv_gic_global_state_size(s);
    void *global;
    void *bounce;
    int i;

    mgs->vp_state_size = s->num_cpu * page;
    mgs->vp_state = g_malloc0(mgs->vp_state_size);
    bounce = qemu_memalign(page, page);

    for (i = 0; i < s->num_cpu; i++) {
        CPUState *cpu = s->cpu[i].cpu;

        memset(bounce, 0, page);
        if (mshv_get_vp_state(cpu, MSHV_VP_STATE_LAPIC, bounce, page) < 0) {
            error_report("mshv: vgic: failed to get GIC state for CPU %d", i);
            goto fail;
        }

        if (!mshv_gic_state_is_valid(bounce)) {
            const MshvGICStateHeader *header = bounce;

            error_report("mshv: vgic: invalid GIC state for CPU %d: "
                         "(version %u, gic_version %u)",
                         i, header->version, header->gic_version);
            goto fail;
        }

        mshv_gic_log_timer_ppi("source", i, bounce);
        memcpy(mgs->vp_state + (size_t)i * page, bounce, page);
    }

    qemu_vfree(bounce);

    global = mshv_gic_global_state_read(s->cpu[0].cpu, &global_sz);
    if (!global) {
        error_report("mshv: vgic: failed to get partition-wide GIC state");
        g_free(mgs->vp_state);
        mgs->vp_state = NULL;
        mgs->vp_state_size = 0;
        return -1;
    }

    if (!mshv_gic_global_state_is_valid(global, global_sz)) {
        const MshvGICGlobalState *header = global;

        error_report("mshv: vgic: invalid partition-wide GIC state: "
                     "(version %u, gic_version %u, num_interrupts %u)",
                     header->version, header->gic_version,
                     header->num_interrupts);
        qemu_vfree(global);
        g_free(mgs->vp_state);
        mgs->vp_state = NULL;
        mgs->vp_state_size = 0;
        return -1;
    }

    mgs->global_state_size = mshv_gic_global_state_used_size(global);
    mgs->global_state = g_memdup2(global, mgs->global_state_size);

    {
        const MshvGICGlobalState *header = global;
        uint32_t enabled = 0, asserted = 0, n;
        uint32_t min_id = UINT32_MAX, max_id = 0;
        bool contiguous = true;

        for (n = 0; n < header->num_interrupts; n++) {
            uint8_t flags = header->interrupts[n].interrupt_state.flags;
            uint32_t id = header->interrupts[n].interrupt_id;

            if (flags & MSHV_GIC_INT_ENABLED) {
                enabled++;
            }
            if (flags & MSHV_GIC_INT_ASSERTED) {
                asserted++;
            }
            min_id = MIN(min_id, id);
            max_id = MAX(max_id, id);
            if (n > 0 &&
                id != header->interrupts[n - 1].interrupt_id + 1) {
                contiguous = false;
            }
        }

        info_report("MSHV ARM GIC global source: version %u gic_version %u "
                    "num_interrupts %u intid %u..%u%s enabled %u asserted %u "
                    "gicd_ctlr_grp1a 0x%" PRIx64 " (read %zu, kept %u bytes)",
                    header->version, header->gic_version,
                    header->num_interrupts, min_id, max_id,
                    contiguous ? " contiguous" : " NON-CONTIGUOUS",
                    enabled, asserted,
                    header->gicd_ctlr_enable_grp1a, global_sz,
                    mgs->global_state_size);
    }

    qemu_vfree(global);

    return 0;

fail:
    qemu_vfree(bounce);
    g_free(mgs->vp_state);
    mgs->vp_state = NULL;
    mgs->vp_state_size = 0;
    return -1;
}

static void mshv_gic_opaque_state_free(void *opaque)
{
    MshvGICv3State *mgs = opaque;

    g_free(mgs->vp_state);
    mgs->vp_state = NULL;
    mgs->vp_state_size = 0;
    g_free(mgs->global_state);
    mgs->global_state = NULL;
    mgs->global_state_size = 0;
}

static int mshv_gic_opaque_state_restore(void *opaque, int version_id)
{
    MshvGICv3State *mgs = opaque;
    GICv3State *s = ARM_GICV3_COMMON(opaque);
    size_t page = MSHV_GIC_VP_STATE_SIZE;
    void *bounce;
    int i;

    if (!mgs->vp_state_size) {
        return 0;
    }

    if (mgs->vp_state_size != (uint32_t)(s->num_cpu * page)) {
        error_report("mshv: vgic: unexpected GIC state size %u (want %zu)",
                     mgs->vp_state_size, s->num_cpu * page);
        return -1;
    }

    /*
     * Restore the distributor before the per-VP state: the SPI descriptors
     * decide which interrupts are enabled and where they are routed, and the
     * hypervisor validates per-VP pending/active entries against them.
     */
    if (!mgs->global_state_size) {
        error_report("mshv: vgic: migration stream carries no partition-wide "
                     "GIC state; the destination would drop every device "
                     "interrupt");
        return -1;
    }

    if (!QEMU_IS_ALIGNED(mgs->global_state_size, page) ||
        !mshv_gic_global_state_is_valid(mgs->global_state,
                                        mgs->global_state_size) ||
        mshv_gic_global_state_used_size(mgs->global_state) !=
            mgs->global_state_size) {
        error_report("mshv: vgic: invalid partition-wide GIC state in "
                     "migration stream (size %u)", mgs->global_state_size);
        return -1;
    }

    bounce = qemu_memalign(page, mgs->global_state_size);
    memcpy(bounce, mgs->global_state, mgs->global_state_size);
    if (mshv_set_vp_state(s->cpu[0].cpu, MSHV_VP_STATE_GIC_GLOBAL, bounce,
                          mgs->global_state_size) < 0) {
        error_report("mshv: vgic: failed to set partition-wide GIC state");
        qemu_vfree(bounce);
        return -1;
    }
    qemu_vfree(bounce);

    bounce = qemu_memalign(page, page);

    for (i = 0; i < s->num_cpu; i++) {
        CPUState *cpu = s->cpu[i].cpu;

        memcpy(bounce, mgs->vp_state + (size_t)i * page, page);
        if (mshv_set_vp_state(cpu, MSHV_VP_STATE_LAPIC, bounce, page) < 0) {
            error_report("mshv: vgic: failed to set GIC state for CPU %d", i);
            qemu_vfree(bounce);
            return -1;
        }

        memset(bounce, 0, page);
        if (mshv_get_vp_state(cpu, MSHV_VP_STATE_LAPIC, bounce, page) < 0) {
            error_report("mshv: vgic: failed to read back GIC state for "
                         "CPU %d", i);
            qemu_vfree(bounce);
            return -1;
        }
        mshv_gic_log_timer_ppi("destination readback", i, bounce);
    }

    qemu_vfree(bounce);
    return 0;
}

static bool gicv3_is_mshv(void *opaque)
{
    return mshv_enabled();
}

const VMStateDescription vmstate_gicv3_mshv = {
    .name = "arm_gicv3/mshv_gic_state",
    .version_id = 2,
    .minimum_version_id = 2,
    .needed = gicv3_is_mshv,
    .pre_save = mshv_gic_opaque_state_save,
    .post_save = mshv_gic_opaque_state_free,
    .post_load = mshv_gic_opaque_state_restore,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(vp_state_size, MshvGICv3State),
        VMSTATE_VBUFFER_ALLOC_UINT32(vp_state, MshvGICv3State, 0, 0,
                                     vp_state_size),
        VMSTATE_UINT32(global_state_size, MshvGICv3State),
        VMSTATE_VBUFFER_ALLOC_UINT32(global_state, MshvGICv3State, 0, 0,
                                     global_state_size),
        VMSTATE_END_OF_LIST()
    },
};

static void mshv_gicv3_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);
    ARMGICv3CommonClass *agcc = ARM_GICV3_COMMON_CLASS(klass);
    MSHVARMGICv3Class *mgc = MSHV_GICV3_CLASS(klass);

    agcc->pre_save = mshv_gicv3_get;
    agcc->post_load = mshv_gicv3_put;

    device_class_set_parent_realize(dc, mshv_gicv3_realize,
                                    &mgc->parent_realize);
    resettable_class_set_parent_phases(rc, NULL, mshv_gicv3_reset_hold, NULL,
                                       &mgc->parent_phases);
}

static const TypeInfo mshv_arm_gicv3_info = {
    .name = TYPE_MSHV_GICV3,
    .parent = TYPE_ARM_GICV3_COMMON,
    .instance_size = sizeof(MshvGICv3State),
    .class_init = mshv_gicv3_class_init,
    .class_size = sizeof(MSHVARMGICv3Class),
};

/*
 * Re-arm the virtual timer PPI on a migrated vCPU.
 *
 * Hyper-V drops the virtual timer registers written through SET_VP_REGISTERS on
 * a stopped VP: the setters update the hardware timer but never the VP's
 * inactive context, and the partial context save on the way out of precise work
 * deliberately skips the timer. The restored deadline is therefore lost and
 * PPI 27 is never re-asserted, so a vCPU that migrated while idle parks in WFI
 * with nothing left to wake it.
 *
 * The GIC state path does not share that defect. Unlike
 * HVCALL_ASSERT_VIRTUAL_INTERRUPT, which the hypervisor ignores for the timer
 * because it is a physically backed "implemented" PPI, the state restore applies
 * Asserted/SetPending to any PPI and then queues real delivery. Re-arming the
 * PPI here therefore makes the destination take one timer interrupt on resume.
 *
 * That is enough to heal the timer permanently: the interrupt ends the WFI, and
 * Linux reprograms CNTV_CVAL/CNTV_CTL with ordinary guest register writes, which
 * Hyper-V handles correctly. This holds even if the guest treats the interrupt
 * as spurious because the deadline had not yet passed -- an idle NO_HZ guest
 * migrates with its next tick seconds away -- since leaving the idle loop is
 * itself what reprograms the tick.
 *
 * This runs from the timer restore path rather than from post_load because the
 * GIC vmstate section is loaded before the per-vCPU timer section, so the
 * migrated timer values are not yet known while the GIC blob is being restored.
 */
/*
 * Read-only companion to mshv_gic_rearm_timer_ppi(). Samples PPI 27 without
 * modifying it, so a probe can separate "the hypervisor never asserts the
 * timer PPI" from "it asserts it but the guest never takes it".
 */
void mshv_gic_dump_timer_ppi(const CPUState *cpu, const char *side)
{
    size_t page = MSHV_GIC_VP_STATE_SIZE;
    void *bounce = qemu_memalign(page, page);

    memset(bounce, 0, page);

    if (mshv_get_vp_state(cpu, MSHV_VP_STATE_LAPIC, bounce, page) == 0) {
        mshv_gic_log_timer_ppi(side, cpu->cpu_index, bounce);
    } else {
        error_report("mshv: vgic: CPU %d: failed to read GIC state for %s",
                     cpu->cpu_index, side);
    }

    qemu_vfree(bounce);
}

int mshv_gic_rearm_timer_ppi(const CPUState *cpu)
{
    size_t page = MSHV_GIC_VP_STATE_SIZE;
    const char *mode = getenv("MSHV_REARM_MODE");
    MshvGICState *gic;
    void *bounce;
    int ret = 0;
    int i;

    /*
     * Which banked interrupts to re-arm. PPI 27 is the virtual timer itself.
     * SGI 0 is Linux's IPI_RESCHEDULE: it has no physical backing, so unlike
     * the timer PPI the hypervisor cannot re-derive its line state from
     * hardware, and a spurious reschedule IPI is harmless -- it simply pulls
     * the vCPU out of WFI and runs the scheduler, which reprograms the tick.
     */
    int targets[2];
    int n_targets = 0;

    if (!mode) {
        mode = "both";
    }

    if (g_str_equal(mode, "none")) {
        return 0;
    }
    if (g_str_equal(mode, "ppi") || g_str_equal(mode, "both")) {
        targets[n_targets++] = ARCH_TIMER_VIRT_IRQ;
    }
    if (g_str_equal(mode, "sgi") || g_str_equal(mode, "both")) {
        targets[n_targets++] = 0;
    }

    bounce = qemu_memalign(page, page);
    memset(bounce, 0, page);

    if (mshv_get_vp_state(cpu, MSHV_VP_STATE_LAPIC, bounce, page) < 0) {
        error_report("mshv: vgic: CPU %d: failed to read GIC state to re-arm "
                     "the virtual timer PPI", cpu->cpu_index);
        qemu_vfree(bounce);
        return -1;
    }

    gic = bounce;

    for (i = 0; i < n_targets; i++) {
        MshvGICInterruptState *irq = &gic->banked_interrupts[targets[i]];

        /*
         * The hypervisor only queues delivery for an enabled interrupt, and an
         * already-active one would make the whole restore fail with
         * HV_STATUS_INVALID_VP_STATE.
         */
        if (irq->flags & MSHV_GIC_INT_ACTIVE) {
            info_report("MSHV ARM GIC destination CPU %d: INTID %d already "
                        "active, not re-arming", cpu->cpu_index, targets[i]);
            continue;
        }

        /*
         * Asserted matches the level-triggered nature of the timer output,
         * while SetPending is the software pending bit the hypervisor stores
         * verbatim for PPIs. Either one alone satisfies the delivery
         * condition; both are set so that delivery does not depend on which of
         * the two the hypervisor recomputes from hardware.
         */
        irq->flags |= MSHV_GIC_INT_ENABLED | MSHV_GIC_INT_ASSERTED |
                      MSHV_GIC_INT_SET_PENDING;

        info_report("MSHV ARM GIC destination CPU %d: re-arming INTID %d "
                    "(flags now 0x%02x)",
                    cpu->cpu_index, targets[i], irq->flags);
    }

    if (mshv_set_vp_state(cpu, MSHV_VP_STATE_LAPIC, bounce, page) < 0) {
        error_report("mshv: vgic: CPU %d: failed to re-arm the virtual timer "
                     "PPI", cpu->cpu_index);
        ret = -1;
    } else {
        memset(bounce, 0, page);
        if (mshv_get_vp_state(cpu, MSHV_VP_STATE_LAPIC, bounce, page) == 0) {
            for (i = 0; i < n_targets; i++) {
                const MshvGICInterruptState *irq =
                    &gic->banked_interrupts[targets[i]];

                info_report("MSHV ARM GIC post-rearm readback CPU %d: "
                            "INTID %d flags=0x%02x enabled=%d asserted=%d "
                            "pending=%d active=%d",
                            cpu->cpu_index, targets[i], irq->flags,
                            !!(irq->flags & MSHV_GIC_INT_ENABLED),
                            !!(irq->flags & MSHV_GIC_INT_ASSERTED),
                            !!(irq->flags & MSHV_GIC_INT_SET_PENDING),
                            !!(irq->flags & MSHV_GIC_INT_ACTIVE));
            }
        }
    }

    qemu_vfree(bounce);
    return ret;
}

static void mshv_gicv3_register_types(void)
{
    type_register_static(&mshv_arm_gicv3_info);
}

type_init(mshv_gicv3_register_types)
