/*
 * QEMU MSHV support
 *
 * Copyright Microsoft, Corp. 2025
 *
 * Authors: 
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 */

#ifndef QEMU_MSHV_ARM_H
#define QEMU_MSHV_ARM_H

#include "target/arm/cpu.h"


void mshv_arm_set_cpu_features_from_host(ARMCPU *cpu);

#endif
