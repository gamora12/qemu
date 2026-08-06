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
 * MSHV models the GICv3 entirely inside the hypervisor and only exposes it
 * as an opaque, per-VP "local interrupt controller" state blob (the same
 * MSHV_VP_STATE_LAPIC type used for the x86 local APIC). The distributor,
 * redistributor and CPU interface state are all folded into this blob; there
 * is no per-register window like KVM provides. We therefore save/restore one
 * page-sized opaque blob per VP and migrate them verbatim.
 */
typedef struct MshvGICv3State {
    GICv3State parent_obj;
    uint32_t vp_state_size;
    uint8_t *vp_state;
} MshvGICv3State;

DECLARE_OBJ_CHECKERS(MshvGICv3State, MSHVARMGICv3Class,
                     MSHV_GICV3, TYPE_MSHV_GICV3)

static void mshv_gicv3_get(GICv3State *s)
{
    /*
     * The decoded distributor/redistributor/CPU-interface state is not
     * accessible under MSHV; the authoritative state travels as an opaque
     * per-VP blob migrated by the vmstate_gicv3_mshv subsection.
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
}

/*
 * Save/restore the opaque per-VP GICv3 state. MSHV exposes the whole GIC
 * (distributor + redistributor + CPU interface) as a page-sized opaque blob
 * per VP, retrieved/applied via MSHV_GET_VP_STATE/MSHV_SET_VP_STATE with the
 * MSHV_VP_STATE_LAPIC state type. The hypervisor requires the transfer buffer
 * to be page aligned, so we bounce each VP's blob through an aligned page.
 */
#define MSHV_GIC_VP_STATE_SIZE HV_HYP_PAGE_SIZE

static int mshv_gic_opaque_state_save(void *opaque)
{
    MshvGICv3State *mgs = opaque;
    GICv3State *s = ARM_GICV3_COMMON(opaque);
    size_t page = MSHV_GIC_VP_STATE_SIZE;
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
            qemu_vfree(bounce);
            g_free(mgs->vp_state);
            mgs->vp_state = NULL;
            mgs->vp_state_size = 0;
            return 1;
        }
        memcpy(mgs->vp_state + (size_t)i * page, bounce, page);
    }

    qemu_vfree(bounce);
    return 0;
}

static void mshv_gic_opaque_state_free(void *opaque)
{
    MshvGICv3State *mgs = opaque;

    g_free(mgs->vp_state);
    mgs->vp_state = NULL;
    mgs->vp_state_size = 0;
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
        return 1;
    }

    bounce = qemu_memalign(page, page);

    for (i = 0; i < s->num_cpu; i++) {
        CPUState *cpu = s->cpu[i].cpu;

        memcpy(bounce, mgs->vp_state + (size_t)i * page, page);
        if (mshv_set_vp_state(cpu, MSHV_VP_STATE_LAPIC, bounce, page) < 0) {
            error_report("mshv: vgic: failed to set GIC state for CPU %d", i);
            qemu_vfree(bounce);
            return 1;
        }
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
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = gicv3_is_mshv,
    .pre_save = mshv_gic_opaque_state_save,
    .post_save = mshv_gic_opaque_state_free,
    .post_load = mshv_gic_opaque_state_restore,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(vp_state_size, MshvGICv3State),
        VMSTATE_VBUFFER_ALLOC_UINT32(vp_state, MshvGICv3State, 0, 0,
                                     vp_state_size),
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

static void mshv_gicv3_register_types(void)
{
    type_register_static(&mshv_arm_gicv3_info);
}

type_init(mshv_gicv3_register_types)
