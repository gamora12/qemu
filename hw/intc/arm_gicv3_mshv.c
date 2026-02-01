/*
 * ARM Generic Interrupt Controller using MSHV in-kernel support
 *
 * Copyright Microsoft, Corp. 2026
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
#include "hw/intc/arm_gicv3_common.h"
#include "migration/blocker.h"
#include "target/arm/cpregs.h"
#include "hw/hyperv/hvgdk_mini.h"
#include "system/mshv.h"
#include "system/mshv_int.h"

struct MSHVARMGICv3Class {
    ARMGICv3CommonClass parent_class;
    DeviceRealize parent_realize;
    ResettablePhases parent_phases;
};

OBJECT_DECLARE_TYPE(GICv3State, MSHVARMGICv3Class, MSHV_GICV3)

static void mshv_gicv3_get(GICv3State *s)
{
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
    union hv_interrupt_control control = {
        .interrupt_type = HV_ARM64_INTERRUPT_TYPE_FIXED,
        .rsvd1 = 0,
        .asserted = level,
        .rsvd2 = 0
    };

    if (irq > s->num_irq) {
        return;
    }

    struct hv_input_assert_virtual_interrupt arg = {0};
    arg.control = control;
    arg.vector = GIC_INTERNAL + irq;

    struct mshv_root_hvcall args = {0};
    args.code   = HVCALL_ASSERT_VIRTUAL_INTERRUPT;
    args.in_sz  = sizeof(arg);
    args.in_ptr = (uint64_t)&arg;

    ret = mshv_hvcall(vm_fd, &args);
    if (ret < 0) {
        error_report("Failed to set GICv3 IRQ %d to level %d", irq, level);
    }
}

static void mshv_set_reg(CPUState *cpu, hv_register_assoc *reg)
{
    int ret;
    int cpu_fd = mshv_vcpufd(cpu);
    hv_input_set_vp_registers *in = cpu->accel->hvcall_args.input_page;
    size_t in_sz = sizeof(hv_input_set_vp_registers) + sizeof(hv_register_assoc);
    struct mshv_root_hvcall args = {0};

    /* fill the input struct */
    memset(in, 0, sizeof(hv_input_set_vp_registers));
    in->vp_index = cpu->cpu_index;
    memcpy(in->elements, reg, sizeof(hv_register_assoc));

    /* create the hvcall envelope */
    args.code = HVCALL_SET_VP_REGISTERS;
    args.in_sz = in_sz;
    args.in_ptr = (uint64_t) in;
    args.reps = (uint16_t) 1;

    /* perform the call */
    ret = mshv_hvcall(cpu_fd, &args);
    if (ret < 0) {
        error_report("Failed to set register 0x%08x", reg->name);
        return;
    }
}

static void mshv_gicv3_realize(DeviceState *dev, Error **errp)
{
    ERRP_GUARD();
    GICv3State *s = MSHV_GICV3(dev);
    MSHVARMGICv3Class *mgc = MSHV_GICV3_GET_CLASS(s);
    int i;

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

        mshv_set_reg(cpu_state, &gicr_base);
    }

    if (s->maint_irq) {
        error_setg(errp, "Nested virtualisation not currently supported by MSHV");
        return;
    }

    error_setg(&s->migration_blocker,
        "Live migration disabled because GIC state save/restore not supported on MSHV");
    if (migrate_add_blocker(&s->migration_blocker, errp) < 0) {
        error_report_err(*errp);
    }
}

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
    .instance_size = sizeof(GICv3State),
    .class_init = mshv_gicv3_class_init,
    .class_size = sizeof(MSHVARMGICv3Class),
};

static void mshv_gicv3_register_types(void)
{
    type_register_static(&mshv_arm_gicv3_info);
}

type_init(mshv_gicv3_register_types)
