/*
 * QEMU MSHV support
 *
 * Copyright Microsoft, Corp. 2026
 *
 * Authors: Aastha Rawat          <aastharawat@linux.microsoft.com>
 *          Anirudh Rayabharam    <anirudh@anirudhrb.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include <sys/ioctl.h>

#include "qemu/error-report.h"
#include "qemu/memalign.h"
#include "hw/arm/virt.h"
#include "migration/vmstate.h"

#include "system/cpus.h"
#include "target/arm/cpu.h"
#include "target/arm/internals.h"
#include "target/arm/mshv_arm.h"
#include "target/arm/helper.h"

#include "system/mshv.h"
#include "system/mshv_int.h"
#include "hw/hyperv/hvgdk_mini.h"
#include "hw/hyperv/hvhdk_mini.h"

typedef struct ARMHostCPUFeatures {
    ARMISARegisters isar;
    uint64_t features;
    uint64_t midr;
    uint32_t reset_sctlr;
    const char *dtb_compatible;
} ARMHostCPUFeatures;

static ARMHostCPUFeatures arm_host_cpu_features;

/*
 * Simple 64-bit registers that map directly to a CPUARMState field.
 * Modelled after target/arm/hvf/hvf.c's hvf_reg_match[].
 */
typedef struct MshvRegMatch {
    uint32_t hv_reg;
    size_t offset;
} MshvRegMatch;

static const MshvRegMatch mshv_reg_match[] = {
    { HV_ARM64_REGISTER_X0,  offsetof(CPUARMState, xregs[0]) },
    { HV_ARM64_REGISTER_X1,  offsetof(CPUARMState, xregs[1]) },
    { HV_ARM64_REGISTER_X2,  offsetof(CPUARMState, xregs[2]) },
    { HV_ARM64_REGISTER_X3,  offsetof(CPUARMState, xregs[3]) },
    { HV_ARM64_REGISTER_X4,  offsetof(CPUARMState, xregs[4]) },
    { HV_ARM64_REGISTER_X5,  offsetof(CPUARMState, xregs[5]) },
    { HV_ARM64_REGISTER_X6,  offsetof(CPUARMState, xregs[6]) },
    { HV_ARM64_REGISTER_X7,  offsetof(CPUARMState, xregs[7]) },
    { HV_ARM64_REGISTER_X8,  offsetof(CPUARMState, xregs[8]) },
    { HV_ARM64_REGISTER_X9,  offsetof(CPUARMState, xregs[9]) },
    { HV_ARM64_REGISTER_X10, offsetof(CPUARMState, xregs[10]) },
    { HV_ARM64_REGISTER_X11, offsetof(CPUARMState, xregs[11]) },
    { HV_ARM64_REGISTER_X12, offsetof(CPUARMState, xregs[12]) },
    { HV_ARM64_REGISTER_X13, offsetof(CPUARMState, xregs[13]) },
    { HV_ARM64_REGISTER_X14, offsetof(CPUARMState, xregs[14]) },
    { HV_ARM64_REGISTER_X15, offsetof(CPUARMState, xregs[15]) },
    { HV_ARM64_REGISTER_X16, offsetof(CPUARMState, xregs[16]) },
    { HV_ARM64_REGISTER_X17, offsetof(CPUARMState, xregs[17]) },
    { HV_ARM64_REGISTER_X18, offsetof(CPUARMState, xregs[18]) },
    { HV_ARM64_REGISTER_X19, offsetof(CPUARMState, xregs[19]) },
    { HV_ARM64_REGISTER_X20, offsetof(CPUARMState, xregs[20]) },
    { HV_ARM64_REGISTER_X21, offsetof(CPUARMState, xregs[21]) },
    { HV_ARM64_REGISTER_X22, offsetof(CPUARMState, xregs[22]) },
    { HV_ARM64_REGISTER_X23, offsetof(CPUARMState, xregs[23]) },
    { HV_ARM64_REGISTER_X24, offsetof(CPUARMState, xregs[24]) },
    { HV_ARM64_REGISTER_X25, offsetof(CPUARMState, xregs[25]) },
    { HV_ARM64_REGISTER_X26, offsetof(CPUARMState, xregs[26]) },
    { HV_ARM64_REGISTER_X27, offsetof(CPUARMState, xregs[27]) },
    { HV_ARM64_REGISTER_X28, offsetof(CPUARMState, xregs[28]) },
    { HV_ARM64_REGISTER_FP,  offsetof(CPUARMState, xregs[29]) },
    { HV_ARM64_REGISTER_LR,  offsetof(CPUARMState, xregs[30]) },
    { HV_ARM64_REGISTER_PC,  offsetof(CPUARMState, pc) },
    /*
     * QEMU keeps the current SP in xregs[31] as well; this is kept in sync
     * with sp_el[] via aarch64_save_sp()/aarch64_restore_sp() below.
     */
    { HV_ARM64_REGISTER_SP_EL0,  offsetof(CPUARMState, sp_el[0]) },
    { HV_ARM64_REGISTER_SP_EL1,  offsetof(CPUARMState, sp_el[1]) },
    { HV_ARM64_REGISTER_ELR_EL1, offsetof(CPUARMState, elr_el[1]) },
};

/*
 * EL0/EL1 system (control) registers that map directly to a CPUARMState
 * cp15 field. These are the mshv analog of the registers KVM migrates
 * opaquely via its KVM_GET_REG_LIST cpreg list.
 *
 * AFSR0_EL1, AFSR1_EL1 and AMAIR_EL1 are intentionally omitted: QEMU models
 * them as RAZ/WI and keeps no backing state for them.
 *
 * CNTVOFF_EL2 is intentionally omitted: MSHV does not implement it as a VP
 * register at all. Both the read and the write paths fall through to
 * "unhandled timer register" and return HV_STATUS_MSR_ACCESS_FAILED, so it can
 * be neither sampled nor restored. The guest virtual counter is instead carried
 * across migration by writing CNTVCT_EL0, which MSHV maps onto the VP's
 * internal TSC and explicitly allows the parent of an exo partition to restore;
 * see store_timer_regs(). KVM likewise does not sync CNTVOFF_EL2 as a per-vCPU
 * register, and uses the equivalent KVM_REG_ARM_TIMER_CNT.
 */
static const MshvRegMatch mshv_sysreg_match[] = {
    { HV_ARM64_REGISTER_SCTLR_EL1,      offsetof(CPUARMState, cp15.sctlr_el[1]) },
    { HV_ARM64_REGISTER_CPACR_EL1,      offsetof(CPUARMState, cp15.cpacr_el1) },
    { HV_ARM64_REGISTER_TTBR0_EL1,      offsetof(CPUARMState, cp15.ttbr0_el[1]) },
    { HV_ARM64_REGISTER_TTBR1_EL1,      offsetof(CPUARMState, cp15.ttbr1_el[1]) },
    { HV_ARM64_REGISTER_TCR_EL1,        offsetof(CPUARMState, cp15.tcr_el[1]) },
    { HV_ARM64_REGISTER_ESR_EL1,        offsetof(CPUARMState, cp15.esr_el[1]) },
    { HV_ARM64_REGISTER_FAR_EL1,        offsetof(CPUARMState, cp15.far_el[1]) },
    { HV_ARM64_REGISTER_PAR_EL1,        offsetof(CPUARMState, cp15.par_el[1]) },
    { HV_ARM64_REGISTER_MAIR_EL1,       offsetof(CPUARMState, cp15.mair_el[1]) },
    { HV_ARM64_REGISTER_VBAR_EL1,       offsetof(CPUARMState, cp15.vbar_el[1]) },
    { HV_ARM64_REGISTER_CONTEXTIDR_EL1, offsetof(CPUARMState, cp15.contextidr_el[1]) },
    { HV_ARM64_REGISTER_TPIDR_EL1,      offsetof(CPUARMState, cp15.tpidr_el[1]) },
    { HV_ARM64_REGISTER_TPIDR_EL0,      offsetof(CPUARMState, cp15.tpidr_el[0]) },
    { HV_ARM64_REGISTER_TPIDRRO_EL0,    offsetof(CPUARMState, cp15.tpidrro_el[0]) },
    { HV_ARM64_REGISTER_CSSELR_EL1,     offsetof(CPUARMState, cp15.csselr_el[1]) },
    { HV_ARM64_REGISTER_MDSCR_EL1,      offsetof(CPUARMState, cp15.mdscr_el1) },
    { HV_ARM64_REGISTER_CNTKCTL_EL1,    offsetof(CPUARMState, cp15.c14_cntkctl) },
    /*
     * The EL1 virtual timer (CNTV_CTL_EL0 / CNTV_CVAL_EL0) is owned and driven
     * by MSHV internally: the hypervisor programs the timer and injects the
     * virtual timer PPI (INTID 27) itself. QEMU never observes the guest's
     * direct writes to these registers (they are not trapped), so env holds a
     * stale copy. The post-init and post-reset paths call store_regs() with no
     * preceding load_regs(), which would push those stale/reset values back
     * into MSHV and reprogram the live timer -- causing a storm of spurious,
     * mis-contexted INTID 27 interrupts in the guest. Like CNTVOFF_EL2, these
     * must not be part of the routine per-vCPU register sync.
     */
};

/*
 * Pointer authentication keys.
 *
 * The guest writes these via MSR and the accesses are not trapped, so env
 * never observes them: the live values exist only in the VP. They must still
 * be transferred on migration, because the guest's in-memory signed pointers
 * were signed with the source VP's keys -- if the destination VP keeps its
 * own keys, the first AUTIASP fails and the guest takes an FPAC exception.
 *
 * These are kept out of mshv_sysreg_match[] because they only exist when the
 * CPU implements FEAT_PAuth; they are synced separately after that check.
 */
static const MshvRegMatch mshv_pauth_reg_match[] = {
    { HV_ARM64_REGISTER_API_A_KEY_LO_EL1, offsetof(CPUARMState, keys.apia.lo) },
    { HV_ARM64_REGISTER_API_A_KEY_HI_EL1, offsetof(CPUARMState, keys.apia.hi) },
    { HV_ARM64_REGISTER_API_B_KEY_LO_EL1, offsetof(CPUARMState, keys.apib.lo) },
    { HV_ARM64_REGISTER_API_B_KEY_HI_EL1, offsetof(CPUARMState, keys.apib.hi) },
    { HV_ARM64_REGISTER_APD_A_KEY_LO_EL1, offsetof(CPUARMState, keys.apda.lo) },
    { HV_ARM64_REGISTER_APD_A_KEY_HI_EL1, offsetof(CPUARMState, keys.apda.hi) },
    { HV_ARM64_REGISTER_APD_B_KEY_LO_EL1, offsetof(CPUARMState, keys.apdb.lo) },
    { HV_ARM64_REGISTER_APD_B_KEY_HI_EL1, offsetof(CPUARMState, keys.apdb.hi) },
    { HV_ARM64_REGISTER_APG_A_KEY_LO_EL1, offsetof(CPUARMState, keys.apga.lo) },
    { HV_ARM64_REGISTER_APG_A_KEY_HI_EL1, offsetof(CPUARMState, keys.apga.hi) },
};

/* SIMD/FP registers Q0..Q31 map to the low 128 bits of vfp.zregs[i]. */
static const uint32_t mshv_fpreg_names[32] = {
    HV_ARM64_REGISTER_Q0,  HV_ARM64_REGISTER_Q1,  HV_ARM64_REGISTER_Q2,
    HV_ARM64_REGISTER_Q3,  HV_ARM64_REGISTER_Q4,  HV_ARM64_REGISTER_Q5,
    HV_ARM64_REGISTER_Q6,  HV_ARM64_REGISTER_Q7,  HV_ARM64_REGISTER_Q8,
    HV_ARM64_REGISTER_Q9,  HV_ARM64_REGISTER_Q10, HV_ARM64_REGISTER_Q11,
    HV_ARM64_REGISTER_Q12, HV_ARM64_REGISTER_Q13, HV_ARM64_REGISTER_Q14,
    HV_ARM64_REGISTER_Q15, HV_ARM64_REGISTER_Q16, HV_ARM64_REGISTER_Q17,
    HV_ARM64_REGISTER_Q18, HV_ARM64_REGISTER_Q19, HV_ARM64_REGISTER_Q20,
    HV_ARM64_REGISTER_Q21, HV_ARM64_REGISTER_Q22, HV_ARM64_REGISTER_Q23,
    HV_ARM64_REGISTER_Q24, HV_ARM64_REGISTER_Q25, HV_ARM64_REGISTER_Q26,
    HV_ARM64_REGISTER_Q27, HV_ARM64_REGISTER_Q28, HV_ARM64_REGISTER_Q29,
    HV_ARM64_REGISTER_Q30, HV_ARM64_REGISTER_Q31,
};

/*
 * Store the general-purpose, PC, SP, ELR, PSTATE and SPSR_EL1 state from the
 * CPUARMState into the mshv partition.
 */
static int store_core_regs(const CPUState *cpu)
{
    ARMCPU *arm_cpu = ARM_CPU(cpu);
    CPUARMState *env = &arm_cpu->env;
    struct hv_register_assoc assocs[ARRAY_SIZE(mshv_reg_match) + 2] = {};
    size_t n = 0;
    int ret;

    /*
     * Flush the current SP (kept in xregs[31]) into the sp_el[] banks so the
     * table-driven copy below picks up the correct value.
     */
    aarch64_save_sp(env, arm_current_el(env));

    for (size_t i = 0; i < ARRAY_SIZE(mshv_reg_match); i++) {
        assocs[n].name = mshv_reg_match[i].hv_reg;
        assocs[n].value.reg64 =
            *(uint64_t *)((void *)env + mshv_reg_match[i].offset);
        n++;
    }

    assocs[n].name = HV_ARM64_REGISTER_PSTATE;
    assocs[n].value.reg64 = pstate_read(env);
    n++;

    assocs[n].name = HV_ARM64_REGISTER_SPSR_EL1;
    assocs[n].value.reg64 = env->banked_spsr[aarch64_banked_spsr_index(1)];
    n++;

    ret = mshv_set_generic_regs(cpu, assocs, n);
    if (ret < 0) {
        error_report("failed to set core registers");
        return -1;
    }

    return 0;
}

static int load_core_regs(CPUState *cpu)
{
    ARMCPU *arm_cpu = ARM_CPU(cpu);
    CPUARMState *env = &arm_cpu->env;
    struct hv_register_assoc assocs[ARRAY_SIZE(mshv_reg_match) + 2] = {};
    size_t n_simple = ARRAY_SIZE(mshv_reg_match);
    size_t n = 0;
    uint64_t pstate;
    int ret;

    for (size_t i = 0; i < n_simple; i++) {
        assocs[n++].name = mshv_reg_match[i].hv_reg;
    }
    assocs[n++].name = HV_ARM64_REGISTER_PSTATE;
    assocs[n++].name = HV_ARM64_REGISTER_SPSR_EL1;

    ret = mshv_get_generic_regs(cpu, assocs, n);
    if (ret < 0) {
        error_report("failed to get core registers");
        return -1;
    }

    for (size_t i = 0; i < n_simple; i++) {
        *(uint64_t *)((void *)env + mshv_reg_match[i].offset) =
            assocs[i].value.reg64;
    }

    pstate = assocs[n_simple].value.reg64;
    env->aarch64 = ((pstate & PSTATE_nRW) == 0);
    pstate_write(env, pstate);

    env->banked_spsr[aarch64_banked_spsr_index(1)] =
        assocs[n_simple + 1].value.reg64;

    /* Reload xregs[31] from the sp_el[] bank for the current EL. */
    aarch64_restore_sp(env, arm_current_el(env));

    return 0;
}

static int store_fp_regs(const CPUState *cpu)
{
    ARMCPU *arm_cpu = ARM_CPU(cpu);
    CPUARMState *env = &arm_cpu->env;
    struct hv_register_assoc assocs[ARRAY_SIZE(mshv_fpreg_names) + 2] = {};
    size_t n = 0;
    int ret;

    for (size_t i = 0; i < ARRAY_SIZE(mshv_fpreg_names); i++) {
        assocs[n].name = mshv_fpreg_names[i];
        assocs[n].value.reg128.low_part = env->vfp.zregs[i].d[0];
        assocs[n].value.reg128.high_part = env->vfp.zregs[i].d[1];
        n++;
    }

    assocs[n].name = HV_ARM64_REGISTER_FPCR;
    assocs[n].value.reg64 = vfp_get_fpcr(env);
    n++;

    assocs[n].name = HV_ARM64_REGISTER_FPSR;
    assocs[n].value.reg64 = vfp_get_fpsr(env);
    n++;

    ret = mshv_set_generic_regs(cpu, assocs, n);
    if (ret < 0) {
        error_report("failed to set FP/SIMD registers");
        return -1;
    }

    return 0;
}

static int load_fp_regs(CPUState *cpu)
{
    ARMCPU *arm_cpu = ARM_CPU(cpu);
    CPUARMState *env = &arm_cpu->env;
    struct hv_register_assoc assocs[ARRAY_SIZE(mshv_fpreg_names) + 2] = {};
    size_t n_simd = ARRAY_SIZE(mshv_fpreg_names);
    size_t n = 0;
    int ret;

    for (size_t i = 0; i < n_simd; i++) {
        assocs[n++].name = mshv_fpreg_names[i];
    }
    assocs[n++].name = HV_ARM64_REGISTER_FPCR;
    assocs[n++].name = HV_ARM64_REGISTER_FPSR;

    ret = mshv_get_generic_regs(cpu, assocs, n);
    if (ret < 0) {
        error_report("failed to get FP/SIMD registers");
        return -1;
    }

    for (size_t i = 0; i < n_simd; i++) {
        env->vfp.zregs[i].d[0] = assocs[i].value.reg128.low_part;
        env->vfp.zregs[i].d[1] = assocs[i].value.reg128.high_part;
    }

    vfp_set_fpcr(env, assocs[n_simd].value.reg64);
    vfp_set_fpsr(env, assocs[n_simd + 1].value.reg64);

    return 0;
}

static int store_pauth_regs(const CPUState *cpu)
{
    ARMCPU *arm_cpu = ARM_CPU(cpu);
    CPUARMState *env = &arm_cpu->env;
    struct hv_register_assoc assocs[ARRAY_SIZE(mshv_pauth_reg_match)] = {};
    size_t n_regs = ARRAY_SIZE(mshv_pauth_reg_match);

    if (!cpu_isar_feature(aa64_pauth, arm_cpu)) {
        return 0;
    }

    for (size_t i = 0; i < n_regs; i++) {
        assocs[i].name = mshv_pauth_reg_match[i].hv_reg;
        assocs[i].value.reg64 =
            *(uint64_t *)((void *)env + mshv_pauth_reg_match[i].offset);
    }

    if (mshv_set_generic_regs(cpu, assocs, n_regs) < 0) {
        error_report("failed to set pointer authentication keys");
        return -1;
    }

    return 0;
}

static int load_pauth_regs(CPUState *cpu)
{
    ARMCPU *arm_cpu = ARM_CPU(cpu);
    CPUARMState *env = &arm_cpu->env;
    struct hv_register_assoc assocs[ARRAY_SIZE(mshv_pauth_reg_match)] = {};
    size_t n_regs = ARRAY_SIZE(mshv_pauth_reg_match);

    if (!cpu_isar_feature(aa64_pauth, arm_cpu)) {
        return 0;
    }

    for (size_t i = 0; i < n_regs; i++) {
        assocs[i].name = mshv_pauth_reg_match[i].hv_reg;
    }

    if (mshv_get_generic_regs(cpu, assocs, n_regs) < 0) {
        error_report("failed to get pointer authentication keys");
        return -1;
    }

    for (size_t i = 0; i < n_regs; i++) {
        *(uint64_t *)((void *)env + mshv_pauth_reg_match[i].offset) =
            assocs[i].value.reg64;
    }

    return 0;
}

static int store_sys_regs(const CPUState *cpu)
{
    ARMCPU *arm_cpu = ARM_CPU(cpu);
    CPUARMState *env = &arm_cpu->env;
    struct hv_register_assoc assocs[ARRAY_SIZE(mshv_sysreg_match)] = {};
    size_t n_regs = ARRAY_SIZE(mshv_sysreg_match);
    int ret;

    for (size_t i = 0; i < n_regs; i++) {
        assocs[i].name = mshv_sysreg_match[i].hv_reg;
        assocs[i].value.reg64 =
            *(uint64_t *)((void *)env + mshv_sysreg_match[i].offset);
    }

    ret = mshv_set_generic_regs(cpu, assocs, n_regs);
    if (ret < 0) {
        /*
         * The batched hvcall does not tell us which register was rejected.
         * Retry one register at a time so we can log the offending name(s).
         */
        for (size_t i = 0; i < n_regs; i++) {
            if (mshv_set_generic_regs(cpu, &assocs[i], 1) < 0) {
                error_report("failed to set system register 0x%08x "
                             "(value 0x%016" PRIx64 ")",
                             assocs[i].name, assocs[i].value.reg64);
            }
        }
        error_report("failed to set system registers");
        return -1;
    }

    return 0;
}

static int load_sys_regs(CPUState *cpu)
{
    ARMCPU *arm_cpu = ARM_CPU(cpu);
    CPUARMState *env = &arm_cpu->env;
    struct hv_register_assoc assocs[ARRAY_SIZE(mshv_sysreg_match)] = {};
    size_t n_regs = ARRAY_SIZE(mshv_sysreg_match);
    int ret;

    for (size_t i = 0; i < n_regs; i++) {
        assocs[i].name = mshv_sysreg_match[i].hv_reg;
    }

    ret = mshv_get_generic_regs(cpu, assocs, n_regs);
    if (ret < 0) {
        error_report("failed to get system registers");
        return -1;
    }

    for (size_t i = 0; i < n_regs; i++) {
        *(uint64_t *)((void *)env + mshv_sysreg_match[i].offset) =
            assocs[i].value.reg64;
    }

    return 0;
}

/*
 * Decode HV_REGISTER_INTERNAL_ACTIVITY_STATE for logging. Takes the raw value
 * rather than re-reading it, because every GET has to suspend the VP.
 */
static void mshv_log_internal_activity(const CPUState *cpu, const char *side,
                                       uint64_t raw)
{
    struct hv_register_assoc activity = {
        .name = HV_REGISTER_INTERNAL_ACTIVITY_STATE,
    };

    activity.value.internal_activity.as_uint64 = raw;

    info_report("MSHV activity %s vCPU %d: startup_suspend=%d halt_suspend=%d "
                "idle_suspend=%d raw=0x%" PRIx64,
                side, cpu->cpu_index,
                (int)activity.value.internal_activity.startup_suspend,
                (int)activity.value.internal_activity.halt_suspend,
                (int)activity.value.internal_activity.idle_suspend, raw);
}

/*
 * MSHV has no PSCI power-state register, and HV_REGISTER_EXPLICIT_SUSPEND is
 * not a usable substitute for one. It belongs to the root-scheduler dispatch
 * protocol: the mshv driver clears it immediately before every MSHV_RUN_VP
 * dispatch and sets it again once the VP stops being dispatched (see
 * mshv_vp_{set,clear}_explicit_suspend() in drivers/hv/mshv_root_main.c). A
 * perfectly healthy vCPU therefore reads back as "suspended" whenever it is not
 * currently executing - which is precisely when QEMU is able to look at it.
 * Sampling it into ARMCPU::power_state used to make every such VP appear
 * powered off; because power_state is migrated as part of vmstate_arm_cpu, that
 * bogus value travelled in the migration stream and the destination re-suspended
 * every vCPU, so the guest never resumed.
 *
 * PSCI itself is handled inside the hypervisor for MSHV (see the comment on
 * mshv_vcpu_thread_is_idle() in accel/mshv/mshv-all.c), so QEMU never has to
 * drive the guest's power transitions by hand. Clearing the bit here does not
 * by itself start the guest either: a VP only executes when a thread issues
 * MSHV_RUN_VP, and QEMU gates that in user space via cpu_can_run(), so a
 * paused vCPU stays paused regardless.
 *
 * What does have to be migrated is HV_REGISTER_INTERNAL_ACTIVITY_STATE, which
 * is the MSHV equivalent of KVM's MP state and the only VP runnability state
 * the hypervisor expects the VMM to carry across a migration. For exo
 * partitions the hypervisor deliberately does not restore it itself - that is
 * what HvRestorePartitionState does for Hyper-V-native save/restore - so this
 * register is the exo escape hatch, and a VP is forbidden from writing its own
 * copy. Of its three bits only StartupSuspend is migrated:
 *
 *   StartupSuspend  A VP the hypervisor has never started. Destination
 *                   secondaries come up with this set, and a StartupSuspended
 *                   VP is never dispatched, so leaving it set strands every AP
 *                   for the lifetime of the guest. Nothing the guest does can
 *                   clear it, so it has to travel.
 *   IdleSuspend     The ARM64 analogue of x64 HaltSuspend. Deliberately not
 *                   migrated: the hypervisor sets it itself whenever the VP
 *                   executes WFI, and QEMU dispatches every vCPU, so it is
 *                   derived state that re-establishes itself. Restoring APs
 *                   with StartupSuspend cleared and IdleSuspend left at 0 was
 *                   measured to work. It is also the one bit we cannot sample
 *                   honestly, because the GET has to suspend the VP.
 *   HaltSuspend     Rejected outright by the hypervisor on ARM64.
 *
 * StartupSuspend and IdleSuspend are mutually exclusive. Writing the register
 * also re-evaluates the VP's timers, so store_regs() applies it after the
 * timer deadline has been restored.
 */
static int store_mp_state(const CPUState *cpu)
{
    CPUState *cs = (CPUState *)cpu;
    ARMCPU *arm_cpu = ARM_CPU(cs);
    AccelCPUState *state = cpu->accel;
    int ret;

    /*
     * Resume the VP. The intercept suspend must be cleared before the
     * explicit suspend, otherwise the VP would remain suspended.
     */
    struct hv_register_assoc assocs[2] = {};
    assocs[0].name = HV_REGISTER_INTERCEPT_SUSPEND;
    assocs[0].value.intercept_suspend.suspended = 0;
    assocs[1].name = HV_REGISTER_EXPLICIT_SUSPEND;
    assocs[1].value.explicit_suspend.suspended = 0;

    ret = mshv_set_generic_regs(cpu, assocs, 2);
    if (ret < 0) {
        error_report("failed to set mp state");
        return -1;
    }

    info_report("MSHV mp_state vCPU %d: power_state=%d halted=%u",
                cs->cpu_index, arm_cpu->power_state, cs->halted);

    if (state->mp_state_restore_pending) {
        struct hv_register_assoc activity = {
            .name = HV_REGISTER_INTERNAL_ACTIVITY_STATE,
        };

        activity.value.internal_activity.as_uint64 = state->mp_state_activity;
        /*
         * Carry StartupSuspend only. HaltSuspend is rejected on ARM64, and
         * IdleSuspend is derived state the hypervisor re-establishes on the
         * next WFI (see the header comment).
         */
        activity.value.internal_activity.halt_suspend = 0;
        activity.value.internal_activity.idle_suspend = 0;

        ret = mshv_set_generic_regs(cpu, &activity, 1);
        if (ret < 0) {
            error_report("MSHV activity vCPU %d: SET failed ret=%d raw=0x%"
                         PRIx64, cs->cpu_index, ret,
                         activity.value.internal_activity.as_uint64);
            return -1;
        }

        mshv_log_internal_activity(cpu, "restored",
                                   activity.value.internal_activity.as_uint64);
        state->mp_state_restore_pending = false;
    }

    return 0;
}

static int load_mp_state(CPUState *cpu)
{
    struct hv_register_assoc activity = {
        .name = HV_REGISTER_INTERNAL_ACTIVITY_STATE,
    };
    int ret;

    /*
     * Never clobber a value that arrived in the migration stream but has not
     * been applied yet; the destination's own reading is stale by definition.
     */
    if (cpu->accel->mp_state_restore_pending) {
        return 0;
    }

    /*
     * HV_REGISTER_EXPLICIT_SUSPEND cannot be sampled to recover the guest's
     * power state: it belongs to the dispatch protocol and always reads back
     * set here, so it would misreport a perfectly healthy vCPU as powered off
     * (see the comment on store_mp_state()).
     *
     * HV_REGISTER_INTERNAL_ACTIVITY_STATE is the register that actually
     * carries VP runnability. Unlike its setter, which is restricted to exo
     * partitions, the getter is permissive, so this is safe everywhere.
     */
    ret = mshv_get_generic_regs(cpu, &activity, 1);
    if (ret < 0) {
        warn_report("MSHV activity vCPU %d: GET failed ret=%d, MP state will "
                    "not be migrated", cpu->cpu_index, ret);
        return 0;
    }

    cpu->accel->mp_state_activity = activity.value.internal_activity.as_uint64;
    mshv_log_internal_activity(cpu, "sampled", cpu->accel->mp_state_activity);

    return 0;
}

static int load_regs(CPUState *cpu)
{
    int ret;

    ret = load_core_regs(cpu);
    if (ret < 0) {
        error_report("Failed to load core registers");
        return -1;
    }

    ret = load_fp_regs(cpu);
    if (ret < 0) {
        error_report("Failed to load FP/SIMD registers");
        return -1;
    }

    ret = load_sys_regs(cpu);
    if (ret < 0) {
        error_report("Failed to load system registers");
        return -1;
    }

    ret = load_pauth_regs(cpu);
    if (ret < 0) {
        error_report("Failed to load pointer authentication keys");
        return -1;
    }

    ret = load_mp_state(cpu);
    if (ret < 0) {
        error_report("Failed to load mp state");
        return -1;
    }

    return 0;
}

/*
 * Source-side capture of the virtual timer registers. These are not part of the
 * normal register set, so we need to explicitly save them here.
 */
static int mshv_arm_timer_pre_save(void *opaque)
{
    ARMCPU *arm_cpu = opaque;
    CPUState *cpu = CPU(arm_cpu);
    CPUARMState *env = &arm_cpu->env;
    struct hv_register_assoc counter = {
        .name = HV_ARM64_REGISTER_CNTVCT_EL0,
    };
    int ret;

    struct hv_register_assoc assocs[] = {
        {
            .name = HV_ARM64_REGISTER_CNTV_CTL_EL0,
        },
        {
            .name = HV_ARM64_REGISTER_CNTV_CVAL_EL0,
        },
    };

    ret = mshv_get_generic_regs(cpu, assocs, ARRAY_SIZE(assocs));
    if (ret < 0) {
        error_report("MSHV ARM timer source vCPU %d: GET failed ret=%d",
                     cpu->cpu_index, ret);
        return -1;
    }

    env->cp15.c14_timer[GTIMER_VIRT].ctl = assocs[0].value.reg64;
    env->cp15.c14_timer[GTIMER_VIRT].cval = assocs[1].value.reg64;
    info_report("MSHV ARM timer source vCPU %d: GET ret=%d "
                "CNTV_CTL=0x%" PRIx64 " CNTV_CVAL=0x%" PRIx64,
                cpu->cpu_index, ret, assocs[0].value.reg64,
                assocs[1].value.reg64);

    ret = mshv_get_generic_regs(cpu, &counter, 1);
    if (ret < 0) {
        error_report("MSHV ARM timer source vCPU %d: CNTVCT GET failed "
                     "ret=%d", cpu->cpu_index, ret);
        cpu->accel->arm_timer_cntvct_valid = false;
    } else {
        cpu->accel->arm_timer_cntvct = counter.value.reg64;
        cpu->accel->arm_timer_cntvct_valid = true;
        info_report("MSHV ARM timer source vCPU %d: CNTVCT GET ret=%d "
                    "CNTVCT=0x%" PRIx64,
                    cpu->cpu_index, ret, counter.value.reg64);
    }

    return 0;
}

/*
 * The destination callback does not touch Hyper-V because other migration
 * sections, including the GIC, may still be loading.
 */
static int mshv_arm_timer_post_load(void *opaque, int version_id)
{
    ARMCPU *arm_cpu = opaque;
    CPUState *cpu = CPU(arm_cpu);

    cpu->accel->arm_timer_restore_pending = true;
    return 0;
}

/* CNTV_CTL_EL0 bits. */
#define CNTV_CTL_ENABLE  (1U << 0)
#define CNTV_CTL_IMASK   (1U << 1)

static int store_timer_regs(const CPUState *cpu)
{
    ARMCPU *arm_cpu = ARM_CPU(cpu);
    CPUARMState *env = &arm_cpu->env;
    AccelCPUState *state = cpu->accel;
    struct hv_register_assoc assocs[2] = {};
    struct hv_register_assoc before[2] = {
        {
            .name = HV_ARM64_REGISTER_CNTV_CTL_EL0,
        },
        {
            .name = HV_ARM64_REGISTER_CNTV_CVAL_EL0,
        },
    };
    struct hv_register_assoc counter = {
        .name = HV_ARM64_REGISTER_CNTVCT_EL0,
    };
    struct hv_register_assoc readback[2] = {
        {
            .name = HV_ARM64_REGISTER_CNTV_CTL_EL0,
        },
        {
            .name = HV_ARM64_REGISTER_CNTV_CVAL_EL0,
        },
    };
    int ret;

    if (!state->arm_timer_restore_pending) {
        return 0;
    }

    info_report("MSHV ARM timer destination vCPU %d: restore pending "
                "CNTV_CTL=0x%" PRIx64 " CNTV_CVAL=0x%" PRIx64,
                cpu->cpu_index,
                env->cp15.c14_timer[GTIMER_VIRT].ctl,
                env->cp15.c14_timer[GTIMER_VIRT].cval);

    ret = mshv_get_generic_regs((CPUState *)cpu, before,
                                ARRAY_SIZE(before));
    if (ret < 0) {
        error_report("MSHV ARM timer destination vCPU %d: pre-SET GET "
                     "failed ret=%d", cpu->cpu_index, ret);
    } else {
        info_report("MSHV ARM timer destination vCPU %d: pre-SET GET ret=%d "
                    "CNTV_CTL=0x%" PRIx64 " CNTV_CVAL=0x%" PRIx64,
                    cpu->cpu_index, ret, before[0].value.reg64,
                    before[1].value.reg64);
    }

    ret = mshv_get_generic_regs((CPUState *)cpu, &counter, 1);
    if (ret < 0) {
        error_report("MSHV ARM timer destination vCPU %d: pre-SET CNTVCT "
                     "GET failed ret=%d", cpu->cpu_index, ret);
    } else {
        info_report("MSHV ARM timer destination vCPU %d: pre-SET CNTVCT "
                    "GET ret=%d CNTVCT=0x%" PRIx64,
                    cpu->cpu_index, ret, counter.value.reg64);
    }

    /*
     * Restore the guest virtual counter before the deadline.
     *
     * CNTV_CVAL_EL0 is an absolute deadline in the source partition's counter
     * timebase, so it only means anything once the counter it was measured
     * against is back in place. Writing it while the destination counter still
     * reads its own (much lower) uptime leaves the deadline far in the future:
     * the timer then stalls for as long as the source VM had been running, and
     * the guest's first counter read after resuming goes backwards, which Linux
     * turns into a large forward jump in its timebase.
     *
     * CNTVOFF_EL2 is not usable here even though it is the architectural home
     * of this offset: MSHV rejects both reads and writes of it. CNTVCT_EL0 is
     * the supported route -- the hypervisor maps it onto the VP's internal TSC
     * and explicitly permits the parent of an exo partition to restore it. It
     * is the MSHV analogue of KVM_REG_ARM_TIMER_CNT.
     *
     * Caveat: the hypervisor keeps the reference TSC page valid across this
     * write only while partition time is frozen, and deactivates it otherwise.
     * A Linux guest reading CNTVCT_EL0 directly (clocksource arch_sys_counter)
     * is unaffected, but a guest relying on that enlightenment would fall back
     * to the register.
     */
    if (state->arm_timer_cntvct_restore_pending) {
        struct hv_register_assoc restore_counter = {
            .name = HV_ARM64_REGISTER_CNTVCT_EL0,
            .value.reg64 = state->arm_timer_cntvct,
        };

        ret = mshv_set_generic_regs(cpu, &restore_counter, 1);
        if (ret < 0) {
            /*
             * Non-fatal: leaving the counter alone reproduces the pre-fix
             * behaviour (a stalled timer) rather than failing the migration.
             */
            error_report("MSHV ARM timer destination vCPU %d: CNTVCT SET "
                         "failed ret=%d CNTVCT=0x%" PRIx64,
                         cpu->cpu_index, ret, restore_counter.value.reg64);
        } else {
            info_report("MSHV ARM timer destination vCPU %d: CNTVCT SET ret=%d "
                        "CNTVCT=0x%" PRIx64,
                        cpu->cpu_index, ret, restore_counter.value.reg64);
        }

        state->arm_timer_cntvct_restore_pending = false;
    }

    /*
     * Install the deadline before restoring ENABLE/IMASK. Hyper-V can then
     * schedule the deadline or immediately assert PPI 27 if it has expired.
     */
    assocs[0].name = HV_ARM64_REGISTER_CNTV_CVAL_EL0;
    assocs[0].value.reg64 = env->cp15.c14_timer[GTIMER_VIRT].cval;

    assocs[1].name = HV_ARM64_REGISTER_CNTV_CTL_EL0;
    assocs[1].value.reg64 = env->cp15.c14_timer[GTIMER_VIRT].ctl;

    ret = mshv_set_generic_regs(cpu, assocs, ARRAY_SIZE(assocs));
    if (ret < 0) {
        error_report("MSHV ARM timer destination vCPU %d: SET failed ret=%d "
                     "CNTV_CVAL=0x%" PRIx64 " CNTV_CTL=0x%" PRIx64,
                     cpu->cpu_index, ret, assocs[0].value.reg64,
                     assocs[1].value.reg64);
        return -1;
    }

    info_report("MSHV ARM timer destination vCPU %d: SET ret=%d "
                "CNTV_CVAL=0x%" PRIx64 " CNTV_CTL=0x%" PRIx64,
                cpu->cpu_index, ret, assocs[0].value.reg64,
                assocs[1].value.reg64);

    ret = mshv_get_generic_regs((CPUState *)cpu, readback,
                                ARRAY_SIZE(readback));
    if (ret < 0) {
        error_report("MSHV ARM timer destination vCPU %d: post-SET GET "
                     "failed ret=%d", cpu->cpu_index, ret);
    } else {
        info_report("MSHV ARM timer destination vCPU %d: post-SET GET ret=%d "
                    "CNTV_CTL=0x%" PRIx64 " CNTV_CVAL=0x%" PRIx64,
                    cpu->cpu_index, ret, readback[0].value.reg64,
                    readback[1].value.reg64);
    }

    counter.value.reg64 = 0;
    ret = mshv_get_generic_regs((CPUState *)cpu, &counter, 1);
    if (ret < 0) {
        error_report("MSHV ARM timer destination vCPU %d: post-SET CNTVCT "
                     "GET failed ret=%d", cpu->cpu_index, ret);
    } else {
        info_report("MSHV ARM timer destination vCPU %d: post-SET CNTVCT "
                    "GET ret=%d CNTVCT=0x%" PRIx64,
                    cpu->cpu_index, ret, counter.value.reg64);
    }

    /*
     * Sample PPI 27 for every vCPU, not only the ones that dispatch. The
     * secondaries never take an intercept, so they never reach the
     * post-dispatch probe; this is the only place their GIC state is visible.
     */
    mshv_gic_dump_timer_ppi(cpu, "post-restore");

    /*
     * Arm the post-dispatch probe with what the hypervisor reports right now,
     * before the VP has ever been run. mshv_run_vcpu() re-reads these after
     * MSHV_RUN_VP returns; a divergence proves the values were clobbered by
     * the context load rather than simply never delivered.
     */
    state->arm_timer_probe_ctl = readback[0].value.reg64;
    state->arm_timer_probe_cval = readback[1].value.reg64;
    state->arm_timer_probe_runs = 0;
    state->arm_timer_probe_armed = true;
    state->dst_trace = true;

    state->arm_timer_restore_pending = false;

    /*
     * Hyper-V accepted the writes above but will discard them when the VP
     * leaves precise work, so the restored deadline never re-asserts PPI 27.
     * Deliver one timer interrupt through the GIC state path instead, which is
     * not affected by that defect, and let the guest reprogram the timer for
     * itself.
     */
    if ((env->cp15.c14_timer[GTIMER_VIRT].ctl & CNTV_CTL_ENABLE) &&
        !(env->cp15.c14_timer[GTIMER_VIRT].ctl & CNTV_CTL_IMASK)) {
        mshv_gic_rearm_timer_ppi(cpu);
    }

    return 0;
}

/*
 * Diagnostic probe. Every timer readback prior to this one was taken before the
 * destination VP had ever been dispatched, which cannot distinguish:
 *
 *   (a) the SET persisted and PPI 27 is simply never asserted, from
 *   (b) the first context load overwrote EL02 from a stale inactive store.
 *
 * Re-read the same registers after MSHV_RUN_VP has returned and compare against
 * the pre-dispatch sample stashed by store_timer_regs(). Limited to the first
 * few dispatches because each GET suspends the VP.
 */
#define MSHV_TIMER_PROBE_DISPATCHES 8

/* ISR_EL1 / DAIF interrupt bits. */
#define ARM_IRQ_BIT_I (1ULL << 7)
#define ARM_IRQ_BIT_F (1ULL << 6)

static void mshv_arm_timer_probe_post_dispatch(CPUState *cpu)
{
    AccelCPUState *state = cpu->accel;
    struct hv_register_assoc probe[2] = {
        {
            .name = HV_ARM64_REGISTER_CNTV_CTL_EL0,
        },
        {
            .name = HV_ARM64_REGISTER_CNTV_CVAL_EL0,
        },
    };
    struct hv_register_assoc counter = {
        .name = HV_ARM64_REGISTER_CNTVCT_EL0,
    };
    struct hv_register_assoc core[4] = {
        {
            .name = HV_ARM64_REGISTER_PC,
        },
        {
            .name = HV_ARM64_REGISTER_PSTATE,
        },
        {
            .name = HV_ARM64_REGISTER_DAIF,
        },
        {
            .name = HV_ARM64_REGISTER_ISR_EL1,
        },
    };
    bool persisted;
    int core_ret[4];
    unsigned i;
    int ret;

    if (!state->arm_timer_probe_armed) {
        return;
    }

    state->arm_timer_probe_runs++;

    ret = mshv_get_generic_regs(cpu, probe, ARRAY_SIZE(probe));
    if (ret < 0) {
        error_report("MSHV ARM timer probe vCPU %d: post-dispatch GET failed "
                     "ret=%d", cpu->cpu_index, ret);
        state->arm_timer_probe_armed = false;
        return;
    }

    if (mshv_get_generic_regs(cpu, &counter, 1) < 0) {
        counter.value.reg64 = 0;
    }

    /*
     * Read these one at a time: a single unsupported register name fails the
     * whole batch, and a silently zeroed batch is indistinguishable from a
     * guest genuinely reading zero.
     */
    for (i = 0; i < ARRAY_SIZE(core); i++) {
        core_ret[i] = mshv_get_generic_regs(cpu, &core[i], 1);
        if (core_ret[i] < 0) {
            core[i].value.reg64 = 0;
        }
    }

    persisted = (probe[0].value.reg64 == state->arm_timer_probe_ctl) &&
                (probe[1].value.reg64 == state->arm_timer_probe_cval);

    info_report("MSHV ARM timer probe vCPU %d: after dispatch #%u "
                "CNTV_CTL=0x%" PRIx64 " CNTV_CVAL=0x%" PRIx64
                " CNTVCT=0x%" PRIx64 " (pre-dispatch CNTV_CTL=0x%" PRIx64
                " CNTV_CVAL=0x%" PRIx64 ") %s",
                cpu->cpu_index, state->arm_timer_probe_runs,
                probe[0].value.reg64, probe[1].value.reg64,
                counter.value.reg64, state->arm_timer_probe_ctl,
                state->arm_timer_probe_cval,
                persisted ? "TIMER-REGS-SAME" : "TIMER-REGS-CHANGED");

    /*
     * ISR_EL1.I reports whether an IRQ is currently being presented to the
     * core by the CPU interface, and DAIF.I whether the guest has IRQs masked.
     * Together they separate "the interrupt never arrives" from "it arrives
     * but the guest cannot take it".
     */
    info_report("MSHV ARM core probe vCPU %d: after dispatch #%u "
                "PC=0x%" PRIx64 "(r%d) PSTATE=0x%" PRIx64 "(r%d) "
                "DAIF=0x%" PRIx64 "(r%d) ISR_EL1=0x%" PRIx64 "(r%d) "
                "ISR.I=%d ISR.F=%d DAIF.I=%d -> %s",
                cpu->cpu_index, state->arm_timer_probe_runs,
                core[0].value.reg64, core_ret[0],
                core[1].value.reg64, core_ret[1],
                core[2].value.reg64, core_ret[2],
                core[3].value.reg64, core_ret[3],
                !!(core[3].value.reg64 & ARM_IRQ_BIT_I),
                !!(core[3].value.reg64 & ARM_IRQ_BIT_F),
                !!(core[2].value.reg64 & ARM_IRQ_BIT_I),
                core_ret[3] < 0 ? "ISR-UNREADABLE"
                : (core[3].value.reg64 & ARM_IRQ_BIT_I)
                    ? ((core[2].value.reg64 & ARM_IRQ_BIT_I)
                           ? "IRQ-PENDING-BUT-MASKED"
                           : "IRQ-PENDING-UNMASKED")
                    : "NO-IRQ-AT-CORE");

    /*
     * Sample PPI 27 in the same window as CNTV_CTL above. If CNTV_CTL reads
     * 0x5 (expired) while PPI 27 reads asserted=0, the hypervisor never
     * asserted it and the fault is upstream, in the assertion path. If it
     * reads asserted=1, assertion works and the fault is in delivery.
     */
    mshv_gic_dump_timer_ppi(cpu, "post-dispatch");

    if (state->arm_timer_probe_runs >= MSHV_TIMER_PROBE_DISPATCHES) {
        state->arm_timer_probe_armed = false;
    }
}

static const VMStateDescription vmstate_mshv_arm_timer = {
    .name = "cpu/mshv-arm-timer",
    .version_id = 1,
    .minimum_version_id = 1,
    .pre_save = mshv_arm_timer_pre_save,
    .post_load = mshv_arm_timer_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT64(env.cp15.c14_timer[GTIMER_VIRT].ctl, ARMCPU),
        VMSTATE_UINT64(env.cp15.c14_timer[GTIMER_VIRT].cval, ARMCPU),
        VMSTATE_END_OF_LIST()
    },
};

/*
 * The guest virtual counter travels in its own section, with the same
 * deferred-apply discipline as MP state: post_load only records that a value
 * arrived, and store_timer_regs() writes it once the rest of the VM is loaded.
 *
 * Validity travels as data rather than through a .needed predicate. .needed is
 * evaluated before any pre_save runs, so it cannot observe a value sampled
 * during save; carrying the flag in the stream instead makes the restore
 * fail-safe -- if the source could not read the counter, the destination is
 * told so explicitly and simply keeps its own.
 *
 * The counter is sampled in mshv_arm_timer_pre_save() rather than here, so that
 * it is read in the same window as CNTV_CVAL_EL0: the deadline is only
 * meaningful relative to the counter it was measured against, and the virtual
 * counter keeps advancing while the VM is stopped. That makes this section's
 * contents depend on the timer section having been saved first, which holds
 * because mshv_arch_init_vcpu() registers them in that order.
 */
static int mshv_arm_counter_post_load(void *opaque, int version_id)
{
    AccelCPUState *state = opaque;

    state->arm_timer_cntvct_restore_pending = state->arm_timer_cntvct_valid;
    return 0;
}

static const VMStateDescription vmstate_mshv_arm_counter = {
    .name = "cpu/mshv-arm-counter",
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = mshv_arm_counter_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_BOOL(arm_timer_cntvct_valid, AccelCPUState),
        VMSTATE_UINT64(arm_timer_cntvct, AccelCPUState),
        VMSTATE_END_OF_LIST()
    },
};

/*
 * MP state travels as its own subsection rather than through ARMCPU, because it
 * is sampled straight out of the hypervisor by load_mp_state() and has no
 * QEMU-side representation. The destination must not apply it from post_load:
 * other migration sections, including the GIC, may still be loading, so just
 * record that a restore is due and let store_mp_state() perform it.
 */static int mshv_mp_state_post_load(void *opaque, int version_id)
{
    AccelCPUState *state = opaque;

    state->mp_state_restore_pending = true;
    return 0;
}

static const VMStateDescription vmstate_mshv_mp_state = {
    .name = "cpu/mshv-mp-state",
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = mshv_mp_state_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT64(mp_state_activity, AccelCPUState),
        VMSTATE_END_OF_LIST()
    },
};

static int store_regs(const CPUState *cpu)
{
    int ret;

    ret = store_core_regs(cpu);
    if (ret < 0) {
        error_report("Failed to store core registers");
        return -1;
    }

    ret = store_fp_regs(cpu);
    if (ret < 0) {
        error_report("Failed to store FP/SIMD registers");
        return -1;
    }

    ret = store_sys_regs(cpu);
    if (ret < 0) {
        error_report("Failed to store system registers");
        return -1;
    }

    ret = store_pauth_regs(cpu);
    if (ret < 0) {
        error_report("Failed to store pointer authentication keys");
        return -1;
    }

    ret = store_timer_regs(cpu);
    if (ret < 0) {
        error_report("Failed to store virtual timer state");
        return -1;
    }

    /*
     * Last: writing the activity register re-evaluates the VP's timers, so the
     * restored deadline has to be in place first, and a VP should only be made
     * runnable once the rest of its state has landed.
     */
    ret = store_mp_state(cpu);
    if (ret < 0) {
        error_report("Failed to store mp state");
        return -1;
    }

    return 0;
}

int mshv_arch_load_vcpu_state(CPUState *cpu)
{
    return load_regs(cpu);
}

int mshv_arch_store_vcpu_state(const CPUState *cpu)
{
    return store_regs(cpu);
}

static int set_memory_info(const struct hyperv_message *msg,
                           struct hv_arm64_memory_intercept_message *info)
{
    if (msg->header.message_type != HVMSG_GPA_INTERCEPT
            && msg->header.message_type != HVMSG_UNMAPPED_GPA
            && msg->header.message_type != HVMSG_UNACCEPTED_GPA) {
        error_report("invalid message type");
        return -1;
    }
    memcpy(info, msg->payload, sizeof(*info));

    return 0;
}

static uint64_t mshv_mmio_get_reg(CPUState *cpu, int reg_index)
{
    ARMCPU *arm_cpu = ARM_CPU(cpu);
    CPUARMState *env = &arm_cpu->env;
    return reg_index < 31 ? env->xregs[reg_index] : 0ULL;
}

static void mshv_mmio_set_reg(CPUState *cpu, int reg_index, uint64_t val)
{
    ARMCPU *arm_cpu = ARM_CPU(cpu);
    CPUARMState *env = &arm_cpu->env;
    if (reg_index < 31) {
        env->xregs[reg_index] = val;
    }
}

static int handle_unmapped_mem(int vm_fd, CPUState *cpu,
                               const struct hyperv_message *msg,
                               MshvVmExit *exit_reason)
{
    ARMCPU *arm_cpu = ARM_CPU(cpu);
    CPUARMState *env = &arm_cpu->env;
    struct hv_arm64_memory_intercept_message info = { 0 };
    int ret;
    EsrEl2 syndrome;

    ret = set_memory_info(msg, &info);
    if (ret < 0) {
        error_report("failed to convert message to memory info");
        return -1;
    }

    syndrome.raw = info.syndrome;

    if (cpu->accel->dst_trace) {
        AccelCPUState *st = cpu->accel;
        st->dst_mmio++;
        if (st->dst_mmio <= 40 || (st->dst_mmio % 2000) == 0) {
            info_report("MSHV MMIO dst vCPU %d: #%u gpa=0x%" PRIx64
                        " gva=0x%" PRIx64 " write=%d pc=0x%" PRIx64,
                        cpu->cpu_index, st->dst_mmio,
                        (uint64_t)info.guest_physical_address,
                        (uint64_t)info.guest_virtual_address,
                        syndrome.iss >> 6 & 1, (uint64_t)env->pc);
        }
    }

    /*
     * MMIO emulation must touch as little vCPU state as possible. Only the
     * transfer register (for the value moved to/from the device) and the PC
     * (to step over the faulting instruction) are needed.
     *
     * Deliberately do NOT round-trip the full core register set here: writing
     * SP, PSTATE and SPSR back to MSHV on every MMIO exit can perturb state
     * that the guest depends on across the trap. In particular, Pointer
     * Authentication uses SP as the signing modifier, so any transient SP or
     * PSTATE inconsistency corrupts PAC and faults (FPAC) on the next
     * autiasp/autibsp. KVM likewise only writes back the transfer register and
     * PC for an MMIO exit.
     */
    IssDataAbort iss = { .raw = syndrome.iss };
    uint32_t srt = iss.srt;

    /* For a store, fetch just the source transfer register from MSHV. */
    if (iss.wnr && srt < 31) {
        struct hv_register_assoc in = { .name = mshv_reg_match[srt].hv_reg };
        if (mshv_get_generic_regs(cpu, &in, 1) < 0) {
            error_report("Failed to load MMIO transfer register");
            return -1;
        }
        env->xregs[srt] = in.value.reg64;
    }

    static const struct arm_emul_ops mshv_arm_emul_ops = {
        .get_reg = mshv_mmio_get_reg,
        .set_reg = mshv_mmio_set_reg,
    };

    ret = arm_emulate_mmio(cpu, syndrome, info.guest_physical_address,
                           &mshv_arm_emul_ops);
    if (ret < 0) {
        error_report("Failed to emulate MMIO access at gpa 0x%" PRIx64
                     " (pc 0x%" PRIx64 ", syndrome 0x%" PRIx64 ")",
                     info.guest_physical_address, info.header.pc,
                     info.syndrome);
        return -1;
    }

    /*
     * Write back only the PC (advanced past the faulting instruction) and,
     * for a load, the destination transfer register. Use the PC reported in
     * the intercept message rather than a previously-synced env->pc.
     */
    struct hv_register_assoc out[2];
    size_t nout = 0;

    uint64_t advanced_pc = info.header.pc + (syndrome.il == 1 ? 4 : 2);

    out[nout].name = HV_ARM64_REGISTER_PC;
    out[nout].value.reg64 = advanced_pc;
    nout++;

    if (!iss.wnr && srt < 31) {
        out[nout].name = mshv_reg_match[srt].hv_reg;
        out[nout].value.reg64 = env->xregs[srt];
        nout++;
    }

    /*
     * Keep env->pc consistent with the PC advanced in MSHV so env never holds
     * a stale (pre-advance) PC after this exit. The vcpu_dirty clear below is
     * what actually prevents a harmful full writeback, but keeping env correct
     * avoids surprises for any later legitimate synchronize.
     */
    env->pc = advanced_pc;

    if (mshv_set_generic_regs(cpu, out, nout) < 0) {
        error_report("Failed to store MMIO result registers");
        return -1;
    }

    /*
     * MMIO emulation can re-enter QEMU code that calls cpu_synchronize_state()
     * -- e.g. virtio_reset() -> cpu_internal_is_big_endian() on a virtio
     * Status write. On MSHV that does a full load_regs() and marks the vCPU
     * dirty, even though the synchronize is read-only (QEMU inspects env but
     * never changes it). If the vCPU is left dirty, the run loop performs a
     * full store_regs() that blindly overwrites MSHV's live VP register state.
     * That clobbers state MSHV updates asynchronously -- in particular the
     * virtual-timer interrupt entry context and the SCTLR_EL1 pointer-auth
     * enable bits -- corrupting Pointer Authentication so a signed pointer is
     * branched to without being authenticated (crash in the timer handler).
     *
     * We have already reconciled the only state MMIO can change (the transfer
     * register and PC) via the minimal writeback above, and env now matches
     * MSHV. Clear the dirty flag so the run loop does not push the full,
     * redundant register set back and race MSHV's own updates.
     */
    cpu->vcpu_dirty = false;

    *exit_reason = MshvVmExitIgnore;

    return 0;
}

int mshv_run_vcpu(int vm_fd, CPUState *cpu, hv_message *msg, MshvVmExit *exit)
{
    int ret;
    int cpu_fd = mshv_vcpufd(cpu);
    AccelCPUState *state = cpu->accel;

    if (state->arm_timer_probe_armed) {
        info_report("MSHV VP dispatch vCPU %d: entering MSHV_RUN_VP #%u",
                    cpu->cpu_index, state->arm_timer_probe_runs + 1);
    }

    ret = ioctl(cpu_fd, MSHV_RUN_VP, msg);
    if (ret < 0) {        if (state->dst_trace) {
            state->dst_run_eintr++;
            if (state->dst_run_eintr <= 20 ||
                (state->dst_run_eintr % 500) == 0) {
                error_report("MSHV VP dst vCPU %d: RUN_VP errno=%d (%s) "
                             "[cumulative ok=%u eintr=%u]", cpu->cpu_index,
                             errno, strerror(errno), state->dst_run_ok,
                             state->dst_run_eintr);
            }
        }
        if (state->arm_timer_probe_armed) {
            error_report("MSHV VP dispatch vCPU %d: MSHV_RUN_VP FAILED "
                         "ret=%d errno=%d (%s)", cpu->cpu_index, ret,
                         errno, strerror(errno));
        }
        *exit = MshvVmExitShutdown;
        return -errno;
    }

    if (state->dst_trace) {
        state->dst_run_ok++;
        if (state->dst_run_ok <= 5 || (state->dst_run_ok % 2000) == 0) {
            info_report("MSHV VP dst vCPU %d: RUN_VP ok msg=0x%x "
                        "[cumulative ok=%u eintr=%u]", cpu->cpu_index,
                        msg->header.message_type, state->dst_run_ok,
                        state->dst_run_eintr);
        }
    }

    if (state->arm_timer_probe_armed) {
        info_report("MSHV VP dispatch vCPU %d: returned from MSHV_RUN_VP #%u "
                    "message_type=0x%x", cpu->cpu_index,
                    state->arm_timer_probe_runs + 1, msg->header.message_type);
    }

    mshv_arm_timer_probe_post_dispatch(cpu);

    switch (msg->header.message_type) {
    case HVMSG_NONE:
        /*
         * No intercept message. This happens when the VP run is interrupted,
         * e.g. by a SIG_IPI kick for pending timer/I/O work. Re-enter the VP.
         */
        *exit = MshvVmExitIgnore;
        break;
    case HVMSG_UNRECOVERABLE_EXCEPTION:
        *exit = MshvVmExitShutdown;
        break;
    case HVMSG_GPA_INTERCEPT:
    case HVMSG_UNMAPPED_GPA:
        ret = handle_unmapped_mem(vm_fd, cpu, msg, exit);
        if (ret < 0) {
            error_report("failed to handle mmio");
            return -1;
        }
        break;
    default:
        error_report("Unhandled message type: 0x%x", msg->header.message_type);
        return -1;
    }

    return 0;
}

void mshv_arch_init_vcpu(CPUState *cpu)
{
    AccelCPUState *state = cpu->accel;

    mshv_setup_hvcall_args(state);
    vmstate_register(NULL, cpu->cpu_index,
                     &vmstate_mshv_arm_timer, ARM_CPU(cpu));
    vmstate_register(NULL, cpu->cpu_index, &vmstate_mshv_arm_counter, state);
    vmstate_register(NULL, cpu->cpu_index, &vmstate_mshv_mp_state, state);
}

void mshv_arch_destroy_vcpu(CPUState *cpu)
{
    AccelCPUState *state = cpu->accel;

    vmstate_unregister(NULL, &vmstate_mshv_mp_state, state);
    vmstate_unregister(NULL, &vmstate_mshv_arm_counter, state);
    vmstate_unregister(NULL, &vmstate_mshv_arm_timer, ARM_CPU(cpu));

    if (state->hvcall_args.base) {
        qemu_vfree(state->hvcall_args.base);
    }

    state->hvcall_args = (MshvHvCallArgs){0};
}

static int set_partition_prop(int vm_fd, uint32_t prop_code,
                                uint64_t prop_value)
{
    int ret;
    struct hv_input_set_partition_property in = {0};
    in.property_code = prop_code;
    in.property_value = prop_value;

    struct mshv_root_hvcall args = {0};
    args.code = HVCALL_SET_PARTITION_PROPERTY;
    args.in_sz = sizeof(in);
    args.in_ptr = (uint64_t)&in;

    ret = mshv_hvcall(vm_fd, &args);
    if (ret < 0) {
        error_report("Failed to set partition property code %u", prop_code);
        return -1;
    }

    return 0;
}

int mshv_arch_pre_init_vm(int vm_fd)
{
    int ret;
    VirtMachineState *vms = VIRT_MACHINE(qdev_get_machine());

    ret = set_partition_prop(vm_fd,
                            HV_PARTITION_PROPERTY_GICD_BASE_ADDRESS,
                            vms->memmap[VIRT_GIC_DIST].base);
    if (ret < 0) {
        return ret;
    }

    ret = set_partition_prop(vm_fd,
                        HV_PARTITION_PROPERTY_GITS_TRANSLATER_BASE_ADDRESS,
                        vms->memmap[VIRT_GIC_ITS].base);
    if (ret < 0) {
        return ret;
    }

    ret = set_partition_prop(vm_fd,
                        HV_PARTITION_PROPERTY_GIC_LPI_INT_ID_BITS,
                        0);
    if (ret < 0) {
        return ret;
    }

    ret = set_partition_prop(vm_fd,
                        HV_PARTITION_PROPERTY_GIC_PPI_OVERFLOW_INTERRUPT_FROM_CNTV,
                        ARCH_TIMER_VIRT_IRQ);
    if (ret < 0) {
        return ret;
    }

    ret = set_partition_prop(vm_fd,
                        HV_PARTITION_PROPERTY_GIC_PPI_PERFORMANCE_MONITORS_INTERRUPT,
                        VIRTUAL_PMU_IRQ);

    return ret;
}

static uint32_t mshv_arm_get_ipa_bit_size(int mshv_fd)
{
    int ret;
    struct hv_input_get_partition_property in = {0};
    struct hv_output_get_partition_property out = {0};
    struct mshv_root_hvcall args = {0};

    in.property_code = HV_PARTITION_PROPERTY_PHYSICAL_ADDRESS_WIDTH;

    args.code = HVCALL_GET_PARTITION_PROPERTY;
    args.in_sz = sizeof(in);
    args.in_ptr = (uint64_t)&in;
    args.out_sz = sizeof(out);
    args.out_ptr = (uint64_t)&out;

    ret = mshv_hvcall(mshv_fd, &args);

    if (ret < 0) {
        error_report("Failed to get IPA size");
        exit(1);
    }

    return out.property_value;
}

int mshv_arch_accel_init(AccelState *as, MachineState *ms, int mshv_fd)
{
    MachineClass *mc = MACHINE_GET_CLASS(ms);
    int pa_range;
    uint32_t ipa_size;

    if (mc->get_physical_address_range) {
        ipa_size = mshv_arm_get_ipa_bit_size(mshv_fd);
        pa_range = mc->get_physical_address_range(ms, ipa_size, ipa_size);
        if (pa_range < 0) {
            return -EINVAL;
        }
    }

    return 0;
}

void mshv_arch_amend_proc_features(
    union hv_partition_synthetic_processor_features *features)
{
    /*
     * TLB-flush hypercalls are an x86 enlightenment; on ARM the guest
     * broadcasts TLBI instructions instead. Newer hypervisors reject the
     * bit with HV_STATUS_PROPERTY_VALUE_OUT_OF_RANGE, which fails partition
     * setup, so never request it here.
     */
    features->tb_flush_hypercalls = 0;
}

void mshv_arch_disable_partition_proc_features(
    union hv_partition_processor_features *disabled_features)
{
    /* No processor features to disable on ARM */
}

int mshv_arch_post_init_vm(int vm_fd)
{
    return 0;
}

static void clamp_id_aa64mmfr0_parange_to_ipa_size(int mshv_fd,
                                                   ARMISARegisters *isar)
{
    uint32_t ipa_size = mshv_arm_get_ipa_bit_size(mshv_fd);
    uint64_t id_aa64mmfr0;

    /* Clamp down the PARange to the IPA size the kernel supports. */
    uint8_t index = round_down_to_parange_index(ipa_size);
    id_aa64mmfr0 = GET_IDREG(isar, ID_AA64MMFR0);
    id_aa64mmfr0 = FIELD_DP64(id_aa64mmfr0, ID_AA64MMFR0, PARANGE, index);
    SET_IDREG(isar, ID_AA64MMFR0, id_aa64mmfr0);
}

static int mshv_get_partition_regs(int vm_fd, hv_register_name *names,
                             hv_register_value *values, size_t n_regs)
{
    int ret = 0;
    size_t in_sz, names_sz, values_sz;
    void *in_buffer = qemu_memalign(HV_HYP_PAGE_SIZE, HV_HYP_PAGE_SIZE);
    void *out_buffer = qemu_memalign(HV_HYP_PAGE_SIZE, HV_HYP_PAGE_SIZE);
    hv_input_get_vp_registers *in = in_buffer;

    struct mshv_root_hvcall args = {0};

    names_sz = n_regs * sizeof(hv_register_name);
    in_sz = sizeof(hv_input_get_vp_registers) + names_sz;

    memset(in, 0, HV_HYP_PAGE_SIZE);

    in->vp_index = HV_ANY_VP;
    in->input_vtl.target_vtl = HV_VTL_ALL;
    in->input_vtl.use_target_vtl = 1;

    for (int i = 0; i < n_regs; i++) {
        in->names[i] = names[i];
    }

    values_sz = n_regs * sizeof(hv_register_value);

    args.code = HVCALL_GET_VP_REGISTERS;
    args.in_sz = in_sz;
    args.in_ptr = (uintptr_t)in_buffer;
    args.out_sz = values_sz;
    args.out_ptr = (uintptr_t)out_buffer;
    args.reps = n_regs;

    ret = mshv_hvcall(vm_fd, &args);

    if (ret == 0) {
        memcpy(values, out_buffer, values_sz);
    }

    qemu_vfree(in_buffer);
    qemu_vfree(out_buffer);

    return ret;
}

static bool mshv_arm_get_host_cpu_features(ARMHostCPUFeatures *ahcf)
{
    int mshv_fd = mshv_state->fd;
    int vm_fd = mshv_state->vm;
    int i, ret;
    bool success = true;
    uint64_t pfr0, pfr1;
    gchar *contents = NULL;

    static const struct {
        hv_register_name name;
        int isar_idx;
    } regs[] = {
        { HV_ARM64_REGISTER_ID_AA64_PFR0_EL1,  ID_AA64PFR0_EL1_IDX },
        { HV_ARM64_REGISTER_ID_AA64_PFR1_EL1,  ID_AA64PFR1_EL1_IDX },
        { HV_ARM64_REGISTER_ID_AA64_ISAR0_EL1, ID_AA64ISAR0_EL1_IDX },
        { HV_ARM64_REGISTER_ID_AA64_ISAR1_EL1, ID_AA64ISAR1_EL1_IDX },
        { HV_ARM64_REGISTER_ID_AA64_ISAR2_EL1, ID_AA64ISAR2_EL1_IDX },
        { HV_ARM64_REGISTER_ID_AA64_MMFR0_EL1, ID_AA64MMFR0_EL1_IDX },
        { HV_ARM64_REGISTER_ID_AA64_MMFR1_EL1, ID_AA64MMFR1_EL1_IDX },
        { HV_ARM64_REGISTER_ID_AA64_MMFR2_EL1, ID_AA64MMFR2_EL1_IDX },
        { HV_ARM64_REGISTER_ID_AA64_DFR0_EL1,  ID_AA64DFR0_EL1_IDX },
        { HV_ARM64_REGISTER_ID_AA64_DFR1_EL1,  ID_AA64DFR1_EL1_IDX },
    };

    size_t n_regs = ARRAY_SIZE(regs);
    hv_register_name *reg_names = g_new(hv_register_name, n_regs);
    hv_register_value *reg_values = g_new(hv_register_value, n_regs);

    for (i = 0; i < n_regs; i++) {
        reg_names[i] = regs[i].name;
    }

    ret = mshv_get_partition_regs(vm_fd, reg_names, reg_values, n_regs);

    if (ret < 0) {
        error_report("Failed to get host ID registers");
        success = false;
        goto out;
    }

    for (i = 0; i < n_regs; i++) {
        ahcf->isar.idregs[regs[i].isar_idx] = reg_values[i].reg64;
    }

    /* Read MIDR_EL1 from sysfs */
    if (g_file_get_contents(
            "/sys/devices/system/cpu/cpu0/regs/identification/midr_el1",
            &contents, NULL, NULL)) {
        ahcf->midr = g_ascii_strtoull(contents, NULL, 0);
    } else {
        error_report("Failed to read MIDR_EL1 from sysfs");
        success = false;
        goto out;
    }

    ahcf->dtb_compatible = "arm,armv8";
    ahcf->features = (1ULL << ARM_FEATURE_V8) |
                     (1ULL << ARM_FEATURE_AARCH64) |
                     (1ULL << ARM_FEATURE_PMU) |
                     (1ULL << ARM_FEATURE_GENERIC_TIMER) |
                     (1ULL << ARM_FEATURE_NEON);

    clamp_id_aa64mmfr0_parange_to_ipa_size(mshv_fd, &ahcf->isar);

    /*
     * SVE (Scalable Vector Extension) and SME (Scalable Matrix Extension)
     * require specific context switch logic in the accelerator.
     * Mask them out for now to ensure stability.
     */
    /* Mask SVE in PFR0 */
    pfr0 = GET_IDREG(&ahcf->isar, ID_AA64PFR0);
    pfr0 &= ~R_ID_AA64PFR0_SVE_MASK;
    SET_IDREG(&ahcf->isar, ID_AA64PFR0, pfr0);

    /* Mask SME in PFR1 */
    pfr1 = GET_IDREG(&ahcf->isar, ID_AA64PFR1);
    pfr1 &= ~R_ID_AA64PFR1_SME_MASK;
    SET_IDREG(&ahcf->isar, ID_AA64PFR1, pfr1);

out:
    g_free(contents);
    g_free(reg_names);
    g_free(reg_values);
    return success;
}

void mshv_arm_set_cpu_features_from_host(ARMCPU *cpu)
{
    if (!arm_host_cpu_features.dtb_compatible) {
        if (!mshv_enabled() ||
            !mshv_arm_get_host_cpu_features(&arm_host_cpu_features)) {
            /*
             * We can't report this error yet, so flag that we need to
             * in arm_cpu_realizefn().
             */
            cpu->host_cpu_probe_failed = true;
            return;
        }
    }

    cpu->dtb_compatible = arm_host_cpu_features.dtb_compatible;
    cpu->isar = arm_host_cpu_features.isar;
    cpu->env.features = arm_host_cpu_features.features;
    cpu->midr = arm_host_cpu_features.midr;
    cpu->reset_sctlr = arm_host_cpu_features.reset_sctlr;
}
