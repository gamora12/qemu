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

typedef union {
    uint64_t raw;
    struct {
        uint32_t iss: 25;
        uint32_t il: 1;
        uint32_t ec: 6;
        uint32_t iss2: 5;
        uint32_t _rsvd: 27;
    } __attribute__((packed));
} EsrEl2;

typedef union {
    uint32_t raw;
    struct {
        uint32_t dfsc: 6;
        uint32_t wnr: 1;
        uint32_t s1ptw: 1;
        uint32_t cm: 1;
        uint32_t ea: 1;
        uint32_t fnv: 1;
        uint32_t set: 2;
        uint32_t vncr: 1;
        uint32_t ar: 1;
        uint32_t sf: 1;
        uint32_t srt: 5;
        uint32_t sse: 1;
        uint32_t sas: 2;
        uint32_t isv: 1;
        uint32_t _unused: 7;
    } __attribute__((packed));
} IssDataAbort;

typedef enum {
    data_abort_lower = 36,
    data_abort = 37,
} ExceptionClass;

int mshv_store_regs(CPUState *cpu)
{
    int ret;

    ret = set_standard_regs(cpu);
    if (ret < 0) {
        error_report("Failed to store standard registers");
        return -1;
    }

    /* TODO: should store special registers? the equivalent hvf code doesn't */

    return 0;
}

static int emulate_with_syndrome(CPUState *cpu,
                                 struct hv_arm64_memory_intercept_message *info)
{
    ARMCPU *arm_cpu = ARM_CPU(cpu);
    CPUARMState *env = &arm_cpu->env;
    int ret;
    EsrEl2 syndrome = { 0 };
    syndrome.raw = info->syndrome;

    uint8_t ec = syndrome.ec;
    uint64_t gpa = info->guest_physical_address;

    if (!(ec == data_abort_lower || ec == data_abort)) {
        error_report("Unknown exception class 0x%x\n", ec);
        return -1;
    }

    IssDataAbort iss = { 0 };
    iss.raw = syndrome.iss;
    if (!iss.isv) {
        error_report("Invalid ESR EL2 ISV field\n");
        return -1;
    }

    uint64_t len = 1ULL << iss.sas;
    bool sign_extend = iss.sse;
    uint64_t reg_index = iss.srt;

    // Load the regs from MSHV
    ret = mshv_load_regs(cpu);
    if (ret < 0) {
        error_report("Failed to load registers");
        return -1;
    }

    if (iss.wnr) {
        uint8_t data[8];
        uint64_t val = reg_index < 31 ? env->xregs[reg_index] : 0ULL;
        val = cpu_to_le64(val);

        memcpy(data, &val, sizeof(val));
        ret = mshv_guest_mem_write(gpa, data, len, false);
        if (ret < 0) {
            error_report("Failed to write guest memory");
            return -1;
        }
    } else {
        uint8_t data[8] = { 0 };
        ret = mshv_guest_mem_read(gpa, data, len, false, false);
        if (ret < 0) {
            error_report("Failed to read guest memory");
            return -1;
        }

        uint64_t val;
        memcpy(&val, data, sizeof(val));

        val = le64_to_cpu(val);

        if (sign_extend) {
            uint64_t shift = 64 - (len * 8);
            val = (((int64_t)val << shift) >> shift);
            if (!iss.sf) {
                val &= 0xffffffff;
            }
        }

        env->xregs[reg_index] = val;
    }

    env->pc += (syndrome.il == 1) ? 4 : 2;

    ret = mshv_store_regs(cpu);
    if (ret < 0) {
        error_report("failed to store registers");
        return -1;
    }

    return 0;
}

static int handle_unmapped_mem(int vm_fd, CPUState *cpu,
                               const struct hyperv_message *msg,
                               MshvVmExit *exit_reason)
{
    struct hv_arm64_memory_intercept_message info = { 0 };
    int ret;

    ret = set_memory_info(msg, &info);
    if (ret < 0) {
        error_report("failed to convert message to memory info");
        return -1;
    }

    ret = emulate_with_syndrome(cpu, &info);
    if (ret < 0) {
        error_report("Failed to emulate with syndrome");
        return -1;
    }

    return 0;
}

int mshv_run_vcpu(int vm_fd, CPUState *cpu, hv_message *msg, MshvVmExit *exit)
{
    handle_unmapped_mem(0, NULL, NULL, NULL);
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