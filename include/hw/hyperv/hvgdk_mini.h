/*
 * Userspace interfaces for /dev/mshv* devices and derived fds
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_HYPERV_HVGDK_MINI_H
#define HW_HYPERV_HVGDK_MINI_H

#define MSHV_IOCTL  0xB8

/* Hyper-V specific model specific registers (MSRs) */

/* HV_X64_SYNTHETIC_MSR */
#define HV_X64_MSR_GUEST_OS_ID      0x40000000
#define HV_X64_MSR_HYPERCALL        0x40000001
#define HV_X64_MSR_VP_INDEX         0x40000002
#define HV_X64_MSR_RESET            0x40000003
#define HV_X64_MSR_VP_RUNTIME       0x40000010
#define HV_X64_MSR_TIME_REF_COUNT   0x40000020
#define HV_X64_MSR_REFERENCE_TSC    0x40000021
#define HV_X64_MSR_TSC_FREQUENCY    0x40000022
#define HV_X64_MSR_APIC_FREQUENCY   0x40000023

typedef enum hv_register_name {
    /* Suspend Registers */
    HV_REGISTER_EXPLICIT_SUSPEND     = 0x00000000,
    HV_REGISTER_INTERCEPT_SUSPEND    = 0x00000001,
    HV_REGISTER_INTERNAL_ACTIVITY_STATE = 0x00000004,

    /* Pending Interruption Register */
    HV_REGISTER_PENDING_INTERRUPTION = 0x00010002,
    HV_REGISTER_INTERRUPT_STATE      = 0x00010003,
    HV_REGISTER_PENDING_EVENT0       = 0x00010004,
    HV_REGISTER_PENDING_EVENT1       = 0x00010005,

#if defined(__aarch64__)
    HV_ARM64_REGISTER_XZR   = 0x0002FFFE,
    HV_ARM64_REGISTER_X0    = 0x00020000,
    HV_ARM64_REGISTER_X1    = 0x00020001,
    HV_ARM64_REGISTER_X2    = 0x00020002,
    HV_ARM64_REGISTER_X3    = 0x00020003,
    HV_ARM64_REGISTER_X4    = 0x00020004,
    HV_ARM64_REGISTER_X5    = 0x00020005,
    HV_ARM64_REGISTER_X6    = 0x00020006,
    HV_ARM64_REGISTER_X7    = 0x00020007,
    HV_ARM64_REGISTER_X8    = 0x00020008,
    HV_ARM64_REGISTER_X9    = 0x00020009,
    HV_ARM64_REGISTER_X10   = 0x0002000A,
    HV_ARM64_REGISTER_X11   = 0x0002000B,
    HV_ARM64_REGISTER_X12   = 0x0002000C,
    HV_ARM64_REGISTER_X13   = 0x0002000D,
    HV_ARM64_REGISTER_X14   = 0x0002000E,
    HV_ARM64_REGISTER_X15   = 0x0002000F,
    HV_ARM64_REGISTER_X16   = 0x00020010,
    HV_ARM64_REGISTER_X17   = 0x00020011,
    HV_ARM64_REGISTER_X18   = 0x00020012,
    HV_ARM64_REGISTER_X19   = 0x00020013,
    HV_ARM64_REGISTER_X20   = 0x00020014,
    HV_ARM64_REGISTER_X21   = 0x00020015,
    HV_ARM64_REGISTER_X22   = 0x00020016,
    HV_ARM64_REGISTER_X23   = 0x00020017,
    HV_ARM64_REGISTER_X24   = 0x00020018,
    HV_ARM64_REGISTER_X25   = 0x00020019,
    HV_ARM64_REGISTER_X26   = 0x0002001A,
    HV_ARM64_REGISTER_X27   = 0x0002001B,
    HV_ARM64_REGISTER_X28   = 0x0002001C,
    HV_ARM64_REGISTER_FP    = 0x0002001D,
    HV_ARM64_REGISTER_LR    = 0x0002001E,
    HV_ARM64_REGISTER_PC    = 0x00020022,

    /* AArch64 System Register Descriptions: Floating-point registers */
	HV_ARM64_REGISTER_Q0 = 0x00030000,
	HV_ARM64_REGISTER_Q1 = 0x00030001,
	HV_ARM64_REGISTER_Q2 = 0x00030002,
	HV_ARM64_REGISTER_Q3 = 0x00030003,
	HV_ARM64_REGISTER_Q4 = 0x00030004,
	HV_ARM64_REGISTER_Q5 = 0x00030005,
	HV_ARM64_REGISTER_Q6 = 0x00030006,
	HV_ARM64_REGISTER_Q7 = 0x00030007,
	HV_ARM64_REGISTER_Q8 = 0x00030008,
	HV_ARM64_REGISTER_Q9 = 0x00030009,
	HV_ARM64_REGISTER_Q10 = 0x0003000A,
	HV_ARM64_REGISTER_Q11 = 0x0003000B,
	HV_ARM64_REGISTER_Q12 = 0x0003000C,
	HV_ARM64_REGISTER_Q13 = 0x0003000D,
	HV_ARM64_REGISTER_Q14 = 0x0003000E,
	HV_ARM64_REGISTER_Q15 = 0x0003000F,
	HV_ARM64_REGISTER_Q16 = 0x00030010,
	HV_ARM64_REGISTER_Q17 = 0x00030011,
	HV_ARM64_REGISTER_Q18 = 0x00030012,
	HV_ARM64_REGISTER_Q19 = 0x00030013,
	HV_ARM64_REGISTER_Q20 = 0x00030014,
	HV_ARM64_REGISTER_Q21 = 0x00030015,
	HV_ARM64_REGISTER_Q22 = 0x00030016,
	HV_ARM64_REGISTER_Q23 = 0x00030017,
	HV_ARM64_REGISTER_Q24 = 0x00030018,
	HV_ARM64_REGISTER_Q25 = 0x00030019,
	HV_ARM64_REGISTER_Q26 = 0x0003001A,
	HV_ARM64_REGISTER_Q27 = 0x0003001B,
	HV_ARM64_REGISTER_Q28 = 0x0003001C,
	HV_ARM64_REGISTER_Q29 = 0x0003001D,
	HV_ARM64_REGISTER_Q30 = 0x0003001E,
	HV_ARM64_REGISTER_Q31 = 0x0003001F,

    /* AArch64 System Register Descriptions: Special-purpose registers */
	HV_ARM64_REGISTER_CURRENT_EL   = 0x00021003,
	HV_ARM64_REGISTER_DAIF         = 0x00021004,
	HV_ARM64_REGISTER_DIT          = 0x00021005,
	HV_ARM64_REGISTER_PSTATE       = 0x00020023, // Legacy
	HV_ARM64_REGISTER_ELR_EL1      = 0x00040015, // Legacy
	HV_ARM64_REGISTER_ELR_EL2      = 0x00021001,
	HV_ARM64_REGISTER_ELR_ELX      = 0x0002100C, // ElrEl1 or ElrEl2 depending on current EL.
	HV_ARM64_REGISTER_FPCR         = 0x00040012, // Legacy
	HV_ARM64_REGISTER_FPSR         = 0x00040013, // Legacy
	HV_ARM64_REGISTER_NZCV         = 0x00021006,
	HV_ARM64_REGISTER_PAN          = 0x00021007,
	HV_ARM64_REGISTER_SP           = 0x0002001F, // Legacy
	HV_ARM64_REGISTER_SP_EL0       = 0x00020020, // Legacy
	HV_ARM64_REGISTER_SP_EL1       = 0x00020021, // Legacy
	HV_ARM64_REGISTER_SP_EL2       = 0x00021000,
	HV_ARM64_REGISTER_SP_SEL       = 0x00021008,
	HV_ARM64_REGISTER_SPSR_EL1     = 0x00040014, // Legacy
	HV_ARM64_REGISTER_SPSR_EL2     = 0x00021002,
	HV_ARM64_REGISTER_SPSR_ELX     = 0x0002100D, // SpsrEl1 or SpsrEl2 depending on current EL.
	HV_ARM64_REGISTER_SSBS         = 0x00021009,
	HV_ARM64_REGISTER_TCO          = 0x0002100A,
	HV_ARM64_REGISTER_UAO          = 0x0002100B,

    /* AArch64 System Register Descriptions: ID Registers */
    HV_ARM64_REGISTER_ID_MIDR_EL1         = 0x00022000,
    HV_ARM64_REGISTER_ID_MPIDR_EL1        = 0x00022005,
    HV_ARM64_REGISTER_ID_AA64_PFR0_EL1    = 0x00022020,
    HV_ARM64_REGISTER_ID_AA64_PFR1_EL1    = 0x00022021,
    HV_ARM64_REGISTER_ID_AA64_ISAR0_EL1   = 0x00022030,
    HV_ARM64_REGISTER_ID_AA64_ISAR1_EL1   = 0x00022031,
    HV_ARM64_REGISTER_ID_AA64_ISAR2_EL1   = 0x00022032,
    HV_ARM64_REGISTER_ID_AA64_MMFR0_EL1   = 0x00022038,
    HV_ARM64_REGISTER_ID_AA64_MMFR1_EL1   = 0x00022039,
    HV_ARM64_REGISTER_ID_AA64_MMFR2_EL1   = 0x0002203a,
    HV_ARM64_REGISTER_ID_AA64_DFR0_EL1    = 0x00022028,
    HV_ARM64_REGISTER_ID_AA64_DFR1_EL1    = 0x00022029,

    /* AArch64 System Register Descriptions: General system control registers */
	HV_ARM64_REGISTER_ACCDATA_EL1        = 0x00040020,
	HV_ARM64_REGISTER_ACTLR_EL1          = 0x00040003,
	HV_ARM64_REGISTER_ACTLR_EL2          = 0x00040021,
	HV_ARM64_REGISTER_AFSR0_EL1          = 0x00040016,
	HV_ARM64_REGISTER_AFSR0_EL2          = 0x00040022,
	HV_ARM64_REGISTER_AFSR0_ELX          = 0x00040073,
	HV_ARM64_REGISTER_AFSR1_EL1          = 0x00040017,
	HV_ARM64_REGISTER_AFSR1_EL2          = 0x00040023,
	HV_ARM64_REGISTER_AFSR1_ELX          = 0x00040074,
	HV_ARM64_REGISTER_AIDR_EL1           = 0x00040024,
	HV_ARM64_REGISTER_AMAIR_EL1          = 0x00040018,
	HV_ARM64_REGISTER_AMAIR_EL2          = 0x00040025,
	HV_ARM64_REGISTER_AMAIR_ELX          = 0x00040075,
	HV_ARM64_REGISTER_APD_A_KEY_HI_EL1   = 0x00040026,
	HV_ARM64_REGISTER_APD_A_KEY_LO_EL1   = 0x00040027,
	HV_ARM64_REGISTER_APD_B_KEY_HI_EL1   = 0x00040028,
	HV_ARM64_REGISTER_APD_B_KEY_LO_EL1   = 0x00040029,
	HV_ARM64_REGISTER_APG_A_KEY_HI_EL1   = 0x0004002A,
	HV_ARM64_REGISTER_APG_A_KEY_LO_EL1   = 0x0004002B,
	HV_ARM64_REGISTER_API_A_KEY_HI_EL1   = 0x0004002C,
	HV_ARM64_REGISTER_API_A_KEY_LO_EL1   = 0x0004002D,
	HV_ARM64_REGISTER_API_B_KEY_HI_EL1   = 0x0004002E,
	HV_ARM64_REGISTER_API_B_KEY_LO_EL1   = 0x0004002F,
	HV_ARM64_REGISTER_CCSIDR_EL1         = 0x00040030,
	HV_ARM64_REGISTER_CCSIDR2_EL1        = 0x00040031,
	HV_ARM64_REGISTER_CLIDR_EL1          = 0x00040032,
	HV_ARM64_REGISTER_CONTEXTIDR_EL1     = 0x0004000D,
	HV_ARM64_REGISTER_CONTEXTIDR_EL2     = 0x00040033,
	HV_ARM64_REGISTER_CONTEXTIDR_ELX     = 0x00040076,
	HV_ARM64_REGISTER_CPACR_EL1          = 0x00040004,
	HV_ARM64_REGISTER_CPTR_EL2           = 0x00040034,
	HV_ARM64_REGISTER_CPACR_ELX          = 0x00040077,
	HV_ARM64_REGISTER_CSSELR_EL1         = 0x00040035,
	HV_ARM64_REGISTER_CTR_EL0            = 0x00040036,
	HV_ARM64_REGISTER_DACR32_EL2         = 0x00040037,
	HV_ARM64_REGISTER_DCZID_EL0          = 0x00040038,
	HV_ARM64_REGISTER_ESR_EL1            = 0x00040008,
	HV_ARM64_REGISTER_ESR_EL2            = 0x00040039,
	HV_ARM64_REGISTER_ESR_ELX            = 0x00040078,
	HV_ARM64_REGISTER_FAR_EL1            = 0x00040009,
	HV_ARM64_REGISTER_FAR_EL2            = 0x0004003A,
	HV_ARM64_REGISTER_FAR_ELX            = 0x00040079,
	HV_ARM64_REGISTER_FPEXC32_EL2        = 0x0004003B,
	HV_ARM64_REGISTER_GCR_EL1            = 0x0004003C,
	HV_ARM64_REGISTER_GMID_EL1           = 0x0004003D,
	HV_ARM64_REGISTER_HACR_EL2           = 0x0004003E,
	HV_ARM64_REGISTER_HAFGRTR_EL2        = 0x0004003F,
	HV_ARM64_REGISTER_HCR_EL2            = 0x00040040,
	HV_ARM64_REGISTER_HCRX_EL2           = 0x00040041,
	HV_ARM64_REGISTER_HDFGRTR_EL2        = 0x00040042,
	HV_ARM64_REGISTER_HDFGWTR_EL2        = 0x00040043,
	HV_ARM64_REGISTER_HFGITR_EL2         = 0x00040044,
	HV_ARM64_REGISTER_HFGRTR_EL2         = 0x00040045,
	HV_ARM64_REGISTER_HFGWTR_EL2         = 0x00040046,
	HV_ARM64_REGISTER_HPFAR_EL2          = 0x00040047,
	HV_ARM64_REGISTER_HSTR_EL2           = 0x00040048,
	HV_ARM64_REGISTER_IFSR32_EL2         = 0x00040049,
	HV_ARM64_REGISTER_ISR_EL1            = 0x0004004A,
	HV_ARM64_REGISTER_LORC_EL1           = 0x0004004B,
	HV_ARM64_REGISTER_LOREA_EL1          = 0x0004004C,
	HV_ARM64_REGISTER_LORID_EL1          = 0x0004004D,
	HV_ARM64_REGISTER_LORN_EL1           = 0x0004004E,
	HV_ARM64_REGISTER_LORSA_EL1          = 0x0004004F,
	HV_ARM64_REGISTER_MAIR_EL1           = 0x0004000B,
	HV_ARM64_REGISTER_MAIR_EL2           = 0x00040050,
	HV_ARM64_REGISTER_MAIR_ELX           = 0x0004007A,
	HV_ARM64_REGISTER_MIDR_EL1           = 0x00040051,
	HV_ARM64_REGISTER_MPIDR_EL1          = 0x00040001,
	HV_ARM64_REGISTER_MVFR0_EL1          = 0x00040052,
	HV_ARM64_REGISTER_MVFR1_EL1          = 0x00040053,
	HV_ARM64_REGISTER_MVFR2_EL1          = 0x00040054,
	HV_ARM64_REGISTER_PAR_EL1            = 0x0004000A,
	HV_ARM64_REGISTER_REVIDR_EL1         = 0x00040055,
	HV_ARM64_REGISTER_RGSR_EL1           = 0x00040056,
	HV_ARM64_REGISTER_RNDR               = 0x00040057,
	HV_ARM64_REGISTER_RNDRRS             = 0x00040058,
	HV_ARM64_REGISTER_SCTLR_EL1          = 0x00040002,
	HV_ARM64_REGISTER_SCTLR_EL2          = 0x00040059,
	HV_ARM64_REGISTER_SCTLR_ELX          = 0x0004007B,
	HV_ARM64_REGISTER_SCXTNUM_EL0        = 0x0004005A,
	HV_ARM64_REGISTER_SCXTNUM_EL1        = 0x0004005B,
	HV_ARM64_REGISTER_SCXTNUM_EL2        = 0x0004005C,
	HV_ARM64_REGISTER_SMCR_EL1           = 0x0004005D,
	HV_ARM64_REGISTER_SMCR_EL2           = 0x0004005E,
	HV_ARM64_REGISTER_SMIDR_EL1          = 0x0004005F,
	HV_ARM64_REGISTER_SMPRI_EL1          = 0x00040060,
	HV_ARM64_REGISTER_SMPRIMAP_EL2       = 0x00040061,
	HV_ARM64_REGISTER_TCR_EL1            = 0x00040007,
	HV_ARM64_REGISTER_TCR_EL2            = 0x00040062,
	HV_ARM64_REGISTER_TCR_ELX            = 0x0004007C,
	HV_ARM64_REGISTER_TFSRE0_EL1         = 0x00040063,
	HV_ARM64_REGISTER_TFSR_EL1           = 0x00040064,
	HV_ARM64_REGISTER_TFSR_EL2           = 0x00040065,
	HV_ARM64_REGISTER_TPIDR2_EL0         = 0x00040066,
	HV_ARM64_REGISTER_TPIDR_EL0          = 0x00040011,
	HV_ARM64_REGISTER_TPIDR_EL1          = 0x0004000E,
	HV_ARM64_REGISTER_TPIDR_EL2          = 0x00040067,
	HV_ARM64_REGISTER_TPIDRRO_EL0        = 0x00040010,
	HV_ARM64_REGISTER_TTBR0_EL1          = 0x00040005,
	HV_ARM64_REGISTER_TTBR0_EL2          = 0x00040068,
	HV_ARM64_REGISTER_TTBR0_ELX          = 0x0004007D,
	HV_ARM64_REGISTER_TTBR1_EL1          = 0x00040006,
	HV_ARM64_REGISTER_TTBR1_EL2          = 0x0004007E,
	HV_ARM64_REGISTER_TTBR1_ELX          = 0x0004007F,
	HV_ARM64_REGISTER_VBAR_EL1           = 0x0004000C,
	HV_ARM64_REGISTER_VBAR_EL2           = 0x00040069,
	HV_ARM64_REGISTER_VBAR_ELX           = 0x00040080,
	HV_ARM64_REGISTER_VMPIDR_EL2         = 0x0004006A,
	HV_ARM64_REGISTER_VNCR_EL2           = 0x0004006B,
	HV_ARM64_REGISTER_VPIDR_EL2          = 0x0004006C,
	HV_ARM64_REGISTER_VSTCR_EL2          = 0x0004006D,
	HV_ARM64_REGISTER_VSTTBR_EL2         = 0x0004006E,
	HV_ARM64_REGISTER_VTCR_EL2           = 0x0004006F,
	HV_ARM64_REGISTER_VTTBR_EL2          = 0x00040070,
	HV_ARM64_REGISTER_ZCR_EL1            = 0x00040071,
	HV_ARM64_REGISTER_ZCR_EL2            = 0x00040072,
	HV_ARM64_REGISTER_ZCR_ELX            = 0x00040081,

    /* ARM GIC (System Registers): The GIC Redistributor */
    HV_ARM64_REGISTER_GICR_BASE_GPA       = 0x00063000,

    /* AArch64 System Register Descriptions: Debug Registers */
	HV_ARM64_REGISTER_DBGAUTHSTATUS_EL1 = 0x00050040,
	HV_ARM64_REGISTER_DBGBCR0_EL1 = 0x00050000,
	HV_ARM64_REGISTER_DBGBCR1_EL1 = 0x00050001,
	HV_ARM64_REGISTER_DBGBCR2_EL1 = 0x00050002,
	HV_ARM64_REGISTER_DBGBCR3_EL1 = 0x00050003,
	HV_ARM64_REGISTER_DBGBCR4_EL1 = 0x00050004,
	HV_ARM64_REGISTER_DBGBCR5_EL1 = 0x00050005,
	HV_ARM64_REGISTER_DBGBCR6_EL1 = 0x00050006,
	HV_ARM64_REGISTER_DBGBCR7_EL1 = 0x00050007,
	HV_ARM64_REGISTER_DBGBCR8_EL1 = 0x00050008,
	HV_ARM64_REGISTER_DBGBCR9_EL1 = 0x00050009,
	HV_ARM64_REGISTER_DBGBCR10_EL1 = 0x0005000A,
	HV_ARM64_REGISTER_DBGBCR11_EL1 = 0x0005000B,
	HV_ARM64_REGISTER_DBGBCR12_EL1 = 0x0005000C,
	HV_ARM64_REGISTER_DBGBCR13_EL1 = 0x0005000D,
	HV_ARM64_REGISTER_DBGBCR14_EL1 = 0x0005000E,
	HV_ARM64_REGISTER_DBGBCR15_EL1 = 0x0005000F,
	HV_ARM64_REGISTER_DBGBVR0_EL1 = 0x00050020,
	HV_ARM64_REGISTER_DBGBVR1_EL1 = 0x00050021,
	HV_ARM64_REGISTER_DBGBVR2_EL1 = 0x00050022,
	HV_ARM64_REGISTER_DBGBVR3_EL1 = 0x00050023,
	HV_ARM64_REGISTER_DBGBVR4_EL1 = 0x00050024,
	HV_ARM64_REGISTER_DBGBVR5_EL1 = 0x00050025,
	HV_ARM64_REGISTER_DBGBVR6_EL1 = 0x00050026,
	HV_ARM64_REGISTER_DBGBVR7_EL1 = 0x00050027,
	HV_ARM64_REGISTER_DBGBVR8_EL1 = 0x00050028,
	HV_ARM64_REGISTER_DBGBVR9_EL1 = 0x00050029,
	HV_ARM64_REGISTER_DBGBVR10_EL1 = 0x0005002A,
	HV_ARM64_REGISTER_DBGBVR11_EL1 = 0x0005002B,
	HV_ARM64_REGISTER_DBGBVR12_EL1 = 0x0005002C,
	HV_ARM64_REGISTER_DBGBVR13_EL1 = 0x0005002D,
	HV_ARM64_REGISTER_DBGBVR14_EL1 = 0x0005002E,
	HV_ARM64_REGISTER_DBGBVR15_EL1 = 0x0005002F,
	HV_ARM64_REGISTER_DBGCLAIMCLR_EL1 = 0x00050041,
	HV_ARM64_REGISTER_DBGCLAIMSET_EL1 = 0x00050042,
	HV_ARM64_REGISTER_DBGDTRRX_EL0 = 0x00050043,
	HV_ARM64_REGISTER_DBGDTRTX_EL0 = 0x00050044,
	HV_ARM64_REGISTER_DBGPRCR_EL1 = 0x00050045,
	HV_ARM64_REGISTER_DBGVCR32_EL2 = 0x00050046,
	HV_ARM64_REGISTER_DBGWCR0_EL1 = 0x00050010,
	HV_ARM64_REGISTER_DBGWCR1_EL1 = 0x00050011,
	HV_ARM64_REGISTER_DBGWCR2_EL1 = 0x00050012,
	HV_ARM64_REGISTER_DBGWCR3_EL1 = 0x00050013,
	HV_ARM64_REGISTER_DBGWCR4_EL1 = 0x00050014,
	HV_ARM64_REGISTER_DBGWCR5_EL1 = 0x00050015,
	HV_ARM64_REGISTER_DBGWCR6_EL1 = 0x00050016,
	HV_ARM64_REGISTER_DBGWCR7_EL1 = 0x00050017,
	HV_ARM64_REGISTER_DBGWCR8_EL1 = 0x00050018,
	HV_ARM64_REGISTER_DBGWCR9_EL1 = 0x00050019,
	HV_ARM64_REGISTER_DBGWCR10_EL1 = 0x0005001A,
	HV_ARM64_REGISTER_DBGWCR11_EL1 = 0x0005001B,
	HV_ARM64_REGISTER_DBGWCR12_EL1 = 0x0005001C,
	HV_ARM64_REGISTER_DBGWCR13_EL1 = 0x0005001D,
	HV_ARM64_REGISTER_DBGWCR14_EL1 = 0x0005001E,
	HV_ARM64_REGISTER_DBGWCR15_EL1 = 0x0005001F,
	HV_ARM64_REGISTER_DBGWVR0_EL1 = 0x00050030,
	HV_ARM64_REGISTER_DBGWVR1_EL1 = 0x00050031,
	HV_ARM64_REGISTER_DBGWVR2_EL1 = 0x00050032,
	HV_ARM64_REGISTER_DBGWVR3_EL1 = 0x00050033,
	HV_ARM64_REGISTER_DBGWVR4_EL1 = 0x00050034,
	HV_ARM64_REGISTER_DBGWVR5_EL1 = 0x00050035,
	HV_ARM64_REGISTER_DBGWVR6_EL1 = 0x00050036,
	HV_ARM64_REGISTER_DBGWVR7_EL1 = 0x00050037,
	HV_ARM64_REGISTER_DBGWVR8_EL1 = 0x00050038,
	HV_ARM64_REGISTER_DBGWVR9_EL1 = 0x00050039,
	HV_ARM64_REGISTER_DBGWVR10_EL1 = 0x0005003A,
	HV_ARM64_REGISTER_DBGWVR11_EL1 = 0x0005003B,
	HV_ARM64_REGISTER_DBGWVR12_EL1 = 0x0005003C,
	HV_ARM64_REGISTER_DBGWVR13_EL1 = 0x0005003D,
	HV_ARM64_REGISTER_DBGWVR14_EL1 = 0x0005003E,
	HV_ARM64_REGISTER_DBGWVR15_EL1 = 0x0005003F,
	HV_ARM64_REGISTER_DLR_EL0 = 0x00050047,
	HV_ARM64_REGISTER_DSPSR_EL0 = 0x00050048,
	HV_ARM64_REGISTER_MDCCINT_EL1 = 0x00050049,
	HV_ARM64_REGISTER_MDCCSR_EL0 = 0x0005004A,
	HV_ARM64_REGISTER_MDCR_EL2 = 0x0005004B,
	HV_ARM64_REGISTER_MDRAR_EL1 = 0x0005004C,
	HV_ARM64_REGISTER_MDSCR_EL1 = 0x0005004D,
	HV_ARM64_REGISTER_OSDLR_EL1 = 0x0005004E,
	HV_ARM64_REGISTER_OSDTRRX_EL1 = 0x0005004F,
	HV_ARM64_REGISTER_OSDTRTX_EL1 = 0x00050050,
	HV_ARM64_REGISTER_OSECCR_EL1 = 0x00050051,
	HV_ARM64_REGISTER_OSLAR_EL1 = 0x00050052,
	HV_ARM64_REGISTER_OSLSR_EL1 = 0x00050053,
	HV_ARM64_REGISTER_SDER32_EL2 = 0x00050054,
	HV_ARM64_REGISTER_TRFCR_EL1 = 0x00050055,
	HV_ARM64_REGISTER_TRFCR_EL2 = 0x00050056,
	HV_ARM64_REGISTER_TRFCR_ELX = 0x00050057, // TrfcrEl1 or TrfcrEl2 depending on current EL.

    /* AArch64 System Register Descriptions: Generic Timer Registers */
	HV_ARM64_REGISTER_CNTFRQ_EL0 = 0x00058000,
	HV_ARM64_REGISTER_CNTHCTL_EL2 = 0x00058001,
	HV_ARM64_REGISTER_CNTHP_CTL_EL2 = 0x00058002,
	HV_ARM64_REGISTER_CNTHP_CVAL_EL2 = 0x00058003,
	HV_ARM64_REGISTER_CNTHP_TVAL_EL2 = 0x00058004,
	HV_ARM64_REGISTER_CNTHV_CTL_EL2 = 0x00058005,
	HV_ARM64_REGISTER_CNTHV_CVAL_EL2 = 0x00058006,
	HV_ARM64_REGISTER_CNTHV_TVAL_EL2 = 0x00058007,
	HV_ARM64_REGISTER_CNTKCTL_EL1 = 0x00058008,
	HV_ARM64_REGISTER_CNTKCTL_ELX = 0x00058013, // CntkctlEl1 or CnthctlEl2 depending on EL.
	HV_ARM64_REGISTER_CNTP_CTL_EL0 = 0x00058009,
	HV_ARM64_REGISTER_CNTP_CTL_ELX = 0x00058014, // CntpCtlEl0 or CnthpCtlEl2 depending on EL.
	HV_ARM64_REGISTER_CNTP_CVAL_EL0 = 0x0005800A,
	HV_ARM64_REGISTER_CNTP_CVAL_ELX = 0x00058015, // CntpCvalEl0 or CnthpCvalEl2 depending on EL.
	HV_ARM64_REGISTER_CNTP_TVAL_EL0 = 0x0005800B,
	HV_ARM64_REGISTER_CNTP_TVAL_ELX = 0x00058016, // CntpTvalEl0 or CnthpTvalEl2 depending on EL.
	HV_ARM64_REGISTER_CNTPCT_EL0 = 0x0005800C,
	HV_ARM64_REGISTER_CNTPOFF_EL2 = 0x0005800D,
	HV_ARM64_REGISTER_CNTV_CTL_EL0 = 0x0005800E,
	HV_ARM64_REGISTER_CNTV_CTL_ELX = 0x00058017, // CntvCtlEl0 or CnthvCtlEl2 depending on EL.
	HV_ARM64_REGISTER_CNTV_CVAL_EL0 = 0x0005800F,
	HV_ARM64_REGISTER_CNTV_CVAL_ELX = 0x00058018, // CntvCvalEl0 or CnthvCvalEl2 depending on EL.
	HV_ARM64_REGISTER_CNTV_TVAL_EL0 = 0x00058010,
	HV_ARM64_REGISTER_CNTV_TVAL_ELX = 0x00058019, // CntvTvalEl0 or CnthvTvalEl2 depending on EL.
	HV_ARM64_REGISTER_CNTVCT_EL0 = 0x00058011,
	HV_ARM64_REGISTER_CNTVOFF_EL2 = 0x00058012,
#elif defined(__x86_64__)
    /* X64 User-Mode Registers */
    HV_X64_REGISTER_RAX     = 0x00020000,
    HV_X64_REGISTER_RCX     = 0x00020001,
    HV_X64_REGISTER_RDX     = 0x00020002,
    HV_X64_REGISTER_RBX     = 0x00020003,
    HV_X64_REGISTER_RSP     = 0x00020004,
    HV_X64_REGISTER_RBP     = 0x00020005,
    HV_X64_REGISTER_RSI     = 0x00020006,
    HV_X64_REGISTER_RDI     = 0x00020007,
    HV_X64_REGISTER_R8      = 0x00020008,
    HV_X64_REGISTER_R9      = 0x00020009,
    HV_X64_REGISTER_R10     = 0x0002000A,
    HV_X64_REGISTER_R11     = 0x0002000B,
    HV_X64_REGISTER_R12     = 0x0002000C,
    HV_X64_REGISTER_R13     = 0x0002000D,
    HV_X64_REGISTER_R14     = 0x0002000E,
    HV_X64_REGISTER_R15     = 0x0002000F,
    HV_X64_REGISTER_RIP     = 0x00020010,
    HV_X64_REGISTER_RFLAGS  = 0x00020011,

    /* X64 Floating Point and Vector Registers */
    HV_X64_REGISTER_XMM0                = 0x00030000,
    HV_X64_REGISTER_XMM1                = 0x00030001,
    HV_X64_REGISTER_XMM2                = 0x00030002,
    HV_X64_REGISTER_XMM3                = 0x00030003,
    HV_X64_REGISTER_XMM4                = 0x00030004,
    HV_X64_REGISTER_XMM5                = 0x00030005,
    HV_X64_REGISTER_XMM6                = 0x00030006,
    HV_X64_REGISTER_XMM7                = 0x00030007,
    HV_X64_REGISTER_XMM8                = 0x00030008,
    HV_X64_REGISTER_XMM9                = 0x00030009,
    HV_X64_REGISTER_XMM10               = 0x0003000A,
    HV_X64_REGISTER_XMM11               = 0x0003000B,
    HV_X64_REGISTER_XMM12               = 0x0003000C,
    HV_X64_REGISTER_XMM13               = 0x0003000D,
    HV_X64_REGISTER_XMM14               = 0x0003000E,
    HV_X64_REGISTER_XMM15               = 0x0003000F,
    HV_X64_REGISTER_FP_MMX0             = 0x00030010,
    HV_X64_REGISTER_FP_MMX1             = 0x00030011,
    HV_X64_REGISTER_FP_MMX2             = 0x00030012,
    HV_X64_REGISTER_FP_MMX3             = 0x00030013,
    HV_X64_REGISTER_FP_MMX4             = 0x00030014,
    HV_X64_REGISTER_FP_MMX5             = 0x00030015,
    HV_X64_REGISTER_FP_MMX6             = 0x00030016,
    HV_X64_REGISTER_FP_MMX7             = 0x00030017,
    HV_X64_REGISTER_FP_CONTROL_STATUS   = 0x00030018,
    HV_X64_REGISTER_XMM_CONTROL_STATUS  = 0x00030019,

    /* X64 Control Registers */
    HV_X64_REGISTER_CR0     = 0x00040000,
    HV_X64_REGISTER_CR2     = 0x00040001,
    HV_X64_REGISTER_CR3     = 0x00040002,
    HV_X64_REGISTER_CR4     = 0x00040003,
    HV_X64_REGISTER_CR8     = 0x00040004,
    HV_X64_REGISTER_XFEM    = 0x00040005,

    /* X64 Segment Registers */
    HV_X64_REGISTER_ES      = 0x00060000,
    HV_X64_REGISTER_CS      = 0x00060001,
    HV_X64_REGISTER_SS      = 0x00060002,
    HV_X64_REGISTER_DS      = 0x00060003,
    HV_X64_REGISTER_FS      = 0x00060004,
    HV_X64_REGISTER_GS      = 0x00060005,
    HV_X64_REGISTER_LDTR    = 0x00060006,
    HV_X64_REGISTER_TR      = 0x00060007,

    /* X64 Table Registers */
    HV_X64_REGISTER_IDTR    = 0x00070000,
    HV_X64_REGISTER_GDTR    = 0x00070001,

    /* X64 Virtualized MSRs */
    HV_X64_REGISTER_TSC             = 0x00080000,
    HV_X64_REGISTER_EFER            = 0x00080001,
    HV_X64_REGISTER_KERNEL_GS_BASE  = 0x00080002,
    HV_X64_REGISTER_APIC_BASE       = 0x00080003,
    HV_X64_REGISTER_PAT             = 0x00080004,
    HV_X64_REGISTER_SYSENTER_CS     = 0x00080005,
    HV_X64_REGISTER_SYSENTER_EIP    = 0x00080006,
    HV_X64_REGISTER_SYSENTER_ESP    = 0x00080007,
    HV_X64_REGISTER_STAR            = 0x00080008,
    HV_X64_REGISTER_LSTAR           = 0x00080009,
    HV_X64_REGISTER_CSTAR           = 0x0008000A,
    HV_X64_REGISTER_SFMASK          = 0x0008000B,
    HV_X64_REGISTER_INITIAL_APIC_ID = 0x0008000C,

    /* X64 Cache control MSRs */
    HV_X64_REGISTER_MSR_MTRR_CAP            = 0x0008000D,
    HV_X64_REGISTER_MSR_MTRR_DEF_TYPE       = 0x0008000E,
    HV_X64_REGISTER_MSR_MTRR_PHYS_BASE0     = 0x00080010,
    HV_X64_REGISTER_MSR_MTRR_PHYS_BASE1     = 0x00080011,
    HV_X64_REGISTER_MSR_MTRR_PHYS_BASE2     = 0x00080012,
    HV_X64_REGISTER_MSR_MTRR_PHYS_BASE3     = 0x00080013,
    HV_X64_REGISTER_MSR_MTRR_PHYS_BASE4     = 0x00080014,
    HV_X64_REGISTER_MSR_MTRR_PHYS_BASE5     = 0x00080015,
    HV_X64_REGISTER_MSR_MTRR_PHYS_BASE6     = 0x00080016,
    HV_X64_REGISTER_MSR_MTRR_PHYS_BASE7     = 0x00080017,
    HV_X64_REGISTER_MSR_MTRR_PHYS_BASE8     = 0x00080018,
    HV_X64_REGISTER_MSR_MTRR_PHYS_BASE9     = 0x00080019,
    HV_X64_REGISTER_MSR_MTRR_PHYS_BASEA     = 0x0008001A,
    HV_X64_REGISTER_MSR_MTRR_PHYS_BASEB     = 0x0008001B,
    HV_X64_REGISTER_MSR_MTRR_PHYS_BASEC     = 0x0008001C,
    HV_X64_REGISTER_MSR_MTRR_PHYS_BASED     = 0x0008001D,
    HV_X64_REGISTER_MSR_MTRR_PHYS_BASEE     = 0x0008001E,
    HV_X64_REGISTER_MSR_MTRR_PHYS_BASEF     = 0x0008001F,
    HV_X64_REGISTER_MSR_MTRR_PHYS_MASK0     = 0x00080040,
    HV_X64_REGISTER_MSR_MTRR_PHYS_MASK1     = 0x00080041,
    HV_X64_REGISTER_MSR_MTRR_PHYS_MASK2     = 0x00080042,
    HV_X64_REGISTER_MSR_MTRR_PHYS_MASK3     = 0x00080043,
    HV_X64_REGISTER_MSR_MTRR_PHYS_MASK4     = 0x00080044,
    HV_X64_REGISTER_MSR_MTRR_PHYS_MASK5     = 0x00080045,
    HV_X64_REGISTER_MSR_MTRR_PHYS_MASK6     = 0x00080046,
    HV_X64_REGISTER_MSR_MTRR_PHYS_MASK7     = 0x00080047,
    HV_X64_REGISTER_MSR_MTRR_PHYS_MASK8     = 0x00080048,
    HV_X64_REGISTER_MSR_MTRR_PHYS_MASK9     = 0x00080049,
    HV_X64_REGISTER_MSR_MTRR_PHYS_MASKA     = 0x0008004A,
    HV_X64_REGISTER_MSR_MTRR_PHYS_MASKB     = 0x0008004B,
    HV_X64_REGISTER_MSR_MTRR_PHYS_MASKC     = 0x0008004C,
    HV_X64_REGISTER_MSR_MTRR_PHYS_MASKD     = 0x0008004D,
    HV_X64_REGISTER_MSR_MTRR_PHYS_MASKE     = 0x0008004E,
    HV_X64_REGISTER_MSR_MTRR_PHYS_MASKF     = 0x0008004F,
    HV_X64_REGISTER_MSR_MTRR_FIX64K00000    = 0x00080070,
    HV_X64_REGISTER_MSR_MTRR_FIX16K80000    = 0x00080071,
    HV_X64_REGISTER_MSR_MTRR_FIX16KA0000    = 0x00080072,
    HV_X64_REGISTER_MSR_MTRR_FIX4KC0000     = 0x00080073,
    HV_X64_REGISTER_MSR_MTRR_FIX4KC8000     = 0x00080074,
    HV_X64_REGISTER_MSR_MTRR_FIX4KD0000     = 0x00080075,
    HV_X64_REGISTER_MSR_MTRR_FIX4KD8000     = 0x00080076,
    HV_X64_REGISTER_MSR_MTRR_FIX4KE0000     = 0x00080077,
    HV_X64_REGISTER_MSR_MTRR_FIX4KE8000     = 0x00080078,
    HV_X64_REGISTER_MSR_MTRR_FIX4KF0000     = 0x00080079,
    HV_X64_REGISTER_MSR_MTRR_FIX4KF8000     = 0x0008007A,

    HV_X64_REGISTER_TSC_AUX     = 0x0008007B,
    HV_X64_REGISTER_BNDCFGS     = 0x0008007C,
    HV_X64_REGISTER_DEBUG_CTL   = 0x0008007D,

    /* Available */

    HV_X64_REGISTER_SPEC_CTRL       = 0x00080084,
    HV_X64_REGISTER_TSC_ADJUST      = 0x00080096,

    /* CET / Shadow Stack */
    HV_X64_REGISTER_U_XSS                    = 0x0008008B,
    HV_X64_REGISTER_U_CET                    = 0x0008008C,
    HV_X64_REGISTER_S_CET                    = 0x0008008D,
    HV_X64_REGISTER_SSP                      = 0x0008008E,
    HV_X64_REGISTER_PL0_SSP                  = 0x0008008F,
    HV_X64_REGISTER_PL1_SSP                  = 0x00080090,
    HV_X64_REGISTER_PL2_SSP                  = 0x00080091,
    HV_X64_REGISTER_PL3_SSP                  = 0x00080092,
    HV_X64_REGISTER_INTERRUPT_SSP_TABLE_ADDR = 0x00080093,

    /* Other MSRs */
    HV_X64_REGISTER_MSR_IA32_MISC_ENABLE = 0x000800A0,

#endif

    /* Misc */
    HV_X64_REGISTER_HYPERCALL       = 0x00090001,
    HV_REGISTER_GUEST_OS_ID         = 0x00090002,
    HV_REGISTER_REFERENCE_TSC       = 0x00090017,

    /* Hypervisor-defined Registers (Synic) */
    HV_REGISTER_SINT0       = 0x000A0000,
    HV_REGISTER_SINT1       = 0x000A0001,
    HV_REGISTER_SINT2       = 0x000A0002,
    HV_REGISTER_SINT3       = 0x000A0003,
    HV_REGISTER_SINT4       = 0x000A0004,
    HV_REGISTER_SINT5       = 0x000A0005,
    HV_REGISTER_SINT6       = 0x000A0006,
    HV_REGISTER_SINT7       = 0x000A0007,
    HV_REGISTER_SINT8       = 0x000A0008,
    HV_REGISTER_SINT9       = 0x000A0009,
    HV_REGISTER_SINT10      = 0x000A000A,
    HV_REGISTER_SINT11      = 0x000A000B,
    HV_REGISTER_SINT12      = 0x000A000C,
    HV_REGISTER_SINT13      = 0x000A000D,
    HV_REGISTER_SINT14      = 0x000A000E,
    HV_REGISTER_SINT15      = 0x000A000F,
    HV_REGISTER_SCONTROL    = 0x000A0010,
    HV_REGISTER_SVERSION    = 0x000A0011,
    HV_REGISTER_SIEFP       = 0x000A0012,
    HV_REGISTER_SIMP        = 0x000A0013,
    HV_REGISTER_EOM         = 0x000A0014,
    HV_REGISTER_SIRBP       = 0x000A0015,
} hv_register_name;

enum hv_intercept_type {
    HV_INTERCEPT_TYPE_X64_IO_PORT       = 0X00000000,
    HV_INTERCEPT_TYPE_X64_MSR           = 0X00000001,
    HV_INTERCEPT_TYPE_X64_CPUID         = 0X00000002,
    HV_INTERCEPT_TYPE_EXCEPTION         = 0X00000003,

    /* Used to be HV_INTERCEPT_TYPE_REGISTER */
    HV_INTERCEPT_TYPE_RESERVED0         = 0X00000004,
    HV_INTERCEPT_TYPE_MMIO              = 0X00000005,
    HV_INTERCEPT_TYPE_X64_GLOBAL_CPUID  = 0X00000006,
    HV_INTERCEPT_TYPE_X64_APIC_SMI      = 0X00000007,
    HV_INTERCEPT_TYPE_HYPERCALL         = 0X00000008,

    HV_INTERCEPT_TYPE_X64_APIC_INIT_SIPI        = 0X00000009,
    HV_INTERCEPT_MC_UPDATE_PATCH_LEVEL_MSR_READ = 0X0000000A,

    HV_INTERCEPT_TYPE_X64_APIC_WRITE        = 0X0000000B,
    HV_INTERCEPT_TYPE_X64_MSR_INDEX         = 0X0000000C,
    HV_INTERCEPT_TYPE_MAX,
    HV_INTERCEPT_TYPE_INVALID               = 0XFFFFFFFF,
};

struct hv_u128 {
    uint64_t low_part;
    uint64_t high_part;
} QEMU_PACKED;

union hv_x64_xmm_control_status_register {
    struct hv_u128 as_uint128;
    struct {
        union {
            /* long mode */
            uint64_t last_fp_rdp;
            /* 32 bit mode */
            struct {
                uint32_t last_fp_dp;
                uint16_t last_fp_ds;
                uint16_t padding;
            };
        };
        uint32_t xmm_status_control;
        uint32_t xmm_status_control_mask;
    };
};

union hv_x64_fp_register {
    struct hv_u128 as_uint128;
    struct {
        uint64_t mantissa;
        uint64_t biased_exponent:15;
        uint64_t sign:1;
        uint64_t reserved:48;
    };
};

union hv_x64_pending_exception_event {
    uint64_t as_uint64[2];
    struct {
        uint32_t event_pending:1;
        uint32_t event_type:3;
        uint32_t reserved0:4;
        uint32_t deliver_error_code:1;
        uint32_t reserved1:7;
        uint32_t vector:16;
        uint32_t error_code;
        uint64_t exception_parameter;
    };
};

union hv_x64_pending_virtualization_fault_event {
    uint64_t as_uint64[2];
    struct {
        uint32_t event_pending:1;
        uint32_t event_type:3;
        uint32_t reserved0:4;
        uint32_t reserved1:8;
        uint32_t parameter0:16;
        uint32_t code;
        uint64_t parameter1;
    };
};

union hv_x64_pending_interruption_register {
    uint64_t as_uint64;
    struct {
        uint32_t interruption_pending:1;
        uint32_t interruption_type:3;
        uint32_t deliver_error_code:1;
        uint32_t instruction_length:4;
        uint32_t nested_event:1;
        uint32_t reserved:6;
        uint32_t interruption_vector:16;
        uint32_t error_code;
    };
};

union hv_x64_register_sev_control {
    uint64_t as_uint64;
    struct {
        uint64_t enable_encrypted_state:1;
        uint64_t reserved_z:11;
        uint64_t vmsa_gpa_page_number:52;
    };
};

union hv_x64_msr_npiep_config_contents {
    uint64_t as_uint64;
    struct {
        /*
         * These bits enable instruction execution prevention for
         * specific instructions.
         */
        uint64_t prevents_gdt:1;
        uint64_t prevents_idt:1;
        uint64_t prevents_ldt:1;
        uint64_t prevents_tr:1;

        /* The reserved bits must always be 0. */
        uint64_t reserved:60;
    };
};

typedef struct hv_x64_segment_register {
    uint64_t base;
    uint32_t limit;
    uint16_t selector;
    union {
        struct {
            uint16_t segment_type:4;
            uint16_t non_system_segment:1;
            uint16_t descriptor_privilege_level:2;
            uint16_t present:1;
            uint16_t reserved:4;
            uint16_t available:1;
            uint16_t _long:1;
            uint16_t _default:1;
            uint16_t granularity:1;
        };
        uint16_t attributes;
    };
} QEMU_PACKED hv_x64_segment_register;

typedef struct hv_x64_table_register {
    uint16_t pad[3];
    uint16_t limit;
    uint64_t base;
} QEMU_PACKED hv_x64_table_register;

union hv_x64_fp_control_status_register {
    struct hv_u128 as_uint128;
    struct {
        uint16_t fp_control;
        uint16_t fp_status;
        uint8_t fp_tag;
        uint8_t reserved;
        uint16_t last_fp_op;
        union {
            /* long mode */
            uint64_t last_fp_rip;
            /* 32 bit mode */
            struct {
                uint32_t last_fp_eip;
                uint16_t last_fp_cs;
                uint16_t padding;
            };
        };
    };
};

/* General Hypervisor Register Content Definitions */

union hv_explicit_suspend_register {
    uint64_t as_uint64;
    struct {
        uint64_t suspended:1;
        uint64_t reserved:63;
    };
};

union hv_internal_activity_register {
    uint64_t as_uint64;

    struct {
        uint64_t startup_suspend:1;
        uint64_t halt_suspend:1;
        uint64_t idle_suspend:1;
        uint64_t rsvd_z:61;
    };
};

union hv_x64_interrupt_state_register {
    uint64_t as_uint64;
    struct {
        uint64_t interrupt_shadow:1;
        uint64_t nmi_masked:1;
        uint64_t reserved:62;
    };
};

union hv_intercept_suspend_register {
    uint64_t as_uint64;
    struct {
        uint64_t suspended:1;
        uint64_t reserved:63;
    };
};

typedef union hv_register_value {
    struct hv_u128 reg128;
    uint64_t reg64;
    uint32_t reg32;
    uint16_t reg16;
    uint8_t reg8;
    union hv_x64_fp_register fp;
    union hv_x64_fp_control_status_register fp_control_status;
    union hv_x64_xmm_control_status_register xmm_control_status;
    struct hv_x64_segment_register segment;
    struct hv_x64_table_register table;
    union hv_explicit_suspend_register explicit_suspend;
    union hv_intercept_suspend_register intercept_suspend;
    union hv_internal_activity_register internal_activity;
    union hv_x64_interrupt_state_register interrupt_state;
    union hv_x64_pending_interruption_register pending_interruption;
    union hv_x64_msr_npiep_config_contents npiep_config;
    union hv_x64_pending_exception_event pending_exception_event;
    union hv_x64_pending_virtualization_fault_event
        pending_virtualization_fault_event;
    union hv_x64_register_sev_control sev_control;
} hv_register_value;

/*
 * This struct is __packed in the kernel. Since all members are naturally
 * aligned, we can omit QEMU_PACKED to avoid address-of-packed-member warnings.
 */
typedef struct hv_register_assoc {
    uint32_t name;         /* enum hv_register_name */
    uint32_t reserved1;
    uint64_t reserved2;
    union hv_register_value value;
} hv_register_assoc;

union hv_input_vtl {
    uint8_t as_uint8;
    struct {
        uint8_t target_vtl:4;
        uint8_t use_target_vtl:1;
        uint8_t reserved_z:3;
    };
};

typedef struct hv_input_get_vp_registers {
    uint64_t partition_id;
    uint32_t vp_index;
    union hv_input_vtl input_vtl;
    uint8_t  rsvd_z8;
    uint16_t rsvd_z16;
    uint32_t names[];
} QEMU_PACKED hv_input_get_vp_registers;

typedef struct hv_input_set_vp_registers {
    uint64_t partition_id;
    uint32_t vp_index;
    union hv_input_vtl input_vtl;
    uint8_t  rsvd_z8;
    uint16_t rsvd_z16;
    struct hv_register_assoc elements[];
} QEMU_PACKED hv_input_set_vp_registers;

#define MSHV_VP_MAX_REGISTERS   128
enum hv_interrupt_type {
#if defined(__x86_64__)
    HV_X64_INTERRUPT_TYPE_FIXED             = 0x0000,
    HV_X64_INTERRUPT_TYPE_LOWESTPRIORITY    = 0x0001,
    HV_X64_INTERRUPT_TYPE_SMI               = 0x0002,
    HV_X64_INTERRUPT_TYPE_REMOTEREAD        = 0x0003,
    HV_X64_INTERRUPT_TYPE_NMI               = 0x0004,
    HV_X64_INTERRUPT_TYPE_INIT              = 0x0005,
    HV_X64_INTERRUPT_TYPE_SIPI              = 0x0006,
    HV_X64_INTERRUPT_TYPE_EXTINT            = 0x0007,
    HV_X64_INTERRUPT_TYPE_LOCALINT0         = 0x0008,
    HV_X64_INTERRUPT_TYPE_LOCALINT1         = 0x0009,
    HV_X64_INTERRUPT_TYPE_MAXIMUM           = 0x000A,
#elif defined(__aarch64__)
    HV_ARM64_INTERRUPT_TYPE_FIXED           = 0x0000,
    HV_ARM64_INTERRUPT_TYPE_MAXIMUM         = 0x0008,
#endif
};

union hv_interrupt_control {
    uint64_t as_uint64;
    struct {
        uint32_t interrupt_type; /* enum hv_interrupt type */
#if defined(__x86_64__)
        uint32_t level_triggered:1;
        uint32_t logical_dest_mode:1;
        uint32_t rsvd:30;
#elif defined(__aarch64__)
        uint32_t rsvd1:2;
        uint32_t asserted:1;
        uint32_t rsvd2:29;
#endif
    };
};

struct hv_input_assert_virtual_interrupt {
    uint64_t partition_id;
    union hv_interrupt_control control;
    uint64_t dest_addr; /* cpu's apic id */
    uint32_t vector;
    uint8_t target_vtl;
    uint8_t rsvd_z0;
    uint16_t rsvd_z1;
} QEMU_PACKED;

/* Flags for dirty mask of hv_vp_register_page */
enum hv_x64_register_class_type {
    HV_X64_REGISTER_CLASS_GENERAL = 0,
    HV_X64_REGISTER_CLASS_IP = 1,
    HV_X64_REGISTER_CLASS_XMM = 2,
    HV_X64_REGISTER_CLASS_SEGMENT = 3,
    HV_X64_REGISTER_CLASS_FLAGS = 4,
};

#define HV_VP_REGISTER_PAGE_MAX_VECTOR_COUNT  7

union hv_vp_register_page_interrupt_vectors {
    uint64_t as_uint64;
    struct {
        uint8_t vector_count;
        uint8_t vector[HV_VP_REGISTER_PAGE_MAX_VECTOR_COUNT];
    };
};

struct hv_vp_register_page {
    uint16_t version;
    uint8_t isvalid;
    uint8_t rsvdz;
    uint32_t dirty;

    union {
        struct {
            /* General purpose registers (HV_X64_REGISTER_CLASS_GENERAL) */
            union {
                struct {
                    uint64_t rax;
                    uint64_t rcx;
                    uint64_t rdx;
                    uint64_t rbx;
                    uint64_t rsp;
                    uint64_t rbp;
                    uint64_t rsi;
                    uint64_t rdi;
                    uint64_t r8;
                    uint64_t r9;
                    uint64_t r10;
                    uint64_t r11;
                    uint64_t r12;
                    uint64_t r13;
                    uint64_t r14;
                    uint64_t r15;
                } QEMU_PACKED;

                uint64_t gp_registers[16];
            };
            /* Instruction pointer (HV_X64_REGISTER_CLASS_IP) */
            uint64_t rip;
            /* Flags (HV_X64_REGISTER_CLASS_FLAGS) */
            uint64_t rflags;
        } QEMU_PACKED;

        uint64_t registers[18];
    };
    uint8_t reserved[8];
    /* Volatile XMM registers (HV_X64_REGISTER_CLASS_XMM) */
    union {
        struct {
            struct hv_u128 xmm0;
            struct hv_u128 xmm1;
            struct hv_u128 xmm2;
            struct hv_u128 xmm3;
            struct hv_u128 xmm4;
            struct hv_u128 xmm5;
        } QEMU_PACKED;

        struct hv_u128 xmm_registers[6];
    };
    /* Segment registers (HV_X64_REGISTER_CLASS_SEGMENT) */
    union {
        struct {
            struct hv_x64_segment_register es;
            struct hv_x64_segment_register cs;
            struct hv_x64_segment_register ss;
            struct hv_x64_segment_register ds;
            struct hv_x64_segment_register fs;
            struct hv_x64_segment_register gs;
        } QEMU_PACKED;

        struct hv_x64_segment_register segment_registers[6];
    };
    /* Misc. control registers (cannot be set via this interface) */
    uint64_t cr0;
    uint64_t cr3;
    uint64_t cr4;
    uint64_t cr8;
    uint64_t efer;
    uint64_t dr7;
    union hv_x64_pending_interruption_register pending_interruption;
    union hv_x64_interrupt_state_register interrupt_state;
    uint64_t instruction_emulation_hints;
    uint64_t xfem;

    uint8_t reserved1[0x100];

    /* Interrupts injected as part of HvCallDispatchVp. */
    union hv_vp_register_page_interrupt_vectors interrupt_vectors;
} QEMU_PACKED;

/* /dev/mshv */
#define MSHV_CREATE_PARTITION   _IOW(MSHV_IOCTL, 0x00, struct mshv_create_partition)
#define MSHV_CREATE_VP          _IOW(MSHV_IOCTL, 0x01, struct mshv_create_vp)

/* Partition fds created with MSHV_CREATE_PARTITION */
#define MSHV_INITIALIZE_PARTITION   _IO(MSHV_IOCTL, 0x00)
#define MSHV_SET_GUEST_MEMORY       _IOW(MSHV_IOCTL, 0x02, struct mshv_user_mem_region)
#define MSHV_IRQFD                  _IOW(MSHV_IOCTL, 0x03, struct mshv_user_irqfd)
#define MSHV_IOEVENTFD              _IOW(MSHV_IOCTL, 0x04, struct mshv_user_ioeventfd)
#define MSHV_SET_MSI_ROUTING        _IOW(MSHV_IOCTL, 0x05, struct mshv_user_irq_table)

/*
 ********************************
 * VP APIs for child partitions *
 ********************************
 */

/*
 * This struct is __packed in the kernel, but since all members are naturally
 * aligned, so we can omit QEMU_PACKED to avoid address-of-packed-member
 * warnings.
 */
struct hv_local_interrupt_controller_state {
    /* HV_X64_INTERRUPT_CONTROLLER_STATE */
    uint32_t apic_id;
    uint32_t apic_version;
    uint32_t apic_ldr;
    uint32_t apic_dfr;
    uint32_t apic_spurious;
    uint32_t apic_isr[8];
    uint32_t apic_tmr[8];
    uint32_t apic_irr[8];
    uint32_t apic_esr;
    uint32_t apic_icr_high;
    uint32_t apic_icr_low;
    uint32_t apic_lvt_timer;
    uint32_t apic_lvt_thermal;
    uint32_t apic_lvt_perfmon;
    uint32_t apic_lvt_lint0;
    uint32_t apic_lvt_lint1;
    uint32_t apic_lvt_error;
    uint32_t apic_lvt_cmci;
    uint32_t apic_error_status;
    uint32_t apic_initial_count;
    uint32_t apic_counter_value;
    uint32_t apic_divide_configuration;
    uint32_t apic_remote_read;
};

/* Generic hypercall */
#define MSHV_ROOT_HVCALL        _IOWR(MSHV_IOCTL, 0x07, struct mshv_root_hvcall)

/* From hvgdk_mini.h */

#define HV_X64_MSR_GUEST_OS_ID      0x40000000
#define HV_X64_MSR_SINT0            0x40000090
#define HV_X64_MSR_SINT1            0x40000091
#define HV_X64_MSR_SINT2            0x40000092
#define HV_X64_MSR_SINT3            0x40000093
#define HV_X64_MSR_SINT4            0x40000094
#define HV_X64_MSR_SINT5            0x40000095
#define HV_X64_MSR_SINT6            0x40000096
#define HV_X64_MSR_SINT7            0x40000097
#define HV_X64_MSR_SINT8            0x40000098
#define HV_X64_MSR_SINT9            0x40000099
#define HV_X64_MSR_SINT10           0x4000009A
#define HV_X64_MSR_SINT11           0x4000009B
#define HV_X64_MSR_SINT12           0x4000009C
#define HV_X64_MSR_SINT13           0x4000009D
#define HV_X64_MSR_SINT14           0x4000009E
#define HV_X64_MSR_SINT15           0x4000009F
#define HV_X64_MSR_SCONTROL         0x40000080
#define HV_X64_MSR_SIEFP            0x40000082
#define HV_X64_MSR_SIMP             0x40000083
#define HV_X64_MSR_REFERENCE_TSC    0x40000021
#define HV_X64_MSR_EOM              0x40000084

/* Define port identifier type. */
union hv_port_id {
    uint32_t asuint32_t;
    struct {
        uint32_t id:24;
        uint32_t reserved:8;
    };
};

#define HV_MESSAGE_SIZE                 (256)
#define HV_MESSAGE_PAYLOAD_BYTE_COUNT   (240)
#define HV_MESSAGE_PAYLOAD_QWORD_COUNT  (30)

/* Define hypervisor message types. */
enum hv_message_type {
    HVMSG_NONE                          = 0x00000000,

    /* Memory access messages. */
    HVMSG_UNMAPPED_GPA                  = 0x80000000,
    HVMSG_GPA_INTERCEPT                 = 0x80000001,
    HVMSG_UNACCEPTED_GPA                = 0x80000003,
    HVMSG_GPA_ATTRIBUTE_INTERCEPT       = 0x80000004,

    /* Timer notification messages. */
    HVMSG_TIMER_EXPIRED                 = 0x80000010,

    /* Error messages. */
    HVMSG_INVALID_VP_REGISTER_VALUE     = 0x80000020,
    HVMSG_UNRECOVERABLE_EXCEPTION       = 0x80000021,
    HVMSG_UNSUPPORTED_FEATURE           = 0x80000022,

    /*
     * Opaque intercept message. The original intercept message is only
     * accessible from the mapped intercept message page.
     */
    HVMSG_OPAQUE_INTERCEPT              = 0x8000003F,

    /* Trace buffer complete messages. */
    HVMSG_EVENTLOG_BUFFERCOMPLETE       = 0x80000040,

    /* Hypercall intercept */
    HVMSG_HYPERCALL_INTERCEPT           = 0x80000050,

    /* SynIC intercepts */
    HVMSG_SYNIC_EVENT_INTERCEPT         = 0x80000060,
    HVMSG_SYNIC_SINT_INTERCEPT          = 0x80000061,
    HVMSG_SYNIC_SINT_DELIVERABLE        = 0x80000062,

    /* Async call completion intercept */
    HVMSG_ASYNC_CALL_COMPLETION         = 0x80000070,

    /* Root scheduler messages */
    HVMSG_SCHEDULER_VP_SIGNAL_BITSE     = 0x80000100,
    HVMSG_SCHEDULER_VP_SIGNAL_PAIR      = 0x80000101,

    /* Platform-specific processor intercept messages. */
    HVMSG_X64_IO_PORT_INTERCEPT         = 0x80010000,
    HVMSG_X64_MSR_INTERCEPT             = 0x80010001,
    HVMSG_X64_CPUID_INTERCEPT           = 0x80010002,
    HVMSG_X64_EXCEPTION_INTERCEPT       = 0x80010003,
    HVMSG_X64_APIC_EOI                  = 0x80010004,
    HVMSG_X64_LEGACY_FP_ERROR           = 0x80010005,
    HVMSG_X64_IOMMU_PRQ                 = 0x80010006,
    HVMSG_X64_HALT                      = 0x80010007,
    HVMSG_X64_INTERRUPTION_DELIVERABLE  = 0x80010008,
    HVMSG_X64_SIPI_INTERCEPT            = 0x80010009,
    HVMSG_X64_SEV_VMGEXIT_INTERCEPT     = 0x80010013,
};

union hv_x64_vp_execution_state {
    uint16_t as_uint16;
    struct {
        uint16_t cpl:2;
        uint16_t cr0_pe:1;
        uint16_t cr0_am:1;
        uint16_t efer_lma:1;
        uint16_t debug_active:1;
        uint16_t interruption_pending:1;
        uint16_t vtl:4;
        uint16_t enclave_mode:1;
        uint16_t interrupt_shadow:1;
        uint16_t virtualization_fault_active:1;
        uint16_t reserved:2;
    };
};

/* From openvmm::hvdef */
enum hv_x64_intercept_access_type {
    HV_X64_INTERCEPT_ACCESS_TYPE_READ = 0,
    HV_X64_INTERCEPT_ACCESS_TYPE_WRITE = 1,
    HV_X64_INTERCEPT_ACCESS_TYPE_EXECUTE = 2,
};

struct hv_x64_intercept_message_header {
    uint32_t vp_index;
    uint8_t instruction_length:4;
    uint8_t cr8:4; /* Only set for exo partitions */
    uint8_t intercept_access_type;
    union hv_x64_vp_execution_state execution_state;
    struct hv_x64_segment_register cs_segment;
    uint64_t rip;
    uint64_t rflags;
} QEMU_PACKED;

union hv_x64_io_port_access_info {
    uint8_t as_uint8;
    struct {
        uint8_t access_size:3;
        uint8_t string_op:1;
        uint8_t rep_prefix:1;
        uint8_t reserved:3;
    };
};

typedef struct hv_x64_io_port_intercept_message {
    struct hv_x64_intercept_message_header header;
    uint16_t port_number;
    union hv_x64_io_port_access_info access_info;
    uint8_t instruction_byte_count;
    uint32_t reserved;
    uint64_t rax;
    uint8_t instruction_bytes[16];
    struct hv_x64_segment_register ds_segment;
    struct hv_x64_segment_register es_segment;
    uint64_t rcx;
    uint64_t rsi;
    uint64_t rdi;
} QEMU_PACKED hv_x64_io_port_intercept_message;

union hv_x64_memory_access_info {
    uint8_t as_uint8;
    struct {
        uint8_t gva_valid:1;
        uint8_t gva_gpa_valid:1;
        uint8_t hypercall_output_pending:1;
        uint8_t tlb_locked_no_overlay:1;
        uint8_t reserved:4;
    };
};

struct hv_x64_memory_intercept_message {
    struct hv_x64_intercept_message_header header;
    uint32_t cache_type; /* enum hv_cache_type */
    uint8_t instruction_byte_count;
    union hv_x64_memory_access_info memory_access_info;
    uint8_t tpr_priority;
    uint8_t reserved1;
    uint64_t guest_virtual_address;
    uint64_t guest_physical_address;
    uint8_t instruction_bytes[16];
} QEMU_PACKED;

union hv_arm64_vp_execution_state {
    uint16_t as_uint16;
    struct {
        uint16_t cpl:2;
        uint16_t debug_active:1;
        uint16_t interruption_pending:1;
        uint16_t vtl:4;
        uint16_t virtualization_fault_active:1;
        uint16_t reserved:7;
    };
};

struct hv_arm64_intercept_message_header {
    uint32_t vp_index;
    uint8_t instruction_length;
    uint8_t intercept_access_type;
    union hv_arm64_vp_execution_state execution_state;
    uint64_t pc;
    uint64_t cpsr;
};

union hv_arm64_memory_access_info {
    uint8_t as_uint8;
    struct {
        uint8_t gva_valid:1;
        uint8_t gva_gpa_valid:1;
        uint8_t hypercall_output_pending:1;
        uint8_t reserved:5;
    };
};

struct hv_arm64_memory_intercept_message {
    struct hv_arm64_intercept_message_header header;
    uint32_t cache_type; /* enum hv_cache_type */
    uint8_t instruction_byte_count;
    union hv_arm64_memory_access_info memory_access_info;
    uint16_t reserved1;
    uint8_t instruction_bytes[4];
    uint32_t reserved2;
    uint64_t guest_virtual_address;
    uint64_t guest_physical_address;
    uint64_t syndrome;
};

union hv_message_flags {
    uint8_t asu8;
    struct {
        uint8_t msg_pending:1;
        uint8_t reserved:7;
    };
};

struct hv_message_header {
    uint32_t message_type;
    uint8_t payload_size;
    union hv_message_flags message_flags;
    uint8_t reserved[2];
    union {
        uint64_t sender;
        union hv_port_id port;
    };
} QEMU_PACKED;

struct hv_message {
    struct hv_message_header header;
    union {
        uint64_t payload[HV_MESSAGE_PAYLOAD_QWORD_COUNT];
    } u;
} QEMU_PACKED;

/* From  github.com/rust-vmm/mshv-bindings/src/x86_64/regs.rs */

struct hv_cpuid_entry {
    uint32_t function;
    uint32_t index;
    uint32_t flags;
    uint32_t eax;
    uint32_t ebx;
    uint32_t ecx;
    uint32_t edx;
    uint32_t padding[3];
} QEMU_PACKED;

struct hv_cpuid {
    uint32_t nent;
    uint32_t padding;
    struct hv_cpuid_entry entries[0];
} QEMU_PACKED;

#define IA32_MSR_TSC            0x00000010
#define IA32_MSR_EFER           0xC0000080
#define IA32_MSR_KERNEL_GS_BASE 0xC0000102
#define IA32_MSR_APIC_BASE      0x0000001B
#define IA32_MSR_PAT            0x0277
#define IA32_MSR_SYSENTER_CS    0x00000174
#define IA32_MSR_SYSENTER_ESP   0x00000175
#define IA32_MSR_SYSENTER_EIP   0x00000176
#define IA32_MSR_STAR           0xC0000081
#define IA32_MSR_LSTAR          0xC0000082
#define IA32_MSR_CSTAR          0xC0000083
#define IA32_MSR_SFMASK         0xC0000084

#define IA32_MSR_MTRR_CAP       0x00FE
#define IA32_MSR_MTRR_DEF_TYPE  0x02FF
#define IA32_MSR_MTRR_PHYSBASE0 0x0200
#define IA32_MSR_MTRR_PHYSMASK0 0x0201
#define IA32_MSR_MTRR_PHYSBASE1 0x0202
#define IA32_MSR_MTRR_PHYSMASK1 0x0203
#define IA32_MSR_MTRR_PHYSBASE2 0x0204
#define IA32_MSR_MTRR_PHYSMASK2 0x0205
#define IA32_MSR_MTRR_PHYSBASE3 0x0206
#define IA32_MSR_MTRR_PHYSMASK3 0x0207
#define IA32_MSR_MTRR_PHYSBASE4 0x0208
#define IA32_MSR_MTRR_PHYSMASK4 0x0209
#define IA32_MSR_MTRR_PHYSBASE5 0x020A
#define IA32_MSR_MTRR_PHYSMASK5 0x020B
#define IA32_MSR_MTRR_PHYSBASE6 0x020C
#define IA32_MSR_MTRR_PHYSMASK6 0x020D
#define IA32_MSR_MTRR_PHYSBASE7 0x020E
#define IA32_MSR_MTRR_PHYSMASK7 0x020F

#define IA32_MSR_MTRR_FIX64K_00000 0x0250
#define IA32_MSR_MTRR_FIX16K_80000 0x0258
#define IA32_MSR_MTRR_FIX16K_A0000 0x0259
#define IA32_MSR_MTRR_FIX4K_C0000 0x0268
#define IA32_MSR_MTRR_FIX4K_C8000 0x0269
#define IA32_MSR_MTRR_FIX4K_D0000 0x026A
#define IA32_MSR_MTRR_FIX4K_D8000 0x026B
#define IA32_MSR_MTRR_FIX4K_E0000 0x026C
#define IA32_MSR_MTRR_FIX4K_E8000 0x026D
#define IA32_MSR_MTRR_FIX4K_F0000 0x026E
#define IA32_MSR_MTRR_FIX4K_F8000 0x026F

#define IA32_MSR_TSC_AUX          0xC0000103
#define IA32_MSR_BNDCFGS          0x00000d90
#define IA32_MSR_DEBUG_CTL        0x1D9
#define IA32_MSR_SPEC_CTRL        0x00000048
#define IA32_MSR_TSC_ADJUST       0x0000003b

#define IA32_MSR_MISC_ENABLE 0x000001a0

#define HV_TRANSLATE_GVA_VALIDATE_READ       (0x0001)
#define HV_TRANSLATE_GVA_VALIDATE_WRITE      (0x0002)
#define HV_TRANSLATE_GVA_VALIDATE_EXECUTE    (0x0004)

#define HV_HYP_PAGE_SHIFT       12
#define HV_HYP_PAGE_SIZE        BIT(HV_HYP_PAGE_SHIFT)
#define HV_HYP_PAGE_MASK        (~(HV_HYP_PAGE_SIZE - 1))

#define HV_ANY_VP               ((uint32_t)-1)
#define HV_VTL_ALL              0xF

#define HVCALL_GET_PARTITION_PROPERTY    0x0044
#define HVCALL_SET_PARTITION_PROPERTY    0x0045
#define HVCALL_GET_VP_REGISTERS          0x0050
#define HVCALL_SET_VP_REGISTERS          0x0051
#define HVCALL_TRANSLATE_VIRTUAL_ADDRESS 0x0052
#define HVCALL_REGISTER_INTERCEPT_RESULT 0x0091
#define HVCALL_ASSERT_VIRTUAL_INTERRUPT  0x0094

#endif /* HW_HYPERV_HVGDK_MINI_H */
