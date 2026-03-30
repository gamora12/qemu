/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef HELPER__H
#define HELPER__H

#include <stdint.h>
#include <stdbool.h>
#include "exec/helper-proto-common.h"
#include "exec/helper-gen-common.h"
#include "target/arm/syndrome.h"

int arm_emulate_mmio(CPUState *cpu, EsrEl2 syndrome, uint64_t gpa);

#define HELPER_H "tcg/helper-defs.h"
#include "exec/helper-proto.h.inc"
#include "exec/helper-gen.h.inc"
#undef HELPER_H

#endif /* HELPER__H */
