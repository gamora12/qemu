/*
 * QEMU MSHV Support
 *
 * Copyright Microsoft, Corp 2025
 *
 * Authors:
 *  Aastha Rawat          <aastharawat@microsoft.com>
 *  Anirudh Rayabharam    <anrayabh@microsoft.com>
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
*/

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qemu/lockable.h"
#include "system/mshv.h"
#include "system/mshv_int.h"
#include "hw/hyperv/hvgdk_mini.h"
#include <sys/ioctl.h>
#include "system/cpus.h"
#include "target/arm/cpu.h"



static enum hv_register_name STANDARD_REGISTER_NAMES[32] = {
    HV_ARM64_REGISTER_X0,
	HV_ARM64_REGISTER_X1,
	HV_ARM64_REGISTER_X2,
	HV_ARM64_REGISTER_X3,
	HV_ARM64_REGISTER_X4,
	HV_ARM64_REGISTER_X5,
	HV_ARM64_REGISTER_X6,
	HV_ARM64_REGISTER_X7,
	HV_ARM64_REGISTER_X8,
	HV_ARM64_REGISTER_X9,
	HV_ARM64_REGISTER_X10,
	HV_ARM64_REGISTER_X11,
	HV_ARM64_REGISTER_X12,
	HV_ARM64_REGISTER_X13,
	HV_ARM64_REGISTER_X14,
	HV_ARM64_REGISTER_X15,
	HV_ARM64_REGISTER_X16,
	HV_ARM64_REGISTER_X17,
	HV_ARM64_REGISTER_X18,
	HV_ARM64_REGISTER_X19,
	HV_ARM64_REGISTER_X20,
	HV_ARM64_REGISTER_X21,
	HV_ARM64_REGISTER_X22,
	HV_ARM64_REGISTER_X23,
	HV_ARM64_REGISTER_X24,
	HV_ARM64_REGISTER_X25,
	HV_ARM64_REGISTER_X26,
	HV_ARM64_REGISTER_X27,
	HV_ARM64_REGISTER_X28,
	HV_ARM64_REGISTER_FP,
	HV_ARM64_REGISTER_LR,
	HV_ARM64_REGISTER_PC,
};

int mshv_set_generic_regs(const CPUState *cpu, const hv_register_assoc *assocs,
                          size_t n_regs)
{
    int cpu_fd = mshv_vcpufd(cpu);
    int vp_index = cpu->cpu_index;
    size_t in_sz, assocs_sz;
    hv_input_set_vp_registers *in = cpu->accel->hvcall_args.input_page;
    struct mshv_root_hvcall args = {0};
    int ret;

    /* find out the size of the struct w/ a flexible array at the tail */
    assocs_sz = n_regs * sizeof(hv_register_assoc);
    in_sz = sizeof(hv_input_set_vp_registers) + assocs_sz;

    /* fill the input struct */
    memset(in, 0, sizeof(hv_input_set_vp_registers));
    in->vp_index = vp_index;
    memcpy(in->elements, assocs, assocs_sz);

    /* create the hvcall envelope */
    args.code = HVCALL_SET_VP_REGISTERS;
    args.in_sz = in_sz;
    args.in_ptr = (uint64_t) in;
    args.reps = (uint16_t) n_regs;

    /* perform the call */
    ret = mshv_hvcall(cpu_fd, &args);
    if (ret < 0) {
        error_report("Failed to set registers");
        return -1;
    }

    /* assert we set all registers */
    if (args.reps != n_regs) {
        error_report("Failed to set registers: expected %zu elements"
                     ", got %u", n_regs, args.reps);
        return -1;
    }

    return 0;
}

static int set_standard_regs(const CPUState *cpu)
{
    size_t n_regs = ARRAY_SIZE(STANDARD_REGISTER_NAMES);
    struct hv_register_assoc *assocs;
    int ret;
    ARMCPU *arm_cpu = ARM_CPU(cpu);
    CPUARMState *env = &arm_cpu->env;

    assocs = g_new0(hv_register_assoc, n_regs);

    for(size_t i = 0; i < n_regs - 1; i++) {
        assocs[i].name = STANDARD_REGISTER_NAMES[i];
        assocs[i].value.reg64 = env->xregs[i];
    }

    assocs[31].name = STANDARD_REGISTER_NAMES[31];
    assocs[31].value.reg64 = env->pc;

    ret = mshv_set_generic_regs(cpu, assocs, n_regs);
    if(ret < 0) {
        error_report("failed to set standard registers");
        g_free(assocs);
        return -1;
    }

    g_free(assocs);

    return 0;
}
static int get_generic_regs(CPUState *cpu, hv_register_assoc *assocs,
                            size_t n_regs)
{
    int cpu_fd = mshv_vcpufd(cpu);
    int vp_index = cpu->cpu_index;
    hv_input_get_vp_registers *in = cpu->accel->hvcall_args.input_page;
    hv_register_value *values = cpu->accel->hvcall_args.output_page;
    size_t in_sz, names_sz, values_sz;
    int i, ret;
    struct mshv_root_hvcall args = {0};

    /* find out the size of the struct w/ a flexible array at the tail */
    names_sz = n_regs * sizeof(hv_register_name);
    in_sz = sizeof(hv_input_get_vp_registers) + names_sz;

    /* fill the input struct */
    memset(in, 0, sizeof(hv_input_get_vp_registers));
    in->vp_index = vp_index;
    for (i = 0; i < n_regs; i++) {
        in->names[i] = assocs[i].name;
    }

    /* determine size of value output buffer */
    values_sz = n_regs * sizeof(union hv_register_value);

    /* create the hvcall envelope */
    args.code = HVCALL_GET_VP_REGISTERS;
    args.in_sz = in_sz;
    args.in_ptr = (uint64_t) in;
    args.out_sz = values_sz;
    args.out_ptr = (uint64_t) values;
    args.reps = (uint16_t) n_regs;

    /* perform the call */
    ret = mshv_hvcall(cpu_fd, &args);
    if (ret < 0) {
        error_report("Failed to retrieve registers");
        return -1;
    }

    /* assert we got all registers */
    if (args.reps != n_regs) {
        error_report("Failed to retrieve registers: expected %zu elements"
                     ", got %u", n_regs, args.reps);
        return -1;
    }

    /* copy values into assoc */
    for (i = 0; i < n_regs; i++) {
        assocs[i].value = values[i];
    }

    return 0;
}

static void populate_standard_regs(const hv_register_assoc *assocs,
                                   CPUARMState *env)
{
    size_t n_regs = ARRAY_SIZE(STANDARD_REGISTER_NAMES);
    for (size_t i = 0; i < n_regs - 1; i++) {
        env->xregs[i] = assocs[i].value.reg64;
    }
    env->pc = assocs[31].value.reg64;
}

int mshv_get_standard_regs(CPUState *cpu)
{
    size_t n_regs = ARRAY_SIZE(STANDARD_REGISTER_NAMES);
    struct hv_register_assoc *assocs;
    int ret;
    ARMCPU *arm_cpu = ARM_CPU(cpu);
    CPUARMState *env = &arm_cpu->env;

    assocs = g_new0(hv_register_assoc, n_regs);
    for(size_t i = 0; i < n_regs; i++) {
        assocs[i].name = STANDARD_REGISTER_NAMES[i];
    }
    ret = get_generic_regs(cpu, assocs, n_regs);
    if (ret < 0) {
        error_report("failed to get standard registers");
        g_free(assocs);
        return -1;
    }

    populate_standard_regs(assocs, env);

    g_free(assocs);
    return 0;
}

int mshv_load_regs(CPUState *cpu)
{
    int ret;

    ret = mshv_get_standard_regs(cpu);
    if(ret < 0) {
        error_report("Failed to load standard registers");
        return -1;
    }

    return 0;
}

static int set_cpu_state(const CPUState *cpu)
{
    int ret;

    ret = set_standard_regs(cpu);
    if (ret < 0) {
        return ret;
    }

    return 0;
}

static int mshv_configure_arm_vcpu(const CPUState *cpu)
{
    int ret;

    ret = set_cpu_state(cpu);
    if (ret < 0) {
        error_report("Failed to set up CPU state");
        return -1;
    }

    return 0;
}

static int put_regs(const CPUState *cpu)
{
    int ret;
    ret = mshv_configure_arm_vcpu(cpu);
    if(ret < 0) {
        error_report("failed to configure vcpu");
        return ret;
    }

    return 0;
}

int mshv_arch_put_registers(const CPUState *cpu)
{
    int ret;

    ret = put_regs(cpu);
    if (ret < 0) {
        error_report("Failed to put registers");
        return -1;
    }

    return 0;
}

int mshv_create_vcpu(int vm_fd, uint8_t vp_index, int *cpu_fd)
{
    return 0;
}

int mshv_run_vcpu(int vm_fd, CPUState *cpu, hv_message *msg, MshvVmExit *exit)
{
    return 0;
}

void mshv_arch_init_vcpu(CPUState *cpu)
{
    return;
}

void mshv_remove_vcpu(int vm_fd, int cpu_fd)
{
    return;
}

void mshv_arch_destroy_vcpu(CPUState *cpu)
{
    return;
}

void mshv_init_mmio_emu(void)
{
    return;
}

void mshv_arch_amend_proc_features(
    union hv_partition_synthetic_processor_features *features)
{
    return;
}

int mshv_arch_post_init_vm(int vm_fd)
{
    return 0;
}