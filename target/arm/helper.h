/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef HELPER__H

#define HELPER__H

#include "exec/helper-proto-common.h"
#include "exec/helper-gen-common.h"

#include <stdint.h>
#include <stdbool.h>

typedef struct AccelSyndromeOps {
	int (*load_regs)(CPUState *cpu);
	int (*store_regs)(CPUState *cpu);
	int (*mem_read)(uint64_t gpa, void *data, uint64_t len, bool atomic, bool debug);
	int (*mem_write)(uint64_t gpa, const void *data, uint64_t len, bool atomic);
	uint64_t (*get_reg)(CPUState *cpu, int reg_index);
	void (*set_reg)(CPUState *cpu, int reg_index, uint64_t value);
} AccelSyndromeOps;

int accel_emulate_with_syndrome(CPUState *cpu, uint64_t syndrome, uint64_t gpa,
								AccelSyndromeOps *ops);

#define HELPER_H "tcg/helper-defs.h"
#include "exec/helper-proto.h.inc"
#include "exec/helper-gen.h.inc"
#undef HELPER_H

#endif /* HELPER__H */
