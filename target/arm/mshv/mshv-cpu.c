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

#include "system/mshv.h"
#include "system/mshv_int.h"


int mshv_set_generic_regs(const CPUState *cpu, const hv_register_assoc *assocs,
                          size_t n_regs)
{
    return 0;
}

int mshv_load_regs(CPUState *cpu)
{
    return 0;
}

int mshv_arch_put_registers(const CPUState *cpu)
{
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