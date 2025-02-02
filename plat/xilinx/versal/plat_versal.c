/*
 * Copyright (c) 2018-2022, ARM Limited and Contributors. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <plat_private.h>
#include <plat/common/platform.h>

int plat_core_pos_by_mpidr(u_register_t mpidr)
{
	if ((mpidr & MPIDR_CLUSTER_MASK) != 0U)  {
		return -1;
	}

	if ((mpidr & MPIDR_CPU_MASK) >= PLATFORM_CORE_COUNT) {
		return -1;
	}

	return (int)versal_calc_core_pos(mpidr);
}
