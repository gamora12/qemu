/*
 * Type definitions for the mshv host.
 *
 * Copyright Microsoft, Corp. 2025
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_HYPERV_HVHDK_H
#define HW_HYPERV_HVHDK_H

#define HV_PARTITION_SYNTHETIC_PROCESSOR_FEATURES_BANKS 1

struct hv_input_set_partition_property {
    uint64_t partition_id;
    uint32_t property_code; /* enum hv_partition_property_code */
    uint32_t padding;
    uint64_t property_value;
};

struct hv_input_get_partition_property {
	uint64_t partition_id;
	uint32_t property_code; /* enum hv_partition_property_code */
	uint32_t padding;
};

struct hv_output_get_partition_property {
	uint64_t property_value;
};


union hv_partition_synthetic_processor_features {
    uint64_t as_uint64[HV_PARTITION_SYNTHETIC_PROCESSOR_FEATURES_BANKS];

    struct {
        /*
         * Report a hypervisor is present. CPUID leaves
         * 0x40000000 and 0x40000001 are supported.
         */
        uint64_t hypervisor_present:1;

        /*
         * Features associated with HV#1:
         */

        /* Report support for Hv1 (CPUID leaves 0x40000000 - 0x40000006). */
        uint64_t hv1:1;

        /*
         * Access to HV_X64_MSR_VP_RUNTIME.
         * Corresponds to access_vp_run_time_reg privilege.
         */
        uint64_t access_vp_run_time_reg:1;

        /*
         * Access to HV_X64_MSR_TIME_REF_COUNT.
         * Corresponds to access_partition_reference_counter privilege.
         */
        uint64_t access_partition_reference_counter:1;

        /*
         * Access to SINT-related registers (HV_X64_MSR_SCONTROL through
         * HV_X64_MSR_EOM and HV_X64_MSR_SINT0 through HV_X64_MSR_SINT15).
         * Corresponds to access_synic_regs privilege.
         */
        uint64_t access_synic_regs:1;

        /*
         * Access to synthetic timers and associated MSRs
         * (HV_X64_MSR_STIMER0_CONFIG through HV_X64_MSR_STIMER3_COUNT).
         * Corresponds to access_synthetic_timer_regs privilege.
         */
        uint64_t access_synthetic_timer_regs:1;

        /*
         * Access to APIC MSRs (HV_X64_MSR_EOI, HV_X64_MSR_ICR and
         * HV_X64_MSR_TPR) as well as the VP assist page.
         * Corresponds to access_intr_ctrl_regs privilege.
         */
        uint64_t access_intr_ctrl_regs:1;

        /*
         * Access to registers associated with hypercalls
         * (HV_X64_MSR_GUEST_OS_ID and HV_X64_MSR_HYPERCALL).
         * Corresponds to access_hypercall_msrs privilege.
         */
        uint64_t access_hypercall_regs:1;

        /* VP index can be queried. corresponds to access_vp_index privilege. */
        uint64_t access_vp_index:1;

        /*
         * Access to the reference TSC. Corresponds to
         * access_partition_reference_tsc privilege.
         */
        uint64_t access_partition_reference_tsc:1;

        /*
         * Partition has access to the guest idle reg. Corresponds to
         * access_guest_idle_reg privilege.
         */
        uint64_t access_guest_idle_reg:1;

        /*
         * Partition has access to frequency regs. corresponds to
         * access_frequency_regs privilege.
         */
        uint64_t access_frequency_regs:1;

        uint64_t reserved_z12:1; /* Reserved for access_reenlightenment_controls */
        uint64_t reserved_z13:1; /* Reserved for access_root_scheduler_reg */
        uint64_t reserved_z14:1; /* Reserved for access_tsc_invariant_controls */

        /*
         * Extended GVA ranges for HvCallFlushVirtualAddressList hypercall.
         * Corresponds to privilege.
         */
        uint64_t enable_extended_gva_ranges_for_flush_virtual_address_list:1;

        uint64_t reserved_z16:1; /* Reserved for access_vsm. */
        uint64_t reserved_z17:1; /* Reserved for access_vp_registers. */

        /* Use fast hypercall output. Corresponds to privilege. */
        uint64_t fast_hypercall_output:1;

        uint64_t reserved_z19:1; /* Reserved for enable_extended_hypercalls. */

        /*
         * HvStartVirtualProcessor can be used to start virtual processors.
         * Corresponds to privilege.
         */
        uint64_t start_virtual_processor:1;

        uint64_t reserved_z21:1; /* Reserved for Isolation. */

        /* Synthetic timers in direct mode. */
        uint64_t direct_synthetic_timers:1;

        uint64_t reserved_z23:1; /* Reserved for synthetic time unhalted timer */

        /* Use extended processor masks. */
        uint64_t extended_processor_masks:1;

        /*
         * HvCallFlushVirtualAddressSpace / HvCallFlushVirtualAddressList are
         * supported.
         */
        uint64_t tb_flush_hypercalls:1;

        /* HvCallSendSyntheticClusterIpi is supported. */
        uint64_t synthetic_cluster_ipi:1;

        /* HvCallNotifyLongSpinWait is supported. */
        uint64_t notify_long_spin_wait:1;

        /* HvCallQueryNumaDistance is supported. */
        uint64_t query_numa_distance:1;

        /* HvCallSignalEvent is supported. Corresponds to privilege. */
        uint64_t signal_events:1;

        /* HvCallRetargetDeviceInterrupt is supported. */
        uint64_t retarget_device_interrupt:1;

#if defined(__x86_64__)
		/* HvCallRestorePartitionTime is supported. */
		uint64_t restore_time:1;

		/* EnlightenedVmcs nested enlightenment is supported. */
		uint64_t enlightened_vmcs:1;

		uint64_t nested_debug_ctl:1;
		uint64_t synthetic_time_unhalted_timer:1;
		uint64_t idle_spec_ctrl:1;

#else
		uint64_t reserved_z31:1;
		uint64_t reserved_z32:1;
		uint64_t reserved_z33:1;
		uint64_t reserved_z34:1;
		uint64_t reserved_z35:1;
#endif

#if defined(__aarch64__)
		/* Register intercepts supported in V1. As more registers are supported in future
		 * releases, new bits will be added here to prevent migration between incompatible hosts.
		 *
		 * List of registers supported in V1:
		 * 1. TPIDRRO_EL0
		 * 2. TPIDR_EL1
		 * 3. SCTLR_EL1 - Supports write intercept mask.
		 * 4. VBAR_EL1
		 * 5. TCR_EL1 - Supports write intercept mask.
		 * 6. MAIR_EL1 - Supports write intercept mask.
		 * 7. CPACR_EL1 - Supports write intercept mask.
		 * 8. CONTEXTIDR_EL1
		 * 9. PAuth keys (total 10 registers)
		 * 10. HvArm64RegisterSyntheticException
		 */
		uint64_t register_intercepts_v1:1;
#else
		uint64_t reserved_z36:1;
#endif

		/* HvCallWakeVps is supported */
		uint64_t wake_vps:1;

		/* HvCallGet/SetVpRegisters is supported.
		 * Corresponds to AccessVpRegisters privilege.
		 * This feature only affects exo partitions.
		 */
		uint64_t access_vp_regs:1;

#if defined(__aarch64__)
		/* HvCallSyncContext/Ex is supported. */
		uint64_t sync_context:1;
#else
		uint64_t reserved_z39:1;
#endif /* __aarch64__ */

		/* Management VTL synic support is allowed.
		 * Corresponds to the ManagementVtlSynicSupport privilege.
		 */
		uint64_t management_vtl_synic_support:1;

#if defined (__x86_64__)
		/* Hypervisor supports guest mechanism to signal pending interrupts to paravisor. */
		uint64_t proxy_interrupt_doorbell_support:1;
#else
		uint64_t reserved_z41:1;
#endif

#if defined(__aarch64__)
		/* InterceptSystemResetAvailable is exposed. */
		uint64_t intercept_system_reset:1;
#else
		uint64_t reserved_z42:1;
#endif

		/* Hypercalls for host MMIO operations are available. */
		uint64_t mmio_hypercalls:1;

		uint64_t reserved:20;
	};
};

#define HV_PARTITION_PROCESSOR_FEATURES_BANKS 2
#define MSHV_NUM_CPU_FEATURES_BANKS 2

union hv_partition_processor_features {
	uint64_t as_uint64[HV_PARTITION_PROCESSOR_FEATURES_BANKS];
#if defined(__aarch64__)
	struct {
		uint64_t asid16 : 1;
		uint64_t t_gran16 : 1;
		uint64_t t_gran64 : 1;
		uint64_t haf : 1;
		uint64_t hdbs : 1;
		uint64_t pan : 1;
		uint64_t at_s1e1 : 1;
		uint64_t uao : 1;
		uint64_t el0_aarch32 : 1;
		uint64_t fp : 1;
		uint64_t fp_hp : 1;
		uint64_t adv_simd : 1;
		uint64_t adv_simd_hp : 1;
		uint64_t gic_v3v4 : 1;
		uint64_t gic_v4p1 : 1;
		uint64_t ras : 1; // Not supported
		uint64_t pmu_v3 : 1;
		uint64_t pmu_v3_arm_v81 : 1;
		uint64_t pmu_v3_arm_v84 : 1; // Not supported
		uint64_t pmu_v3_arm_v85 : 1; // Not supported
		uint64_t aes : 1;
		uint64_t poly_mul : 1;
		uint64_t sha1 : 1;
		uint64_t sha256 : 1;
		uint64_t sha512 : 1;
		uint64_t crc32 : 1;
		uint64_t atomic : 1;
		uint64_t rdm : 1;
		uint64_t sha3 : 1;
		uint64_t sm3 : 1;
		uint64_t sm4 : 1;
		uint64_t dp : 1;
		uint64_t fhm : 1;
		uint64_t dc_cvap : 1;
		uint64_t dc_cvadp : 1;
		uint64_t apa_base : 1;
		uint64_t apa_ep : 1;
		uint64_t apa_ep2 : 1;
		uint64_t apa_ep2_fp : 1;
		uint64_t apa_ep2_fpc : 1;
		uint64_t jscvt : 1;
		uint64_t fcma : 1;
		uint64_t rcpc_v83 : 1;
		uint64_t rcpc_v84 : 1;
		uint64_t gpa : 1;
		uint64_t l1ip_pipt : 1;
		uint64_t dz_permitted : 1;
		uint64_t ssbs : 1;
		uint64_t ssbs_rw : 1;
		uint64_t smccc_w1_supported : 1;
		uint64_t smccc_w1_mitigated : 1;
		uint64_t smccc_w2_supported : 1;
		uint64_t smccc_w2_mitigated : 1;
		uint64_t csv2 : 1;
		uint64_t csv3 : 1;
		uint64_t sb : 1;
		uint64_t idc : 1;
		uint64_t dic : 1;
		uint64_t tlbi_os : 1;
		uint64_t tlbi_os_range : 1;
		uint64_t flags_m : 1;
		uint64_t flags_m2 : 1;
		uint64_t bf16 : 1;
		uint64_t ebf16 : 1;

		/* Second bank starts here. */
		uint64_t sve_bf16 : 1;
		uint64_t sve_ebf16 : 1;
		uint64_t i8mm : 1;
		uint64_t sve_i8mm : 1;
		uint64_t frintts : 1;
		uint64_t specres : 1;
		uint64_t mtpmu : 1;
		uint64_t rpres : 1;
		uint64_t exs : 1;
		uint64_t spec_sei : 1;
		uint64_t ets : 1;
		uint64_t afp : 1;
		uint64_t iesb : 1;
		uint64_t rng : 1;
		uint64_t lse2 : 1;
		uint64_t idst : 1;
		uint64_t ras_v1p1 : 1;
		uint64_t ras_frac_v1p1 : 1;
		uint64_t sel2 : 1;
		uint64_t amu_v1 : 1;
		uint64_t amu_v1p1 : 1;
		uint64_t dit : 1;
		uint64_t ccidx : 1;
		uint64_t fgt_for_intercepts : 1;
		uint64_t l1ip_vpipt : 1;
		uint64_t l1ip_vipt : 1;
		uint64_t debug_v8 : 1;
		uint64_t debug_v8p2 : 1;
		uint64_t debug_v8p4 : 1;
		uint64_t pmu_v3_arm_v87 : 1;
		uint64_t double_lock : 1;
		uint64_t clrbhb : 1;
		uint64_t spe : 1;
		uint64_t spe_v1p1 : 1;
		uint64_t spe_v1p2 : 1;
		uint64_t tt_cnp : 1;
		uint64_t hpds : 1;
		uint64_t sve : 1;
		uint64_t sve_v2 : 1;
		uint64_t sve_v2p1 : 1;
		uint64_t spec_fpacc : 1;
		uint64_t sve_aes : 1;
		uint64_t sve_bit_perm : 1;
		uint64_t sve_sha3 : 1;
		uint64_t sve_sm4 : 1;
		uint64_t e0_pd : 1;
		/* Remaining reserved bits */
		uint64_t reserved_bank1 : 18;

	};
#endif
};

union hv_partition_processor_xsave_features {
	struct {
		uint64_t xsave_support : 1;
		uint64_t xsaveopt_support : 1;
		uint64_t avx_support : 1;
		uint64_t avx2_support : 1;
		uint64_t fma_support: 1;
		uint64_t mpx_support: 1;
		uint64_t avx512_support : 1;
		uint64_t avx512_dq_support : 1;
		uint64_t avx512_cd_support : 1;
		uint64_t avx512_bw_support : 1;
		uint64_t avx512_vl_support : 1;
		uint64_t xsave_comp_support : 1;
		uint64_t xsave_supervisor_support : 1;
		uint64_t xcr1_support : 1;
		uint64_t avx512_bitalg_support : 1;
		uint64_t avx512_i_fma_support : 1;
		uint64_t avx512_v_bmi_support : 1;
		uint64_t avx512_v_bmi2_support : 1;
		uint64_t avx512_vnni_support : 1;
		uint64_t gfni_support : 1;
		uint64_t vaes_support : 1;
		uint64_t avx512_v_popcntdq_support : 1;
		uint64_t vpclmulqdq_support : 1;
		uint64_t avx512_bf16_support : 1;
		uint64_t avx512_vp2_intersect_support : 1;
		uint64_t avx512_fp16_support : 1;
		uint64_t xfd_support : 1;
		uint64_t amx_tile_support : 1;
		uint64_t amx_bf16_support : 1;
		uint64_t amx_int8_support : 1;
		uint64_t avx_vnni_support : 1;
		uint64_t avx_ifma_support : 1;
		uint64_t avx_ne_convert_support : 1;
		uint64_t avx_vnni_int8_support : 1;
		uint64_t avx_vnni_int16_support : 1;
		uint64_t avx10_1_256_support : 1;
		uint64_t avx10_1_512_support : 1;
		uint64_t amx_fp16_support : 1;
		uint64_t reserved1 : 26;
	};
	uint64_t as_uint64;
};

struct hv_partition_creation_properties {
	union hv_partition_processor_features disabled_processor_features;
	union hv_partition_processor_xsave_features
		disabled_processor_xsave_features;
};


enum hv_translate_gva_result_code {
    HV_TRANSLATE_GVA_SUCCESS                    = 0,

    /* Translation failures. */
    HV_TRANSLATE_GVA_PAGE_NOT_PRESENT           = 1,
    HV_TRANSLATE_GVA_PRIVILEGE_VIOLATION        = 2,
    HV_TRANSLATE_GVA_INVALIDE_PAGE_TABLE_FLAGS  = 3,

    /* GPA access failures. */
    HV_TRANSLATE_GVA_GPA_UNMAPPED               = 4,
    HV_TRANSLATE_GVA_GPA_NO_READ_ACCESS         = 5,
    HV_TRANSLATE_GVA_GPA_NO_WRITE_ACCESS        = 6,
    HV_TRANSLATE_GVA_GPA_ILLEGAL_OVERLAY_ACCESS = 7,

    /*
     * Intercept for memory access by either
     *  - a higher VTL
     *  - a nested hypervisor (due to a violation of the nested page table)
     */
    HV_TRANSLATE_GVA_INTERCEPT                  = 8,

    HV_TRANSLATE_GVA_GPA_UNACCEPTED             = 9,
};

union hv_translate_gva_result {
    uint64_t as_uint64;
    struct {
        uint32_t result_code; /* enum hv_translate_hva_result_code */
        uint32_t cache_type:8;
        uint32_t overlay_page:1;
        uint32_t reserved:23;
    };
};

typedef struct hv_input_translate_virtual_address {
    uint64_t partition_id;
    uint32_t vp_index;
    uint32_t padding;
    uint64_t control_flags;
    uint64_t gva_page;
} hv_input_translate_virtual_address;

typedef struct hv_output_translate_virtual_address {
    union hv_translate_gva_result translation_result;
    uint64_t gpa_page;
} hv_output_translate_virtual_address;

typedef struct hv_register_x64_cpuid_result_parameters {
    struct {
        uint32_t eax;
        uint32_t ecx;
        uint8_t subleaf_specific;
        uint8_t always_override;
        uint16_t padding;
    } input;
    struct {
        uint32_t eax;
        uint32_t eax_mask;
        uint32_t ebx;
        uint32_t ebx_mask;
        uint32_t ecx;
        uint32_t ecx_mask;
        uint32_t edx;
        uint32_t edx_mask;
    } result;
} hv_register_x64_cpuid_result_parameters;

typedef struct hv_register_x64_msr_result_parameters {
    uint32_t msr_index;
    uint32_t access_type;
    uint32_t action; /* enum hv_unimplemented_msr_action */
} hv_register_x64_msr_result_parameters;

union hv_register_intercept_result_parameters {
    struct hv_register_x64_cpuid_result_parameters cpuid;
    struct hv_register_x64_msr_result_parameters msr;
};

typedef struct hv_input_register_intercept_result {
    uint64_t partition_id;
    uint32_t vp_index;
    uint32_t intercept_type; /* enum hv_intercept_type */
    union hv_register_intercept_result_parameters parameters;
} hv_input_register_intercept_result;

#endif /* HW_HYPERV_HVHDK_H */
