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
#include "qemu/memalign.h"
#include "cpu.h"
#include "hw/boards.h"
#include "target/arm/cpregs.h"
#include "target/arm/internals.h"
#include "qemu/lockable.h"
#include "system/mshv.h"
#include "system/mshv_int.h"
#include "hw/hyperv/hvgdk_mini.h"
#include "hw/hyperv/hvhdk_mini.h"
#include <sys/ioctl.h>
#include "system/cpus.h"
#include "target/arm/cpu.h"

#include "target/arm/mshv_arm.h"

#include "internals.h"

#define MAX_REGISTER_COUNT (MAX_CONST(ARRAY_SIZE(STANDARD_REGISTER_NAMES), \
                            MAX_CONST(ARRAY_SIZE(SPECIAL_REGISTER_NAMES), \
                                      ARRAY_SIZE(FPU_REGISTER_NAMES))))
// #define PSR_F_BIT  0x40
// #define PSR_I_BIT  0x80
// #define PSR_A_BIT  0x100
// #define PSR_D_BIT  0x200

#define PSTATE_FAULT_BITS_64 (PSR_MODE_EL1h | PSR_F_BIT | PSR_I_BIT | PSR_A_BIT | PSR_D_BIT)

static QemuMutex *cpu_guards_lock;
static GHashTable *cpu_guards;

/**
 * ARMHostCPUFeatures: information about the host CPU (identified
 * by asking the host kernel)
 */
typedef struct ARMHostCPUFeatures {
    ARMISARegisters isar;
    uint64_t features;
    uint64_t midr;
    uint32_t reset_sctlr;
    const char *dtb_compatible;
} ARMHostCPUFeatures;

static ARMHostCPUFeatures arm_host_cpu_features;

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

static int mshv_get_host_regs(int vm_fd, hv_register_name *names,
                             hv_register_value *values, size_t n_regs)
{
    int ret = 0;
    uint32_t vp_index = -1;
    size_t in_sz, names_sz, values_sz;
    size_t page = HV_HYP_PAGE_SIZE;
    void *in_buffer = qemu_memalign(page, page);
    void *out_buffer = qemu_memalign(page, page);
    hv_input_get_vp_registers *in = in_buffer;

    struct mshv_root_hvcall args = {0};

    names_sz = n_regs * sizeof(hv_register_name);
    in_sz = sizeof(hv_input_get_vp_registers) + names_sz;

    memset(in, 0, HV_HYP_PAGE_SIZE);

    in->partition_id = -1ULL; // Self partition
    in->vp_index = vp_index;
    in->input_vtl.as_uint8 = 0; // Standard VTL
    in->input_vtl.target_vtl = 0xf;
    in->input_vtl.use_target_vtl = 1;

    for (int i = 0; i < n_regs; i++) {
        in->names[i] = names[i];
    }

    values_sz = n_regs * sizeof(hv_register_value);

    args.code = HVCALL_GET_VP_REGISTERS;
    args.in_sz = in_sz;
    args.in_ptr = (uintptr_t)(in_buffer);
    args.out_sz = values_sz;
    args.out_ptr = (uintptr_t)(out_buffer);
    args.reps =  (uint16_t) n_regs;

    ret = mshv_hvcall(vm_fd, &args);


    if (ret == 0) {
        memcpy(values, out_buffer, values_sz);
        for(int i=0; i<n_regs; i++) {
            fprintf(stderr, "MSHV DEBUG: Reg 0x%x = 0x%llx\n",
                names[i], (unsigned long long)values[i].reg64);
        }
    }

    qemu_vfree(in_buffer);
    qemu_vfree(out_buffer);

    return ret;
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
    int ret;
    struct mshv_create_vp vp_arg = {
        .vp_index = vp_index,
    };

    ret = ioctl(vm_fd, MSHV_CREATE_VP, &vp_arg);
    if (ret < 0) {
        error_report("failed to create mshv vcpu: %s", strerror(errno));
        return -1;
    }

    *cpu_fd = ret;

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
    int ret;
    enum MshvVmExit exit_reason = MshvVmExitIgnore;
    int cpu_fd = mshv_vcpufd(cpu);

    ret = ioctl(cpu_fd, MSHV_RUN_VP, msg);
    if (ret < 0) {
        fprintf(stderr, "MSHV: ioctl error: %s\n", strerror(errno));
        return MshvVmExitShutdown;
    }

    switch (msg->header.message_type) {
    case HVMSG_UNMAPPED_GPA:
        
        ret = handle_unmapped_mem(vm_fd, cpu, msg, &exit_reason);
        if (ret < 0) {
            error_report("failed to handle unmapped memory");
            return -1;
        }
        return exit_reason;
    case HVMSG_ARM64_SYSREG_INTERCEPT:
        // ret = mshv_handle_psci(cpu, msg);
        error_report("failed to handle system register intercept\n");
        *exit = MshvVmExitIgnore;
        return -1;
    default:
        printf("MSHV: unhandled vcpu exit type: 0x%x\n",
               msg->header.message_type);
        *exit = MshvVmExitIgnore;
        return -1;
    }
    *exit = MshvVmExitIgnore;
    return 0;
}

void mshv_init_cpu_logic(void)
{
    cpu_guards_lock = g_new0(QemuMutex, 1);
    qemu_mutex_init(cpu_guards_lock);
    cpu_guards = g_hash_table_new(g_direct_hash, g_direct_equal);
}

void mshv_arch_init_vcpu(CPUState *cpu)
{
    AccelCPUState *state = cpu->accel;
    ARMCPU *arm_cpu = ARM_CPU(cpu);
    hv_register_assoc *assocs;
    size_t page = HV_HYP_PAGE_SIZE;
    void *mem = qemu_memalign(page, 2 * page);

    assocs = g_new0(hv_register_assoc, 3);
    assocs[0].name =  HV_ARM64_REGISTER_MIDR_EL1;
    assocs[0].value.reg64 = arm_cpu->midr;
    assocs[1].name = HV_ARM64_REGISTER_MPIDR_EL1;
    assocs[1].value.reg64 = deposit64(arm_cpu->mp_affinity, 31, 1, 1);
    assocs[2].name = HV_ARM64_REGISTER_PSTATE;
    assocs[2].value.reg64 = PSTATE_FAULT_BITS_64;

    state->hvcall_args.base = mem;
    state->hvcall_args.input_page = mem;
    state->hvcall_args.output_page = (uint8_t *)mem + page;

    mshv_set_generic_regs(cpu, assocs, 3);
}

void mshv_remove_vcpu(int vm_fd, int cpu_fd)
{
    return;
}

void mshv_arch_destroy_vcpu(CPUState *cpu)
{
    AccelCPUState *state = cpu->accel;

    if(state->hvcall_args.base) {
        qemu_vfree(state->hvcall_args.base);
    }

    state->hvcall_args = (MshvHvCallArgs){0};
}

uint32_t mshv_arm_get_ipa_bit_size(int mshv_fd)
{
    int ret;

    struct hv_input_get_partition_property in = {0};
    struct hv_output_get_partition_property out = {0};

    struct mshv_root_hvcall args = {0};

    in.partition_id = -1ULL; // Make this constant
    in.property_code = HV_PARTITION_PROPERTY_PHYSICAL_ADDRESS_WIDTH;
    
    args.code   = HVCALL_GET_PARTITION_PROPERTY;
    args.in_sz  = sizeof(in);
    args.in_ptr = (uint64_t)&in;
    args.out_sz = sizeof(out);
    args.out_ptr = (uint64_t)&out;

    ret = mshv_hvcall(mshv_fd, &args);

    if (ret < 0) {
        error_report("Failed to get IPA size");
        return -1;
    }

    return out.property_value;
}


static void clamp_id_aa64mmfr0_parange_to_ipa_size(int mshv_fd, ARMISARegisters *isar)
{
    uint32_t ipa_size = mshv_arm_get_ipa_bit_size(mshv_fd);
    uint64_t id_aa64mmfr0;

    /* Clamp down the PARange to the IPA size the kernel supports. */
    uint8_t index = round_down_to_parange_index(ipa_size);
    id_aa64mmfr0 = GET_IDREG(isar, ID_AA64MMFR0);
    id_aa64mmfr0 = (id_aa64mmfr0 & ~R_ID_AA64MMFR0_PARANGE_MASK) | index;
        SET_IDREG(isar, ID_AA64MMFR0, id_aa64mmfr0);
}

static bool mshv_arm_get_host_cpu_features(ARMHostCPUFeatures *ahcf)
{
    int mshv_fd;
    int vm_fd = mshv_state->vm;
    int i, ret;
    bool success = true;

    const struct {
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

    ret = init_mshv(&mshv_fd);

    if (ret < 0) {
        success = false;
        goto out;
    }

    for(i = 0; i < n_regs; i++){
        reg_names[i] = regs[i].name;
    }

    ret = mshv_get_host_regs(vm_fd, reg_names, reg_values, n_regs);

    if(ret < 0) {
        error_report("Failed to get host registers");
        success = false;
        goto out;
    }

    for(i = 0; i < n_regs; i++) {
        ahcf->isar.idregs[regs[i].isar_idx] = reg_values[i].reg64;
    }

    g_autofree gchar *contents = NULL;
    if (g_file_get_contents("/sys/devices/system/cpu/cpu0/regs/identification/midr_el1", 
                            &contents, NULL, NULL)) {
        printf("MSHV: Successfully read MIDR_EL1 from sysfs: %s\n", contents);
        ahcf->midr = g_ascii_strtoull(contents, NULL, 0);
        printf("MSHV: Read MIDR_EL1 from sysfs: 0x%lx\n", ahcf->midr);
    }
    else {
        error_report("Failed to read MIDR_EL1 from sysfs");
        success = false;
        goto out;
    }

    ahcf->dtb_compatible = "arm,armv8";
    ahcf->features = (1ULL << ARM_FEATURE_V8) |
                     (1ULL << ARM_FEATURE_AARCH64) |
                     (1ULL << ARM_FEATURE_PMU) |
                     (1ULL << ARM_FEATURE_GENERIC_TIMER);

    clamp_id_aa64mmfr0_parange_to_ipa_size(mshv_fd, &ahcf->isar);

    /* SVE (Scalable Vector Extension) and SME (Scalable Matrix Extension) 
     * require specific context switch logic in the accelerator. 
     * Mask them out for now to ensure stability.
     */
    /* Mask SVE in PFR0 */
    uint64_t pfr0 = GET_IDREG(&ahcf->isar, ID_AA64PFR0);
    pfr0 &= ~R_ID_AA64PFR0_SVE_MASK; 
    SET_IDREG(&ahcf->isar, ID_AA64PFR0, pfr0);

    /* Mask SME in PFR1 */
    uint64_t pfr1 = GET_IDREG(&ahcf->isar, ID_AA64PFR1);
    pfr1 &= ~R_ID_AA64PFR1_SME_MASK;
    SET_IDREG(&ahcf->isar, ID_AA64PFR1, pfr1);

out: 
    g_free(reg_names);
    g_free(reg_values);
    close(mshv_fd);
    return success;
}

void mshv_arm_set_cpu_features_from_host(ARMCPU *cpu)
{
    if (!arm_host_cpu_features.dtb_compatible) {
        if (!mshv_enabled() ||
            !mshv_arm_get_host_cpu_features(&arm_host_cpu_features)) {
            /* We can't report this error yet, so flag that we need to
             * in arm_cpu_realizefn().
             */
            cpu->host_cpu_probe_failed = true;
            printf("MSHV: Failed to get host CPU features\n");
            return;
        }
    }

    cpu->dtb_compatible = arm_host_cpu_features.dtb_compatible;
    cpu->isar = arm_host_cpu_features.isar;
    cpu->env.features = arm_host_cpu_features.features;
    cpu->midr = arm_host_cpu_features.midr;
    cpu->reset_sctlr = arm_host_cpu_features.reset_sctlr;
    printf("MSHV: Successfully set CPU features from host\n");
}
