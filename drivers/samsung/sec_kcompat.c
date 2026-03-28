// SPDX-License-Identifier: GPL-2.0
/*
 * drivers/samsung/sec_kcompat.c
 *
 * COPYRIGHT(C) 2019 Samsung Electronics Co., Ltd. All Right Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#define pr_fmt(fmt)     KBUILD_MODNAME ":%s() " fmt, __func__

#include <linux/device.h>
#include <linux/bitmap.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/sec_param.h>

#if defined(CONFIG_MSM_SMEM)
#include <soc/qcom/smem.h>
#endif

#include "sec_kcompat.h"

#if LINUX_VERSION_CODE < KERNEL_VERSION(4,14,0)
#if defined(CONFIG_MSM_SMEM)
void * __weak qcom_smem_get(unsigned host, unsigned item, size_t *size)
{
	void * ret;
	unsigned int size_tmp = 0;

	ret = smem_get_entry(item, &size_tmp, SMEM_APPS, host);
	*size = (size_t)size_tmp;

	return ret;
}

phys_addr_t __weak qcom_smem_virt_to_phys(void *p)
{
	return smem_virt_to_phys(p) & 0xFFFFFFFFULL;
}
#endif /* CONFIG_MSM_SMEM */
#endif /* KERNEL_VERSION(4,14,0) */

#if LINUX_VERSION_CODE < KERNEL_VERSION(4,19,0)
unsigned long * __weak bitmap_alloc(unsigned int nbits, gfp_t flags)
{
	return kmalloc_array(BITS_TO_LONGS(nbits), sizeof(unsigned long),
			flags);
}

unsigned long * __weak bitmap_zalloc(unsigned int nbits, gfp_t flags)
{
	return bitmap_alloc(nbits, flags | __GFP_ZERO);
}
#endif /* KERNEL_VERSION(4,19,0) */

unsigned int __weak is_boot_recovery(void)
{
	return 0;
}

unsigned int __weak system_rev;
struct class * __weak camera_class;

int __weak dwc3_msm_is_suspended(void)
{
	return 1;
}

int __weak dwc3_msm_is_host_highspeed(void)
{
	return 0;
}

#if !defined(CONFIG_SEC_PARAM)
bool __weak sec_get_param(enum sec_param_index index, void *value)
{
	return false;
}

bool __weak sec_set_param(enum sec_param_index index, void *value)
{
	return false;
}
#endif

void __weak set_dload_mode(int on)
{
}

void __weak emerg_pet_watchdog(void)
{
}

void __weak sec_log_buf_pull_early_buffer(bool *init_done)
{
	if (init_done)
		*init_done = true;
}

int __weak qpnp_control_s2_reset_onoff(int on)
{
	return 0;
}

#if !defined(CONFIG_SEC_SMEM)
char * __weak get_ddr_vendor_name(void)
{
	return "NA";
}

u32 __weak get_ddr_DSF_version(void)
{
	return 0;
}

u8 __weak get_ddr_revision_id_1(void)
{
	return 0;
}

u8 __weak get_ddr_revision_id_2(void)
{
	return 0;
}

u8 __weak get_ddr_total_density(void)
{
	return 0;
}

u8 __weak get_ddr_rcw_tDQSCK(u32 ch, u32 cs, u32 dq)
{
	return 0;
}

u8 __weak get_ddr_wr_coarseCDC(u32 ch, u32 cs, u32 dq)
{
	return 0;
}

u8 __weak get_ddr_wr_fineCDC(u32 ch, u32 cs, u32 dq)
{
	return 0;
}

u8 __weak ddr_get_wr_pr_width(u32 ch, u32 cs, u32 dq)
{
	return 0;
}

u8 __weak ddr_get_wr_min_eye_height(u32 ch, u32 cs)
{
	return 0;
}

u8 __weak ddr_get_wr_best_vref(u32 ch, u32 cs)
{
	return 0;
}

u8 __weak ddr_get_wr_vmax_to_vmid(u32 ch, u32 cs, u32 dq)
{
	return 0;
}

u8 __weak ddr_get_wr_vmid_to_vmin(u32 ch, u32 cs, u32 dq)
{
	return 0;
}

u8 __weak ddr_get_dqs_dcc_adj(u32 ch, u32 dq)
{
	return 0;
}

u8 __weak ddr_get_rd_pr_width(u32 ch, u32 cs, u32 dq)
{
	return 0;
}

u8 __weak ddr_get_rd_min_eye_height(u32 ch, u32 cs)
{
	return 0;
}

u8 __weak ddr_get_rd_best_vref(u32 ch, u32 cs, u32 dq)
{
	return 0;
}

u8 __weak ddr_get_dq_dcc_abs(u32 ch, u32 cs, u32 dq)
{
	return 0;
}

u16 __weak ddr_get_small_eye_detected(void)
{
	return 0;
}
#endif
