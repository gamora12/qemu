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
 * CNTVOFF_EL2 is intentionally omitted: it is a hypervisor-owned EL2 register
 * that MSHV manages internally and rejects on HVCALL_SET_VP_REGISTERS (EINVAL).
 * KVM likewise does not sync it as a per-vCPU register.
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
 * MSHV has no explicit power-state register. Hyper-V models the PSCI power
 * state of a VP by holding it in "explicit suspend": a powered-off VP (PSCI
 * CPU_OFF, or a secondary core prior to CPU_ON) has EXPLICIT_SUSPEND.suspended
 * set, while a running VP has it cleared. This mirrors KVM's mp_state
 * (KVM_MP_STATE_STOPPED vs KVM_MP_STATE_RUNNABLE) sync.
 */
static int store_mp_state(const CPUState *cpu)
{
    ARMCPU *arm_cpu = ARM_CPU(cpu);
    int ret;

    if (arm_cpu->power_state == PSCI_OFF) {
        struct hv_register_assoc assoc = {
            .name = HV_REGISTER_EXPLICIT_SUSPEND,
        };
        assoc.value.explicit_suspend.suspended = 1;

        ret = mshv_set_generic_regs(cpu, &assoc, 1);
    } else {
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
    }

    if (ret < 0) {
        error_report("failed to set mp state");
        return -1;
    }

    return 0;
}

static int load_mp_state(CPUState *cpu)
{
    ARMCPU *arm_cpu = ARM_CPU(cpu);
    struct hv_register_assoc assoc = {
        .name = HV_REGISTER_EXPLICIT_SUSPEND,
    };
    int ret;

    ret = mshv_get_generic_regs(cpu, &assoc, 1);
    if (ret < 0) {
        error_report("failed to get mp state");
        return -1;
    }

    arm_set_cpu_power_state(arm_cpu,
        assoc.value.explicit_suspend.suspended ? PSCI_OFF : PSCI_ON);

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

    ret = load_mp_state(cpu);
    if (ret < 0) {
        error_report("Failed to load mp state");
        return -1;
    }

    return 0;
}

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

    ret = load_regs(cpu);
    if (ret < 0) {
        error_report("Failed to load registers");
        return -1;
    }

    static const struct arm_emul_ops mshv_arm_emul_ops = {
        .get_reg = mshv_mmio_get_reg,
        .set_reg = mshv_mmio_set_reg,
    };

    ret = arm_emulate_mmio(cpu, syndrome, info.guest_physical_address,
                           &mshv_arm_emul_ops);
    if (ret < 0) {
        error_report("Failed to emulate with syndrome");
        return -1;
    }

    /* Advance PC past the faulting instruction */
    env->pc += (syndrome.il == 1) ? 4 : 2;

    ret = store_regs(cpu);
    if (ret < 0) {
        error_report("Failed to store registers");
        return -1;
    }

    *exit_reason = MshvVmExitIgnore;

    return 0;
}

int mshv_run_vcpu(int vm_fd, CPUState *cpu, hv_message *msg, MshvVmExit *exit)
{
    int ret;
    int cpu_fd = mshv_vcpufd(cpu);

    ret = ioctl(cpu_fd, MSHV_RUN_VP, msg);
    if (ret < 0) {
        *exit = MshvVmExitShutdown;
        return ret;
    }

    switch (msg->header.message_type) {
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
}

void mshv_arch_destroy_vcpu(CPUState *cpu)
{
    AccelCPUState *state = cpu->accel;

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
