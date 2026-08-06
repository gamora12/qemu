/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * ARM Generic Interrupt Controller using MSHV platform support stub
 *
 * Copyright Microsoft, Corp. 2026
 *
 */
#include "qemu/osdep.h"
#include "hw/intc/arm_gicv3_common.h"
#include "migration/vmstate.h"
#include "qemu/typedefs.h"

static bool needed_never(void *opaque)
{
    return false;
}

const VMStateDescription vmstate_gicv3_mshv = {
    .name = "arm_gicv3/mshv_gic_state",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = needed_never,
};
