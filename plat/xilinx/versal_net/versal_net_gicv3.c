/*
 * Copyright (c) 2018-2019, ARM Limited and Contributors. All rights reserved.
 * Copyright (c) 2018-2022, Xilinx, Inc. All rights reserved.
 * Copyright (C) 2022, Advanced Micro Devices, Inc. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <common/debug.h>
#include <common/interrupt_props.h>
#include <drivers/arm/gicv3.h>
#include <lib/utils.h>
#include <plat/common/platform.h>

#include <plat_private.h>
#include <platform_def.h>

/******************************************************************************
 * The following functions are defined as weak to allow a platform to override
 * the way the GICv3 driver is initialised and used.
 *****************************************************************************/
#pragma weak plat_versal_net_gic_driver_init
#pragma weak plat_versal_net_gic_init
#pragma weak plat_versal_net_gic_cpuif_enable
#pragma weak plat_versal_net_gic_pcpu_init

/* The GICv3 driver only needs to be initialized in EL3 */
static uintptr_t rdistif_base_addrs[PLATFORM_CORE_COUNT];

static const uintptr_t gicr_base_addrs[2] = {
	PLAT_VERSAL_NET_GICR_BASE,	/* GICR Base address of the primary CPU */
	0U				/* Zero Termination */
};

/* List of zero terminated GICR frame addresses which CPUs will probe */
static const uintptr_t *gicr_frames;

static const interrupt_prop_t versal_net_interrupt_props[] = {
	PLAT_VERSAL_NET_G1S_IRQ_PROPS(INTR_GROUP1S),
	PLAT_VERSAL_NET_G0_IRQ_PROPS(INTR_GROUP0)
};

/*
 * MPIDR hashing function for translating MPIDRs read from GICR_TYPER register
 * to core position.
 *
 * Calculating core position is dependent on MPIDR_EL1.MT bit. However, affinity
 * values read from GICR_TYPER don't have an MT field. To reuse the same
 * translation used for CPUs, we insert MT bit read from the PE's MPIDR into
 * that read from GICR_TYPER.
 *
 * Assumptions:
 *
 *   - All CPUs implemented in the system have MPIDR_EL1.MT bit set;
 *   - No CPUs implemented in the system use affinity level 3.
 */
static uint32_t versal_net_gicv3_mpidr_hash(u_register_t mpidr)
{
	mpidr |= (read_mpidr_el1() & MPIDR_MT_MASK);
	return plat_core_pos_by_mpidr(mpidr);
}

static const gicv3_driver_data_t versal_net_gic_data __unused = {
	.gicd_base = PLAT_VERSAL_NET_GICD_BASE,
	.gicr_base = 0U,
	.interrupt_props = versal_net_interrupt_props,
	.interrupt_props_num = ARRAY_SIZE(versal_net_interrupt_props),
	.rdistif_num = PLATFORM_CORE_COUNT,
	.rdistif_base_addrs = rdistif_base_addrs,
	.mpidr_to_core_pos = versal_net_gicv3_mpidr_hash
};

void __init plat_versal_net_gic_driver_init(void)
{
	/*
	 * The GICv3 driver is initialized in EL3 and does not need
	 * to be initialized again in SEL1. This is because the S-EL1
	 * can use GIC system registers to manage interrupts and does
	 * not need GIC interface base addresses to be configured.
	 */
#if IMAGE_BL31
	gicv3_driver_init(&versal_net_gic_data);
	gicr_frames = gicr_base_addrs;

	if (gicv3_rdistif_probe(gicr_frames[0]) == -1) {
		ERROR("No GICR base frame found for Primary CPU\n");
		panic();
	}
#endif
}

/******************************************************************************
 * Versal NET common helper to initialize the GIC. Only invoked by BL31
 *****************************************************************************/
void __init plat_versal_net_gic_init(void)
{
	gicv3_distif_init();
	gicv3_rdistif_init(plat_my_core_pos());
	gicv3_cpuif_enable(plat_my_core_pos());
}

/******************************************************************************
 * Versal NET common helper to enable the GIC CPU interface
 *****************************************************************************/
void plat_versal_net_gic_cpuif_enable(void)
{
	gicv3_cpuif_enable(plat_my_core_pos());
}

/******************************************************************************
 * Versal NET common helper to initialize the per-cpu redistributor interface in
 * GICv3
 *****************************************************************************/
void plat_versal_net_gic_pcpu_init(void)
{
	int32_t result;
	const uintptr_t *plat_gicr_frames = gicr_frames;

	do {
		result = gicv3_rdistif_probe(*plat_gicr_frames);

		/* If the probe is successful, no need to proceed further */
		if (result == 0) {
			break;
		}

		plat_gicr_frames++;
	} while (*plat_gicr_frames != 0U);

	if (result == -1) {
		ERROR("No GICR base frame found for CPU 0x%lx\n", read_mpidr());
		panic();
	}

	gicv3_rdistif_init(plat_my_core_pos());
}
