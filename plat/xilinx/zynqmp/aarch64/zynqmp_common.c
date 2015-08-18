/*
 * Copyright (c) 2013-2014, ARM Limited and Contributors. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 *
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * Neither the name of ARM nor the names of its contributors may be used
 * to endorse or promote products derived from this software without specific
 * prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <arch.h>
#include <arch_helpers.h>
#include <arm_config.h>
#include <arm_gic.h>
#include <bl_common.h>
#include <cci.h>
#include <debug.h>
#include <mmio.h>
#include <platform.h>
#include <xlat_tables.h>
#include <plat_arm.h>
#include "../zynqmp_def.h"
#include "../zynqmp_private.h"

/*******************************************************************************
 * plat_config holds the characteristics of the differences between the three
 * ZYNQMP platforms (Base, A53_A57 & Foundation). It will be populated during cold
 * boot at each boot stage by the primary before enabling the MMU (to allow cci
 * configuration) & used thereafter. Each BL will have its own copy to allow
 * independent operation.
 ******************************************************************************/
arm_config_t arm_config;

/*
 * Table of regions to map using the MMU.
 * This doesn't include TZRAM as the 'mem_layout' argument passed to
 * configure_mmu_elx() will give the available subset of that,
 */
const mmap_region_t plat_arm_mmap[] = {
	{ ZYNQMP_SHARED_RAM_BASE,	ZYNQMP_SHARED_RAM_BASE,	ZYNQMP_SHARED_RAM_SIZE,
						MT_MEMORY | MT_RW | MT_SECURE },
	{ ZYNQMP_TRUSTED_DRAM_BASE, ZYNQMP_TRUSTED_DRAM_BASE,	ZYNQMP_TRUSTED_DRAM_SIZE,
						MT_MEMORY | MT_RW | MT_SECURE },
	{ DEVICE0_BASE,	DEVICE0_BASE,	DEVICE0_SIZE,
						MT_DEVICE | MT_RW | MT_SECURE },
	{ DEVICE1_BASE,	DEVICE1_BASE,	DEVICE1_SIZE,
						MT_DEVICE | MT_RW | MT_SECURE },
	{ APB_BASE,	APB_BASE,	APB_SIZE,
						MT_DEVICE | MT_RW | MT_SECURE },
	{ DRAM1_BASE,	DRAM1_BASE,	DRAM1_SIZE,
						MT_MEMORY | MT_RW | MT_NS },
	{0}
};

/* Array of secure interrupts to be configured by the gic driver */
const unsigned int irq_sec_array[] = {
	ARM_IRQ_SEC_PHY_TIMER,
	IRQ_SEC_IPI_APU,
	ARM_IRQ_SEC_SGI_0,
	ARM_IRQ_SEC_SGI_1,
	ARM_IRQ_SEC_SGI_2,
	ARM_IRQ_SEC_SGI_3,
	ARM_IRQ_SEC_SGI_4,
	ARM_IRQ_SEC_SGI_5,
	ARM_IRQ_SEC_SGI_6,
	ARM_IRQ_SEC_SGI_7
};

#define ZYNQMP_SILICON_VER_MASK   0xF000
#define ZYNQMP_SILICON_VER_SHIFT  12
#define ZYNQMP_CSU_VERSION_SILICON      0x0
#define ZYNQMP_CSU_VERSION_EP108        0x1
#define ZYNQMP_CSU_VERSION_VELOCE       0x2
#define ZYNQMP_CSU_VERSION_QEMU         0x3

#define ZYNQMP_RTL_VER_MASK   0xFF0
#define ZYNQMP_RTL_VER_SHIFT  4

#define ZYNQMP_CSU_BASEADDR		0xFFCA0000
#define ZYNQMP_CSU_VERSION_OFFSET	0x44

static unsigned int zynqmp_get_silicon_ver(void)
{
	uint32_t ver;

	ver = mmio_read_32(ZYNQMP_CSU_BASEADDR + ZYNQMP_CSU_VERSION_OFFSET);
	ver &= ZYNQMP_SILICON_VER_MASK;
	ver >>= ZYNQMP_SILICON_VER_SHIFT;

	return ver;
}

static uint32_t zynqmp_get_rtl_ver(void)
{
	uint32_t ver;

	ver = mmio_read_32(ZYNQMP_CSU_BASEADDR + ZYNQMP_CSU_VERSION_OFFSET);
	ver &= ZYNQMP_RTL_VER_MASK;
	ver >>= ZYNQMP_RTL_VER_SHIFT;

	return ver;
}

uint32_t zynqmp_get_uart_clk(void)
{
	uint32_t ver = zynqmp_get_silicon_ver();

	switch (ver) {
	case ZYNQMP_CSU_VERSION_VELOCE:
		return 48000;
	case ZYNQMP_CSU_VERSION_EP108:
		return 25000000;
	case ZYNQMP_CSU_VERSION_QEMU:
		return 133000000;
	}

	return 0;
}

static unsigned int zynqmp_get_silicon_freq(void)
{
	uint32_t ver = zynqmp_get_silicon_ver();

	switch (ver) {
	case ZYNQMP_CSU_VERSION_VELOCE:
		return 10000;
	case ZYNQMP_CSU_VERSION_EP108:
		return 4000000;
	case ZYNQMP_CSU_VERSION_QEMU:
		return 50000000;
	}

	return 0;
}

static void zynqmp_print_platform_name(void)
{
	uint32_t ver = zynqmp_get_silicon_ver();
	uint32_t rtl = zynqmp_get_rtl_ver();
	char *label = "Uknown";

	switch (ver) {
	case ZYNQMP_CSU_VERSION_VELOCE:
		label = "VELOCE";
		break;
	case ZYNQMP_CSU_VERSION_EP108:
		label = "EP108";
		break;
	case ZYNQMP_CSU_VERSION_QEMU:
		label = "QEMU";
		break;
	}

	NOTICE("ATF running on %s/RTL%d.%d%s\n", label,
	       (rtl & 0xf0) >> 4, rtl & 0xf,
	       zynqmp_is_pmu_up() ? ", with PMU firmware" : "");
}

#define PMU_GLOBAL_CNTRL	0xFFD80000
#define FW_IS_PRESENT		(1 << 4)

/*
 * zynqmp_is_pmu_up - Find if PMU firmware is up and running
 *
 * Return 0 if firmware is not available, non 0 otherwise
 */
uint32_t zynqmp_is_pmu_up(void)
{
	uint32_t ver;

	ver = mmio_read_32(PMU_GLOBAL_CNTRL);
	ver &= FW_IS_PRESENT;

	return ver;
}

#define ZYNQMP_CRL_APB_BASEADDR				0xFF5E0000
#define ZYNQMP_CRL_APB_TIMESTAMP_REF_CTRL_OFFSET	0x128
#define ZYNQMP_CRL_APB_TIMESTAMP_REF_CTRL_CLKACT_BIT	(1 << 24)

#define ZYNQMP_IOU_SCNTRS_BASEADDR		0xFF260000
#define ZYNQMP_IOU_SCNTRS_CONTROL_OFFSET	0x0
#define ZYNQMP_IOU_SCNTRS_CONTROL_EN		(1 << 0)
#define ZYNQMP_IOU_SCNTRS_BASEFREQ_OFFSET	0x20
/*******************************************************************************
 * A single boot loader stack is expected to work on both the Foundation ZYNQMP
 * models and the two flavours of the Base ZYNQMP models (AEMv8 & Cortex). The
 * SYS_ID register provides a mechanism for detecting the differences between
 * these platforms. This information is stored in a per-BL array to allow the
 * code to take the correct path.Per BL platform configuration.
 ******************************************************************************/
int zynqmp_config_setup(void)
{
	uint32_t val;

	arm_config.gicd_base = BASE_GICD_BASE;
	arm_config.gicc_base = BASE_GICC_BASE;
	arm_config.gich_base = BASE_GICH_BASE;
	arm_config.gicv_base = BASE_GICV_BASE;

	zynqmp_print_platform_name();

	/* Global timer init - Program time stamp reference clk */
	val = mmio_read_32(ZYNQMP_CRL_APB_BASEADDR +
			   ZYNQMP_CRL_APB_TIMESTAMP_REF_CTRL_OFFSET);
	val |= ZYNQMP_CRL_APB_TIMESTAMP_REF_CTRL_CLKACT_BIT;
	mmio_write_32(ZYNQMP_CRL_APB_BASEADDR +
		      ZYNQMP_CRL_APB_TIMESTAMP_REF_CTRL_OFFSET, val);

	/* Program freq register in System counter  and enable system counter. */
	mmio_write_32(ZYNQMP_IOU_SCNTRS_BASEADDR +
		      ZYNQMP_IOU_SCNTRS_BASEFREQ_OFFSET,
		      zynqmp_get_silicon_freq());
	mmio_write_32(ZYNQMP_IOU_SCNTRS_BASEADDR +
	              ZYNQMP_IOU_SCNTRS_CONTROL_OFFSET,
		      ZYNQMP_IOU_SCNTRS_CONTROL_EN);

	arm_config.flags |= ARM_CONFIG_HAS_CCI;

	return 0;
}

uint64_t plat_get_syscnt_freq(void)
{
	uint64_t counter_base_frequency;

	/* FIXME: Read the frequency from Frequency modes table */
	counter_base_frequency = zynqmp_get_silicon_freq();

	return counter_base_frequency;
}

void zynqmp_cci_init(void)
{
	/*
	 * Initialize CCI-400 driver
	 */
	if (arm_config.flags & ARM_CONFIG_HAS_CCI)
		arm_cci_init();
}

void zynqmp_cci_enable(void)
{
	if (arm_config.flags & ARM_CONFIG_HAS_CCI)
		cci_enable_snoop_dvm_reqs(MPIDR_AFFLVL1_VAL(read_mpidr()));
}

void fvp_cci_disable(void)
{
	if (arm_config.flags & ARM_CONFIG_HAS_CCI)
		cci_disable_snoop_dvm_reqs(MPIDR_AFFLVL1_VAL(read_mpidr()));
}

void plat_arm_gic_init(void)
{
	arm_gic_init(arm_config.gicc_base,
		arm_config.gicd_base,
		1,
		irq_sec_array,
		ARRAY_SIZE(irq_sec_array));
}
