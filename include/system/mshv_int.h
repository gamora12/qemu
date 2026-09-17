/*
 * QEMU MSHV support
 *
 * Copyright Microsoft, Corp. 2025
 *
 * Authors: Ziqiao Zhou  <ziqiaozhou@microsoft.com>
 *          Magnus Kulke <magnuskulke@microsoft.com>
 *          Jinank Jain  <jinankjain@microsoft.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 */

#ifndef QEMU_MSHV_INT_H
#define QEMU_MSHV_INT_H

#include "hw/hyperv/hvhdk.h"

#define MSHV_MSR_ENTRIES_COUNT 64

/*
 * Interruption-type encoding, used by the hypervisor in
 * hv_x64_pending_interruption_register.interruption_type
 * See TLFS 6.0 section 7.9.2, p55
 * https://learn.microsoft.com/en-us/virtualization/hyper-v-on-windows/tlfs/tlfs
 */
#define MSHV_HV_INTERRUPTION_TYPE_EXT_INT     0
#define MSHV_HV_INTERRUPTION_TYPE_NMI         2
#define MSHV_HV_INTERRUPTION_TYPE_HW_EXC      3
#define MSHV_HV_INTERRUPTION_TYPE_SW_INT      4
#define MSHV_HV_INTERRUPTION_TYPE_PRIV_SW_EXC 5
#define MSHV_HV_INTERRUPTION_TYPE_SW_EXC      6

#define MSHV_DIRTY_PAGES_BATCH_SIZE 0x10000

typedef struct hyperv_message hv_message;

typedef struct MshvHvCallArgs {
    void *base;
    void *input_page;
    void *output_page;
} MshvHvCallArgs;

struct AccelCPUState {
    int cpufd;
    MshvHvCallArgs hvcall_args;
    /*
     * MP state, as carried by HV_REGISTER_INTERNAL_ACTIVITY_STATE. Sampled
     * from the hypervisor by load_mp_state(), migrated by vmstate_mshv_mp_state
     * and written back by store_mp_state(). The restore is gated on
     * mp_state_restore_pending so that a cold boot never applies a zeroed
     * value, which would clear StartupSuspend and start every AP early.
     */
    uint64_t mp_state_activity;
    bool mp_state_restore_pending;
    bool arm_timer_restore_pending;
    /*
     * Guest virtual counter (CNTVCT_EL0), captured on the source by
     * mshv_arm_timer_pre_save() and migrated by vmstate_mshv_arm_counter.
     *
     * CNTV_CVAL_EL0 is an *absolute* deadline expressed in the source
     * partition's counter timebase, so it is only meaningful alongside the
     * counter it was measured against. Each partition's virtual counter starts
     * near zero at creation, so without this the destination counter jumps
     * backwards to its own (much smaller) uptime: the migrated deadline then
     * sits far in the future and the guest's virtual timer stalls for exactly
     * as long as the source VM had been running.
     *
     * arm_timer_cntvct_valid says the value was actually read on the source and
     * travels with it in the migration stream; _restore_pending says a
     * migration delivered a valid one and it has not been applied yet. Both are
     * required so that a cold boot never writes a zeroed counter, and so a
     * source that could not sample the counter leaves the destination's own
     * counter alone instead of resetting it to 0.
     */
    uint64_t arm_timer_cntvct;
    bool arm_timer_cntvct_valid;
    bool arm_timer_cntvct_restore_pending;
    /*
     * Post-dispatch timer probe. Every readback so far happened before the VP
     * was ever dispatched, which cannot tell "the SET was lost on the first
     * context load" apart from "the SET stuck but PPI 27 is never asserted".
     * Sample the same registers again after MSHV_RUN_VP has returned at least
     * once and compare. Diagnostic only.
     */
    unsigned arm_timer_probe_runs;
    bool arm_timer_probe_armed;
    /* Destination-only dispatch accounting, armed at migration restore. */
    bool dst_trace;
    unsigned dst_run_ok;
    unsigned dst_run_eintr;
    unsigned dst_mmio;
    uint64_t arm_timer_probe_ctl;
    uint64_t arm_timer_probe_cval;
};

typedef struct MshvMemoryListener {
    MemoryListener listener;
    int as_id;
} MshvMemoryListener;

typedef struct MshvAddressSpace {
    MshvMemoryListener *ml;
    AddressSpace *as;
} MshvAddressSpace;

struct MshvState {
    AccelState parent_obj;
    int vm;
    MshvMemoryListener memory_listener;
    /* number of listeners */
    int nr_as;
    MshvAddressSpace *as;
    int fd;
    /* irqchip routing */
    struct mshv_user_irq_table *irq_routes;
    int nr_allocated_irq_routes;
    unsigned long *used_gsi_bitmap;
    unsigned int gsi_count;
    union hv_partition_processor_features processor_features;
};

typedef struct MshvMsiControl {
    bool updated;
    GHashTable *gsi_routes;
} MshvMsiControl;

#define mshv_vcpufd(cpu) (cpu->accel->cpufd)

/* cpu */

typedef enum MshvVmExit {
    MshvVmExitIgnore   = 0,
    MshvVmExitShutdown = 1,
    MshvVmExitSpecial  = 2,
} MshvVmExit;

void mshv_init_mmio_emu(void);
int mshv_create_vcpu(int vm_fd, uint8_t vp_index, int *cpu_fd);
void mshv_remove_vcpu(int vm_fd, int cpu_fd);
int mshv_configure_vcpu(const CPUState *cpu);
int mshv_run_vcpu(int vm_fd, CPUState *cpu, hv_message *msg, MshvVmExit *exit);
int mshv_set_generic_regs(const CPUState *cpu, const hv_register_assoc *assocs,
                          size_t n_regs);
int mshv_get_generic_regs(CPUState *cpu, hv_register_assoc *assocs,
                          size_t n_regs);
int mshv_arch_store_vcpu_state(const CPUState *cpu);
int mshv_arch_load_vcpu_state(CPUState *cpu);
int mshv_get_vp_state(const CPUState *cpu, uint8_t type, void *buf,
                      size_t buf_sz);
int mshv_set_vp_state(const CPUState *cpu, uint8_t type, const void *buf,
                      size_t buf_sz);
void mshv_arch_init_vcpu(CPUState *cpu);
void mshv_arch_destroy_vcpu(CPUState *cpu);
int mshv_gic_rearm_timer_ppi(const CPUState *cpu);
void mshv_gic_dump_timer_ppi(const CPUState *cpu, const char *side);
void mshv_arch_amend_proc_features(
    union hv_partition_synthetic_processor_features *features);
void mshv_arch_disable_partition_proc_features(
     union hv_partition_processor_features *disabled_features);
int mshv_arch_accel_init(AccelState *as, MachineState *ms, int mshv_fd);
int mshv_arch_pre_init_vm(int vm_fd);
int mshv_arch_post_init_vm(int vm_fd);
void mshv_setup_hvcall_args(AccelCPUState *state);

typedef struct mshv_root_hvcall mshv_root_hvcall;
int mshv_hvcall(int fd, mshv_root_hvcall *args);

/* memory */
typedef struct MshvMemoryRegion {
    uint64_t guest_phys_addr;
    uint64_t memory_size;
    uint64_t userspace_addr;
    bool readonly;
} MshvMemoryRegion;

int mshv_guest_mem_read(uint64_t gpa, uint8_t *data, uintptr_t size,
                        bool is_secure_mode, bool instruction_fetch);
int mshv_guest_mem_write(uint64_t gpa, const uint8_t *data, uintptr_t size,
                         bool is_secure_mode);
void mshv_set_phys_mem(MshvMemoryListener *mml, MemoryRegionSection *section,
                       bool add);
void mshv_log_sync(MemoryListener *listener, MemoryRegionSection *section);
bool mshv_log_global_start(MemoryListener *listener, Error **errp);
void mshv_log_global_stop(MemoryListener *listener);

/* msr */
int mshv_init_msrs(const CPUState *cpu);
int mshv_get_msrs(CPUState *cpu);
int mshv_set_msrs(const CPUState *cpu);

#endif
