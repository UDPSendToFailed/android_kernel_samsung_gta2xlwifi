/**
 * debugfs.c - DesignWare USB3 DRD Controller DebugFS file
 *
 * Copyright (C) 2010-2011 Texas Instruments Incorporated - http://www.ti.com
 *
 * Authors: Felipe Balbi <balbi@ti.com>,
 *	    Sebastian Andrzej Siewior <bigeasy@linutronix.de>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2  of
 * the License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/ptrace.h>
#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/delay.h>
#include <linux/uaccess.h>

#include <linux/usb/ch9.h>
#include <linux/ipc_logging.h>

#include "core.h"
#include "gadget.h"
#include "io.h"
#include "debug.h"

#define dump_register(nm)				\
{							\
	.name	= __stringify(nm),			\
	.offset	= DWC3_ ##nm - DWC3_GLOBALS_REGS_START,	\
}

#define ep_event_rate(ev, c, p, dt)	\
	((dt) ? ((c.ev - p.ev) * (MSEC_PER_SEC)) / (dt) : 0)

static const struct debugfs_reg32 dwc3_regs[] = {
	dump_register(GSBUSCFG0),
	dump_register(GSBUSCFG1),
	dump_register(GTXTHRCFG),
	dump_register(GRXTHRCFG),
	dump_register(GCTL),
	dump_register(GEVTEN),
	dump_register(GSTS),
	dump_register(GSNPSID),
	dump_register(GGPIO),
	dump_register(GUID),
	dump_register(GUCTL),
	dump_register(GBUSERRADDR0),
	dump_register(GBUSERRADDR1),
	dump_register(GPRTBIMAP0),
	dump_register(GPRTBIMAP1),
	dump_register(GHWPARAMS0),
	dump_register(GHWPARAMS1),
	dump_register(GHWPARAMS2),
	dump_register(GHWPARAMS3),
	dump_register(GHWPARAMS4),
	dump_register(GHWPARAMS5),
	dump_register(GHWPARAMS6),
	dump_register(GHWPARAMS7),
	dump_register(GDBGFIFOSPACE),
	dump_register(GDBGLTSSM),
	dump_register(GPRTBIMAP_HS0),
	dump_register(GPRTBIMAP_HS1),
	dump_register(GPRTBIMAP_FS0),
	dump_register(GPRTBIMAP_FS1),

	dump_register(GUSB2PHYCFG(0)),
	dump_register(GUSB2PHYCFG(1)),
	dump_register(GUSB2PHYCFG(2)),
	dump_register(GUSB2PHYCFG(3)),
	dump_register(GUSB2PHYCFG(4)),
	dump_register(GUSB2PHYCFG(5)),
	dump_register(GUSB2PHYCFG(6)),
	dump_register(GUSB2PHYCFG(7)),
	dump_register(GUSB2PHYCFG(8)),
	dump_register(GUSB2PHYCFG(9)),
	dump_register(GUSB2PHYCFG(10)),
	dump_register(GUSB2PHYCFG(11)),
	dump_register(GUSB2PHYCFG(12)),
	dump_register(GUSB2PHYCFG(13)),
	dump_register(GUSB2PHYCFG(14)),
	dump_register(GUSB2PHYCFG(15)),

	dump_register(GUSB2I2CCTL(0)),
	dump_register(GUSB2I2CCTL(1)),
	dump_register(GUSB2I2CCTL(2)),
	dump_register(GUSB2I2CCTL(3)),
	dump_register(GUSB2I2CCTL(4)),
	dump_register(GUSB2I2CCTL(5)),
	dump_register(GUSB2I2CCTL(6)),
	dump_register(GUSB2I2CCTL(7)),
	dump_register(GUSB2I2CCTL(8)),
	dump_register(GUSB2I2CCTL(9)),
	dump_register(GUSB2I2CCTL(10)),
	dump_register(GUSB2I2CCTL(11)),
	dump_register(GUSB2I2CCTL(12)),
	dump_register(GUSB2I2CCTL(13)),
	dump_register(GUSB2I2CCTL(14)),
	dump_register(GUSB2I2CCTL(15)),

	dump_register(GUSB2PHYACC(0)),
	dump_register(GUSB2PHYACC(1)),
	dump_register(GUSB2PHYACC(2)),
	dump_register(GUSB2PHYACC(3)),
	dump_register(GUSB2PHYACC(4)),
	dump_register(GUSB2PHYACC(5)),
	dump_register(GUSB2PHYACC(6)),
	dump_register(GUSB2PHYACC(7)),
	dump_register(GUSB2PHYACC(8)),
	dump_register(GUSB2PHYACC(9)),
	dump_register(GUSB2PHYACC(10)),
	dump_register(GUSB2PHYACC(11)),
	dump_register(GUSB2PHYACC(12)),
	dump_register(GUSB2PHYACC(13)),
	dump_register(GUSB2PHYACC(14)),
	dump_register(GUSB2PHYACC(15)),

	dump_register(GUSB3PIPECTL(0)),
	dump_register(GUSB3PIPECTL(1)),
	dump_register(GUSB3PIPECTL(2)),
	dump_register(GUSB3PIPECTL(3)),
	dump_register(GUSB3PIPECTL(4)),
	dump_register(GUSB3PIPECTL(5)),
	dump_register(GUSB3PIPECTL(6)),
	dump_register(GUSB3PIPECTL(7)),
	dump_register(GUSB3PIPECTL(8)),
	dump_register(GUSB3PIPECTL(9)),
	dump_register(GUSB3PIPECTL(10)),
	dump_register(GUSB3PIPECTL(11)),
	dump_register(GUSB3PIPECTL(12)),
	dump_register(GUSB3PIPECTL(13)),
	dump_register(GUSB3PIPECTL(14)),
	dump_register(GUSB3PIPECTL(15)),

	dump_register(GTXFIFOSIZ(0)),
	dump_register(GTXFIFOSIZ(1)),
	dump_register(GTXFIFOSIZ(2)),
	dump_register(GTXFIFOSIZ(3)),
	dump_register(GTXFIFOSIZ(4)),
	dump_register(GTXFIFOSIZ(5)),
	dump_register(GTXFIFOSIZ(6)),
	dump_register(GTXFIFOSIZ(7)),
	dump_register(GTXFIFOSIZ(8)),
	dump_register(GTXFIFOSIZ(9)),
	dump_register(GTXFIFOSIZ(10)),
	dump_register(GTXFIFOSIZ(11)),
	dump_register(GTXFIFOSIZ(12)),
	dump_register(GTXFIFOSIZ(13)),
	dump_register(GTXFIFOSIZ(14)),
	dump_register(GTXFIFOSIZ(15)),
	dump_register(GTXFIFOSIZ(16)),
	dump_register(GTXFIFOSIZ(17)),
	dump_register(GTXFIFOSIZ(18)),
	dump_register(GTXFIFOSIZ(19)),
	dump_register(GTXFIFOSIZ(20)),
	dump_register(GTXFIFOSIZ(21)),
	dump_register(GTXFIFOSIZ(22)),
	dump_register(GTXFIFOSIZ(23)),
	dump_register(GTXFIFOSIZ(24)),
	dump_register(GTXFIFOSIZ(25)),
	dump_register(GTXFIFOSIZ(26)),
	dump_register(GTXFIFOSIZ(27)),
	dump_register(GTXFIFOSIZ(28)),
	dump_register(GTXFIFOSIZ(29)),
	dump_register(GTXFIFOSIZ(30)),
	dump_register(GTXFIFOSIZ(31)),

	dump_register(GRXFIFOSIZ(0)),
	dump_register(GRXFIFOSIZ(1)),
	dump_register(GRXFIFOSIZ(2)),
	dump_register(GRXFIFOSIZ(3)),
	dump_register(GRXFIFOSIZ(4)),
	dump_register(GRXFIFOSIZ(5)),
	dump_register(GRXFIFOSIZ(6)),
	dump_register(GRXFIFOSIZ(7)),
	dump_register(GRXFIFOSIZ(8)),
	dump_register(GRXFIFOSIZ(9)),
	dump_register(GRXFIFOSIZ(10)),
	dump_register(GRXFIFOSIZ(11)),
	dump_register(GRXFIFOSIZ(12)),
	dump_register(GRXFIFOSIZ(13)),
	dump_register(GRXFIFOSIZ(14)),
	dump_register(GRXFIFOSIZ(15)),
	dump_register(GRXFIFOSIZ(16)),
	dump_register(GRXFIFOSIZ(17)),
	dump_register(GRXFIFOSIZ(18)),
	dump_register(GRXFIFOSIZ(19)),
	dump_register(GRXFIFOSIZ(20)),
	dump_register(GRXFIFOSIZ(21)),
	dump_register(GRXFIFOSIZ(22)),
	dump_register(GRXFIFOSIZ(23)),
	dump_register(GRXFIFOSIZ(24)),
	dump_register(GRXFIFOSIZ(25)),
	dump_register(GRXFIFOSIZ(26)),
	dump_register(GRXFIFOSIZ(27)),
	dump_register(GRXFIFOSIZ(28)),
	dump_register(GRXFIFOSIZ(29)),
	dump_register(GRXFIFOSIZ(30)),
	dump_register(GRXFIFOSIZ(31)),

	dump_register(GEVNTADRLO(0)),
	dump_register(GEVNTADRHI(0)),
	dump_register(GEVNTSIZ(0)),
	dump_register(GEVNTCOUNT(0)),

	dump_register(GHWPARAMS8),
	dump_register(GFLADJ),
	dump_register(DCFG),
	dump_register(DCTL),
	dump_register(DEVTEN),
	dump_register(DSTS),
	dump_register(DGCMDPAR),
	dump_register(DGCMD),
	dump_register(DALEPENA),

	dump_register(DEPCMDPAR2(0)),
	dump_register(DEPCMDPAR2(1)),
	dump_register(DEPCMDPAR2(2)),
	dump_register(DEPCMDPAR2(3)),
	dump_register(DEPCMDPAR2(4)),
	dump_register(DEPCMDPAR2(5)),
	dump_register(DEPCMDPAR2(6)),
	dump_register(DEPCMDPAR2(7)),
	dump_register(DEPCMDPAR2(8)),
	dump_register(DEPCMDPAR2(9)),
	dump_register(DEPCMDPAR2(10)),
	dump_register(DEPCMDPAR2(11)),
	dump_register(DEPCMDPAR2(12)),
	dump_register(DEPCMDPAR2(13)),
	dump_register(DEPCMDPAR2(14)),
	dump_register(DEPCMDPAR2(15)),
	dump_register(DEPCMDPAR2(16)),
	dump_register(DEPCMDPAR2(17)),
	dump_register(DEPCMDPAR2(18)),
	dump_register(DEPCMDPAR2(19)),
	dump_register(DEPCMDPAR2(20)),
	dump_register(DEPCMDPAR2(21)),
	dump_register(DEPCMDPAR2(22)),
	dump_register(DEPCMDPAR2(23)),
	dump_register(DEPCMDPAR2(24)),
	dump_register(DEPCMDPAR2(25)),
	dump_register(DEPCMDPAR2(26)),
	dump_register(DEPCMDPAR2(27)),
	dump_register(DEPCMDPAR2(28)),
	dump_register(DEPCMDPAR2(29)),
	dump_register(DEPCMDPAR2(30)),
	dump_register(DEPCMDPAR2(31)),

	dump_register(DEPCMDPAR1(0)),
	dump_register(DEPCMDPAR1(1)),
	dump_register(DEPCMDPAR1(2)),
	dump_register(DEPCMDPAR1(3)),
	dump_register(DEPCMDPAR1(4)),
	dump_register(DEPCMDPAR1(5)),
	dump_register(DEPCMDPAR1(6)),
	dump_register(DEPCMDPAR1(7)),
	dump_register(DEPCMDPAR1(8)),
	dump_register(DEPCMDPAR1(9)),
	dump_register(DEPCMDPAR1(10)),
	dump_register(DEPCMDPAR1(11)),
	dump_register(DEPCMDPAR1(12)),
	dump_register(DEPCMDPAR1(13)),
	dump_register(DEPCMDPAR1(14)),
	dump_register(DEPCMDPAR1(15)),
	dump_register(DEPCMDPAR1(16)),
	dump_register(DEPCMDPAR1(17)),
	dump_register(DEPCMDPAR1(18)),
	dump_register(DEPCMDPAR1(19)),
	dump_register(DEPCMDPAR1(20)),
	dump_register(DEPCMDPAR1(21)),
	dump_register(DEPCMDPAR1(22)),
	dump_register(DEPCMDPAR1(23)),
	dump_register(DEPCMDPAR1(24)),
	dump_register(DEPCMDPAR1(25)),
	dump_register(DEPCMDPAR1(26)),
	dump_register(DEPCMDPAR1(27)),
	dump_register(DEPCMDPAR1(28)),
	dump_register(DEPCMDPAR1(29)),
	dump_register(DEPCMDPAR1(30)),
	dump_register(DEPCMDPAR1(31)),

	dump_register(DEPCMDPAR0(0)),
	dump_register(DEPCMDPAR0(1)),
	dump_register(DEPCMDPAR0(2)),
	dump_register(DEPCMDPAR0(3)),
	dump_register(DEPCMDPAR0(4)),
	dump_register(DEPCMDPAR0(5)),
	dump_register(DEPCMDPAR0(6)),
	dump_register(DEPCMDPAR0(7)),
	dump_register(DEPCMDPAR0(8)),
	dump_register(DEPCMDPAR0(9)),
	dump_register(DEPCMDPAR0(10)),
	dump_register(DEPCMDPAR0(11)),
	dump_register(DEPCMDPAR0(12)),
	dump_register(DEPCMDPAR0(13)),
	dump_register(DEPCMDPAR0(14)),
	dump_register(DEPCMDPAR0(15)),
	dump_register(DEPCMDPAR0(16)),
	dump_register(DEPCMDPAR0(17)),
	dump_register(DEPCMDPAR0(18)),
	dump_register(DEPCMDPAR0(19)),
	dump_register(DEPCMDPAR0(20)),
	dump_register(DEPCMDPAR0(21)),
	dump_register(DEPCMDPAR0(22)),
	dump_register(DEPCMDPAR0(23)),
	dump_register(DEPCMDPAR0(24)),
	dump_register(DEPCMDPAR0(25)),
	dump_register(DEPCMDPAR0(26)),
	dump_register(DEPCMDPAR0(27)),
	dump_register(DEPCMDPAR0(28)),
	dump_register(DEPCMDPAR0(29)),
	dump_register(DEPCMDPAR0(30)),
	dump_register(DEPCMDPAR0(31)),

	dump_register(DEPCMD(0)),
	dump_register(DEPCMD(1)),
	dump_register(DEPCMD(2)),
	dump_register(DEPCMD(3)),
	dump_register(DEPCMD(4)),
	dump_register(DEPCMD(5)),
	dump_register(DEPCMD(6)),
	dump_register(DEPCMD(7)),
	dump_register(DEPCMD(8)),
	dump_register(DEPCMD(9)),
	dump_register(DEPCMD(10)),
	dump_register(DEPCMD(11)),
	dump_register(DEPCMD(12)),
	dump_register(DEPCMD(13)),
	dump_register(DEPCMD(14)),
	dump_register(DEPCMD(15)),
	dump_register(DEPCMD(16)),
	dump_register(DEPCMD(17)),
	dump_register(DEPCMD(18)),
	dump_register(DEPCMD(19)),
	dump_register(DEPCMD(20)),
	dump_register(DEPCMD(21)),
	dump_register(DEPCMD(22)),
	dump_register(DEPCMD(23)),
	dump_register(DEPCMD(24)),
	dump_register(DEPCMD(25)),
	dump_register(DEPCMD(26)),
	dump_register(DEPCMD(27)),
	dump_register(DEPCMD(28)),
	dump_register(DEPCMD(29)),
	dump_register(DEPCMD(30)),
	dump_register(DEPCMD(31)),

	dump_register(OCFG),
	dump_register(OCTL),
	dump_register(OEVT),
	dump_register(OEVTEN),
	dump_register(OSTS),
};

static int dwc3_mode_show(struct seq_file *s, void *unused)
{
	struct dwc3		*dwc = s->private;
	unsigned long		flags;
	u32			reg;

	spin_lock_irqsave(&dwc->lock, flags);
	reg = dwc3_readl(dwc->regs, DWC3_GCTL);
	spin_unlock_irqrestore(&dwc->lock, flags);

	switch (DWC3_GCTL_PRTCAP(reg)) {
	case DWC3_GCTL_PRTCAP_HOST:
		seq_printf(s, "host\n");
		break;
	case DWC3_GCTL_PRTCAP_DEVICE:
		seq_printf(s, "device\n");
		break;
	case DWC3_GCTL_PRTCAP_OTG:
		seq_printf(s, "OTG\n");
		break;
	default:
		seq_printf(s, "UNKNOWN %08x\n", DWC3_GCTL_PRTCAP(reg));
	}

	return 0;
}

static int dwc3_mode_open(struct inode *inode, struct file *file)
{
	return single_open(file, dwc3_mode_show, inode->i_private);
}

static ssize_t dwc3_mode_write(struct file *file,
		const char __user *ubuf, size_t count, loff_t *ppos)
{
	struct seq_file		*s = file->private_data;
	struct dwc3		*dwc = s->private;
	unsigned long		flags;
	u32			mode = 0;
	char buf[32] = {0};

	if (copy_from_user(&buf, ubuf, min_t(size_t, sizeof(buf) - 1, count)))
		return -EFAULT;

	if (!strncmp(buf, "host", 4))
		mode |= DWC3_GCTL_PRTCAP_HOST;

	if (!strncmp(buf, "device", 6))
		mode |= DWC3_GCTL_PRTCAP_DEVICE;

	if (!strncmp(buf, "otg", 3))
		mode |= DWC3_GCTL_PRTCAP_OTG;

	if (mode) {
		spin_lock_irqsave(&dwc->lock, flags);
		dwc3_set_mode(dwc, mode);
		spin_unlock_irqrestore(&dwc->lock, flags);
	}
	return count;
}

static const struct file_operations dwc3_mode_fops = {
	.open			= dwc3_mode_open,
	.write			= dwc3_mode_write,
	.read			= seq_read,
	.llseek			= seq_lseek,
	.release		= single_release,
};

static int dwc3_testmode_show(struct seq_file *s, void *unused)
{
	struct dwc3		*dwc = s->private;
	unsigned long		flags;
	u32			reg;

	spin_lock_irqsave(&dwc->lock, flags);
	reg = dwc3_readl(dwc->regs, DWC3_DCTL);
	reg &= DWC3_DCTL_TSTCTRL_MASK;
	reg >>= 1;
	spin_unlock_irqrestore(&dwc->lock, flags);

	switch (reg) {
	case 0:
		seq_printf(s, "no test\n");
		break;
	case TEST_J:
		seq_printf(s, "test_j\n");
		break;
	case TEST_K:
		seq_printf(s, "test_k\n");
		break;
	case TEST_SE0_NAK:
		seq_printf(s, "test_se0_nak\n");
		break;
	case TEST_PACKET:
		seq_printf(s, "test_packet\n");
		break;
	case TEST_FORCE_EN:
		seq_printf(s, "test_force_enable\n");
		break;
	default:
		seq_printf(s, "UNKNOWN %d\n", reg);
	}

	return 0;
}

static int dwc3_testmode_open(struct inode *inode, struct file *file)
{
	return single_open(file, dwc3_testmode_show, inode->i_private);
}

static ssize_t dwc3_testmode_write(struct file *file,
		const char __user *ubuf, size_t count, loff_t *ppos)
{
	struct seq_file		*s = file->private_data;
	struct dwc3		*dwc = s->private;
	unsigned long		flags;
	u32			testmode = 0;
	char			buf[32] = {0};

	if (copy_from_user(&buf, ubuf, min_t(size_t, sizeof(buf) - 1, count)))
		return -EFAULT;

	if (!strncmp(buf, "test_j", 6))
		testmode = TEST_J;
	else if (!strncmp(buf, "test_k", 6))
		testmode = TEST_K;
	else if (!strncmp(buf, "test_se0_nak", 12))
		testmode = TEST_SE0_NAK;
	else if (!strncmp(buf, "test_packet", 11))
		testmode = TEST_PACKET;
	else if (!strncmp(buf, "test_force_enable", 17))
		testmode = TEST_FORCE_EN;
	else
		testmode = 0;

	spin_lock_irqsave(&dwc->lock, flags);
	dwc3_gadget_set_test_mode(dwc, testmode);
	spin_unlock_irqrestore(&dwc->lock, flags);

	return count;
}

static const struct file_operations dwc3_testmode_fops = {
	.open			= dwc3_testmode_open,
	.write			= dwc3_testmode_write,
	.read			= seq_read,
	.llseek			= seq_lseek,
	.release		= single_release,
};

static int dwc3_link_state_show(struct seq_file *s, void *unused)
{
	struct dwc3		*dwc = s->private;
	unsigned long		flags;
	enum dwc3_link_state	state;
	u32			reg;

	spin_lock_irqsave(&dwc->lock, flags);
	reg = dwc3_readl(dwc->regs, DWC3_DSTS);
	state = DWC3_DSTS_USBLNKST(reg);
	spin_unlock_irqrestore(&dwc->lock, flags);

	switch (state) {
	case DWC3_LINK_STATE_U0:
		seq_printf(s, "U0\n");
		break;
	case DWC3_LINK_STATE_U1:
		seq_printf(s, "U1\n");
		break;
	case DWC3_LINK_STATE_U2:
		seq_printf(s, "U2\n");
		break;
	case DWC3_LINK_STATE_U3:
		seq_printf(s, "U3\n");
		break;
	case DWC3_LINK_STATE_SS_DIS:
		seq_printf(s, "SS.Disabled\n");
		break;
	case DWC3_LINK_STATE_RX_DET:
		seq_printf(s, "Rx.Detect\n");
		break;
	case DWC3_LINK_STATE_SS_INACT:
		seq_printf(s, "SS.Inactive\n");
		break;
	case DWC3_LINK_STATE_POLL:
		seq_printf(s, "Poll\n");
		break;
	case DWC3_LINK_STATE_RECOV:
		seq_printf(s, "Recovery\n");
		break;
	case DWC3_LINK_STATE_HRESET:
		seq_printf(s, "HRESET\n");
		break;
	case DWC3_LINK_STATE_CMPLY:
		seq_printf(s, "Compliance\n");
		break;
	case DWC3_LINK_STATE_LPBK:
		seq_printf(s, "Loopback\n");
		break;
	case DWC3_LINK_STATE_RESET:
		seq_printf(s, "Reset\n");
		break;
	case DWC3_LINK_STATE_RESUME:
		seq_printf(s, "Resume\n");
		break;
	default:
		seq_printf(s, "UNKNOWN %d\n", state);
	}

	return 0;
}

static int dwc3_link_state_open(struct inode *inode, struct file *file)
{
	return single_open(file, dwc3_link_state_show, inode->i_private);
}

static ssize_t dwc3_link_state_write(struct file *file,
		const char __user *ubuf, size_t count, loff_t *ppos)
{
	struct seq_file		*s = file->private_data;
	struct dwc3		*dwc = s->private;
	unsigned long		flags;
	enum dwc3_link_state	state = 0;
	char			buf[32] = {0};

	if (copy_from_user(&buf, ubuf, min_t(size_t, sizeof(buf) - 1, count)))
		return -EFAULT;

	if (!strncmp(buf, "SS.Disabled", 11))
		state = DWC3_LINK_STATE_SS_DIS;
	else if (!strncmp(buf, "Rx.Detect", 9))
		state = DWC3_LINK_STATE_RX_DET;
	else if (!strncmp(buf, "SS.Inactive", 11))
		state = DWC3_LINK_STATE_SS_INACT;
	else if (!strncmp(buf, "Recovery", 8))
		state = DWC3_LINK_STATE_RECOV;
	else if (!strncmp(buf, "Compliance", 10))
		state = DWC3_LINK_STATE_CMPLY;
	else if (!strncmp(buf, "Loopback", 8))
		state = DWC3_LINK_STATE_LPBK;
	else
		return -EINVAL;

	spin_lock_irqsave(&dwc->lock, flags);
	dwc3_gadget_set_link_state(dwc, state);
	spin_unlock_irqrestore(&dwc->lock, flags);

	return count;
}

static const struct file_operations dwc3_link_state_fops = {
	.open			= dwc3_link_state_open,
	.write			= dwc3_link_state_write,
	.read			= seq_read,
	.llseek			= seq_lseek,
	.release		= single_release,
};

static int ep_num;
static ssize_t dwc3_store_ep_num(struct file *file, const char __user *ubuf,
				 size_t count, loff_t *ppos)
{
	struct seq_file		*s = file->private_data;
	struct dwc3		*dwc = s->private;
	char			kbuf[10] = {0};
	unsigned int		num, dir, temp;
	unsigned long		flags;

	if (copy_from_user(kbuf, ubuf, min_t(size_t, sizeof(kbuf) - 1, count)))
		return -EFAULT;

	if (sscanf(kbuf, "%u %u", &num, &dir) != 2)
		return -EINVAL;

	if (dir != 0 && dir != 1)
		return -EINVAL;

	temp = (num << 1) + dir;
	if (temp >= (dwc->num_in_eps + dwc->num_out_eps) ||
					temp >= DWC3_ENDPOINTS_NUM)
		return -EINVAL;

	spin_lock_irqsave(&dwc->lock, flags);
	ep_num = temp;
	spin_unlock_irqrestore(&dwc->lock, flags);

	return count;
}

static int dwc3_ep_req_list_show(struct seq_file *s, void *unused)
{
	struct dwc3		*dwc = s->private;
	struct dwc3_ep		*dep;
	struct dwc3_request	*req = NULL;
	struct list_head	*ptr = NULL;
	unsigned long		flags;

	spin_lock_irqsave(&dwc->lock, flags);
	dep = dwc->eps[ep_num];

	seq_printf(s, "%s request list: flags: 0x%x\n", dep->name, dep->flags);
	list_for_each(ptr, &dep->request_list) {
		req = list_entry(ptr, struct dwc3_request, list);

		seq_printf(s,
			"req:0x%pK len: %d sts: %d dma:0x%pKa num_sgs: %d\n",
			req, req->request.length, req->request.status,
			&req->request.dma, req->request.num_sgs);
	}
	spin_unlock_irqrestore(&dwc->lock, flags);

	return 0;
}

static int dwc3_ep_req_list_open(struct inode *inode, struct file *file)
{
	return single_open(file, dwc3_ep_req_list_show, inode->i_private);
}

static const struct file_operations dwc3_ep_req_list_fops = {
	.open			= dwc3_ep_req_list_open,
	.write			= dwc3_store_ep_num,
	.read			= seq_read,
	.llseek			= seq_lseek,
	.release		= single_release,
};

static int dwc3_ep_queued_req_show(struct seq_file *s, void *unused)
{
	struct dwc3		*dwc = s->private;
	struct dwc3_ep		*dep;
	struct dwc3_request	*req = NULL;
	struct list_head	*ptr = NULL;
	unsigned long		flags;

	spin_lock_irqsave(&dwc->lock, flags);
	dep = dwc->eps[ep_num];

	seq_printf(s, "%s queued reqs to HW: flags:0x%x\n", dep->name,
								dep->flags);
	list_for_each(ptr, &dep->req_queued) {
		req = list_entry(ptr, struct dwc3_request, list);

		seq_printf(s,
			"req:0x%pK len:%d sts:%d dma:%pKa nsg:%d trb:0x%pK\n",
			req, req->request.length, req->request.status,
			&req->request.dma, req->request.num_sgs, req->trb);
	}
	spin_unlock_irqrestore(&dwc->lock, flags);

	return 0;
}

static int dwc3_ep_queued_req_open(struct inode *inode, struct file *file)
{
	return single_open(file, dwc3_ep_queued_req_show, inode->i_private);
}

const struct file_operations dwc3_ep_req_queued_fops = {
	.open			= dwc3_ep_queued_req_open,
	.write			= dwc3_store_ep_num,
	.read			= seq_read,
	.llseek			= seq_lseek,
	.release		= single_release,
};

static int dwc3_ep_trbs_show(struct seq_file *s, void *unused)
{
	struct dwc3		*dwc = s->private;
	struct dwc3_ep		*dep;
	struct dwc3_trb		*trb;
	unsigned long		flags;
	int			j;

	if (!ep_num)
		return 0;

	spin_lock_irqsave(&dwc->lock, flags);
	dep = dwc->eps[ep_num];
	if (!dep->trb_pool) {
		spin_unlock_irqrestore(&dwc->lock, flags);
		return 0;
	}

	seq_printf(s, "%s trb pool: flags:0x%x freeslot:%d busyslot:%d\n",
		dep->name, dep->flags, dep->free_slot, dep->busy_slot);
	for (j = 0; j < DWC3_TRB_NUM; j++) {
		trb = &dep->trb_pool[j];
		seq_printf(s, "trb:0x%pK bph:0x%x bpl:0x%x size:0x%x ctrl: %x\n",
			trb, trb->bph, trb->bpl, trb->size, trb->ctrl);
	}
	spin_unlock_irqrestore(&dwc->lock, flags);

	return 0;
}

static int dwc3_ep_trbs_list_open(struct inode *inode, struct file *file)
{
	return single_open(file, dwc3_ep_trbs_show, inode->i_private);
}

const struct file_operations dwc3_ep_trb_list_fops = {
	.open			= dwc3_ep_trbs_list_open,
	.write			= dwc3_store_ep_num,
	.read			= seq_read,
	.llseek			= seq_lseek,
	.release		= single_release,
};

static unsigned int ep_addr_rxdbg_mask = 1;
module_param(ep_addr_rxdbg_mask, uint, S_IRUGO | S_IWUSR);
static unsigned int ep_addr_txdbg_mask = 1;
module_param(ep_addr_txdbg_mask, uint, S_IRUGO | S_IWUSR);

static struct dwc3 *dbg_dwc3_data[DWC_CTRL_COUNT];

static int allow_dbg_print(u8 ep_num)
{
	int dir, num;

	/* allow bus wide events */
	if (ep_num == 0xff)
		return 1;

	dir = ep_num & 0x1;
	num = ep_num >> 1;
	num = 1 << num;

	if (dir && (num & ep_addr_txdbg_mask))
		return 1;
	if (!dir && (num & ep_addr_rxdbg_mask))
		return 1;

	return 0;
}

/**
 * dwc3_dbg_print:  prints the common part of the event
 * @addr:   endpoint address
 * @name:   event name
 * @status: status
 * @extra:  extra information
 * @dwc3: pointer to struct dwc3
 */
void dwc3_dbg_print(struct dwc3 *dwc, u8 ep_num, const char *name,
			int status, const char *extra)
{
	if (!allow_dbg_print(ep_num))
		return;

	if (name == NULL)
		return;

	ipc_log_string(dwc->dwc_ipc_log_ctxt, "%02X %-12.12s %4i ?\t%s",
			ep_num, name, status, extra);
}

/**
 * dwc3_dbg_done: prints a DONE event
 * @addr:   endpoint address
 * @td:     transfer descriptor
 * @status: status
 * @dwc3: pointer to struct dwc3
 */
void dwc3_dbg_done(struct dwc3 *dwc, u8 ep_num,
		const u32 count, int status)
{
	if (!allow_dbg_print(ep_num))
		return;

	ipc_log_string(dwc->dwc_ipc_log_ctxt, "%02X %-12.12s %4i ?\t%d",
			ep_num, "DONE", status, count);
}

/*
 * dwc3_dbg_queue: prints a QUEUE event
 * @addr:   endpoint address
 * @req:    USB request
 * @status: status
 */
void dwc3_dbg_queue(struct dwc3 *dwc, u8 ep_num,
		const struct usb_request *req, int status)
{
	if (!allow_dbg_print(ep_num))
		return;

	if (req != NULL) {
		ipc_log_string(dwc->dwc_ipc_log_ctxt,
			"%02X %-12.12s %4i ?\t%d %d", ep_num, "QUEUE", status,
			!req->no_interrupt, req->length);
	}
}

/**
 * dwc3_dbg_setup: prints a SETUP event
 * @addr: endpoint address
 * @req:  setup request
 */
void dwc3_dbg_setup(struct dwc3 *dwc, u8 ep_num,
		const struct usb_ctrlrequest *req)
{
	if (!allow_dbg_print(ep_num))
		return;

	if (req != NULL) {
		ipc_log_string(dwc->dwc_ipc_log_ctxt,
			"%02X %-12.12s ?\t%02X %02X %04X %04X %d",
			ep_num, "SETUP", req->bRequestType,
			req->bRequest, le16_to_cpu(req->wValue),
			le16_to_cpu(req->wIndex), le16_to_cpu(req->wLength));
	}
}

/**
 * dwc3_dbg_print_reg: prints a reg value
 * @name:   reg name
 * @reg: reg value to be printed
 */
void dwc3_dbg_print_reg(struct dwc3 *dwc, const char *name, int reg)
{
	if (name == NULL)
		return;

	ipc_log_string(dwc->dwc_ipc_log_ctxt, "%s = 0x%08x", name, reg);
}

static ssize_t dwc3_store_int_events(struct file *file,
			const char __user *ubuf, size_t count, loff_t *ppos)
{
	int i, ret;
	unsigned long flags;
	struct seq_file *s = file->private_data;
	struct dwc3 *dwc = s->private;
	struct dwc3_ep *dep;
	struct timespec ts;
	u8 clear_stats;

	if (ubuf == NULL) {
		pr_err("[%s] EINVAL\n", __func__);
		ret = -EINVAL;
		return ret;
	}

	ret = kstrtou8_from_user(ubuf, count, 0, &clear_stats);
	if (ret < 0) {
		pr_err("can't get enter value.\n");
		return ret;
	}

	if (clear_stats != 0) {
		pr_err("Wrong value. To clear stats, enter value as 0.\n");
		ret = -EINVAL;
		return ret;
	}

	spin_lock_irqsave(&dwc->lock, flags);

	pr_debug("%s(): clearing debug interrupt buffers\n", __func__);
	ts = current_kernel_time();
	for (i = 0; i < DWC3_ENDPOINTS_NUM; i++) {
		dep = dwc->eps[i];
		memset(&dep->dbg_ep_events, 0, sizeof(dep->dbg_ep_events));
		memset(&dep->dbg_ep_events_diff, 0, sizeof(dep->dbg_ep_events));
		dep->dbg_ep_events_ts = ts;
	}
	memset(&dwc->dbg_gadget_events, 0, sizeof(dwc->dbg_gadget_events));

	spin_unlock_irqrestore(&dwc->lock, flags);

	return count;
}

static int dwc3_gadget_int_events_show(struct seq_file *s, void *unused)
{
	unsigned long   flags;
	struct dwc3 *dwc = s->private;
	struct dwc3_gadget_events *dbg_gadget_events;
	struct dwc3_ep *dep;
	int i;
	struct timespec ts_delta;
	struct timespec ts_current;
	u32 ts_delta_ms;

	spin_lock_irqsave(&dwc->lock, flags);
	dbg_gadget_events = &dwc->dbg_gadget_events;

	for (i = 0; i < DWC3_ENDPOINTS_NUM; i++) {
		dep = dwc->eps[i];

		if (dep == NULL || !(dep->flags & DWC3_EP_ENABLED))
			continue;

		ts_current = current_kernel_time();
		ts_delta = timespec_sub(ts_current, dep->dbg_ep_events_ts);
		ts_delta_ms = ts_delta.tv_nsec / NSEC_PER_MSEC +
			ts_delta.tv_sec * MSEC_PER_SEC;

		seq_printf(s, "\n\n===== dbg_ep_events for EP(%d) %s =====\n",
			i, dep->name);
		seq_printf(s, "xfercomplete:%u @ %luHz\n",
			dep->dbg_ep_events.xfercomplete,
			ep_event_rate(xfercomplete, dep->dbg_ep_events,
				dep->dbg_ep_events_diff, ts_delta_ms));
		seq_printf(s, "xfernotready:%u @ %luHz\n",
			dep->dbg_ep_events.xfernotready,
			ep_event_rate(xfernotready, dep->dbg_ep_events,
				dep->dbg_ep_events_diff, ts_delta_ms));
		seq_printf(s, "control_data:%u @ %luHz\n",
			dep->dbg_ep_events.control_data,
			ep_event_rate(control_data, dep->dbg_ep_events,
				dep->dbg_ep_events_diff, ts_delta_ms));
		seq_printf(s, "control_status:%u @ %luHz\n",
			dep->dbg_ep_events.control_status,
			ep_event_rate(control_status, dep->dbg_ep_events,
				dep->dbg_ep_events_diff, ts_delta_ms));
		seq_printf(s, "xferinprogress:%u @ %luHz\n",
			dep->dbg_ep_events.xferinprogress,
			ep_event_rate(xferinprogress, dep->dbg_ep_events,
				dep->dbg_ep_events_diff, ts_delta_ms));
		seq_printf(s, "rxtxfifoevent:%u @ %luHz\n",
			dep->dbg_ep_events.rxtxfifoevent,
			ep_event_rate(rxtxfifoevent, dep->dbg_ep_events,
				dep->dbg_ep_events_diff, ts_delta_ms));
		seq_printf(s, "streamevent:%u @ %luHz\n",
			dep->dbg_ep_events.streamevent,
			ep_event_rate(streamevent, dep->dbg_ep_events,
				dep->dbg_ep_events_diff, ts_delta_ms));
		seq_printf(s, "epcmdcomplt:%u @ %luHz\n",
			dep->dbg_ep_events.epcmdcomplete,
			ep_event_rate(epcmdcomplete, dep->dbg_ep_events,
				dep->dbg_ep_events_diff, ts_delta_ms));
		seq_printf(s, "cmdcmplt:%u @ %luHz\n",
			dep->dbg_ep_events.cmdcmplt,
			ep_event_rate(cmdcmplt, dep->dbg_ep_events,
				dep->dbg_ep_events_diff, ts_delta_ms));
		seq_printf(s, "unknown:%u @ %luHz\n",
			dep->dbg_ep_events.unknown_event,
			ep_event_rate(unknown_event, dep->dbg_ep_events,
				dep->dbg_ep_events_diff, ts_delta_ms));
		seq_printf(s, "total:%u @ %luHz\n",
			dep->dbg_ep_events.total,
			ep_event_rate(total, dep->dbg_ep_events,
				dep->dbg_ep_events_diff, ts_delta_ms));

		dep->dbg_ep_events_ts = ts_current;
		dep->dbg_ep_events_diff = dep->dbg_ep_events;
	}

	seq_puts(s, "\n=== dbg_gadget events ==\n");
	seq_printf(s, "disconnect:%u\n reset:%u\n",
		dbg_gadget_events->disconnect, dbg_gadget_events->reset);
	seq_printf(s, "connect:%u\n wakeup:%u\n",
		dbg_gadget_events->connect, dbg_gadget_events->wakeup);
	seq_printf(s, "link_status_change:%u\n eopf:%u\n",
		dbg_gadget_events->link_status_change, dbg_gadget_events->eopf);
	seq_printf(s, "sof:%u\n suspend:%u\n",
		dbg_gadget_events->sof, dbg_gadget_events->suspend);
	seq_printf(s, "erratic_error:%u\n overflow:%u\n",
		dbg_gadget_events->erratic_error,
		dbg_gadget_events->overflow);
	seq_printf(s, "vendor_dev_test_lmp:%u\n cmdcmplt:%u\n",
		dbg_gadget_events->vendor_dev_test_lmp,
		dbg_gadget_events->cmdcmplt);
	seq_printf(s, "unknown_event:%u\n", dbg_gadget_events->unknown_event);

	seq_printf(s, "\n\t== Last %d interrupts stats ==\t\n", MAX_INTR_STATS);
	seq_puts(s, "@ time (us):\t");
	for (i = 0; i < MAX_INTR_STATS; i++)
		seq_printf(s, "%lld\t", ktime_to_us(dwc->irq_start_time[i]));
	seq_puts(s, "\nhard irq time (us):\t");
	for (i = 0; i < MAX_INTR_STATS; i++)
		seq_printf(s, "%d\t", dwc->irq_completion_time[i]);
	seq_puts(s, "\nevents count:\t\t");
	for (i = 0; i < MAX_INTR_STATS; i++)
		seq_printf(s, "%d\t", dwc->irq_event_count[i]);
	seq_puts(s, "\nbh handled count:\t");
	for (i = 0; i < MAX_INTR_STATS; i++)
		seq_printf(s, "%d\t", dwc->bh_handled_evt_cnt[i]);
	seq_puts(s, "\nirq thread time (us):\t");
	for (i = 0; i < MAX_INTR_STATS; i++)
		seq_printf(s, "%d\t", dwc->bh_completion_time[i]);
	seq_putc(s, '\n');

	seq_printf(s, "t_pwr evt irq : %lld\n",
			ktime_to_us(dwc->t_pwr_evt_irq));

	seq_printf(s, "l1_remote_wakeup_cnt : %lu\n",
		dwc->l1_remote_wakeup_cnt);

	spin_unlock_irqrestore(&dwc->lock, flags);
	return 0;
}

static int dwc3_gadget_events_open(struct inode *inode, struct file *f)
{
	return single_open(f, dwc3_gadget_int_events_show, inode->i_private);
}

const struct file_operations dwc3_gadget_dbg_events_fops = {
	.open		= dwc3_gadget_events_open,
	.read		= seq_read,
	.write		= dwc3_store_int_events,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int count;
int dwc3_debugfs_init(struct dwc3 *dwc)
{
	struct dentry		*root;
	struct dentry		*file;

	if (count >= DWC_CTRL_COUNT) {
		dev_err(dwc->dev, "Err dwc instance %d >= %d available\n",
				count, DWC_CTRL_COUNT);
		return -EINVAL;
	}

	root = debugfs_create_dir(dev_name(dwc->dev), NULL);
	if (!root) {
		dev_err(dwc->dev, "Can't create debugfs root\n");
		return -ENOMEM;
	}

	dwc->root = root;

	dwc->regset = kzalloc(sizeof(*dwc->regset), GFP_KERNEL);
	if (!dwc->regset) {
		goto err1;
	}

	dwc->regset->regs = dwc3_regs;
	dwc->regset->nregs = ARRAY_SIZE(dwc3_regs);
	dwc->regset->base = dwc->regs;

	file = debugfs_create_regset32("regdump", S_IRUGO, root, dwc->regset);
	if (!file) {
		dev_dbg(dwc->dev, "Can't create debugfs regdump\n");
		goto err1;
	}

	if (IS_ENABLED(CONFIG_USB_DWC3_DUAL_ROLE)) {
		file = debugfs_create_file("mode", S_IRUGO | S_IWUSR, root,
				dwc, &dwc3_mode_fops);
		if (!file) {
			dev_dbg(dwc->dev, "Can't create debugfs mode\n");
			goto err1;
		}
	}

	if (IS_ENABLED(CONFIG_USB_DWC3_DUAL_ROLE) ||
			IS_ENABLED(CONFIG_USB_DWC3_GADGET)) {
		file = debugfs_create_file("testmode", S_IRUGO | S_IWUSR, root,
				dwc, &dwc3_testmode_fops);
		if (!file) {
			dev_dbg(dwc->dev, "Can't create debugfs testmode\n");
			goto err1;
		}

		file = debugfs_create_file("link_state", S_IRUGO | S_IWUSR, root,
				dwc, &dwc3_link_state_fops);
		if (!file) {
			dev_dbg(dwc->dev, "Can't create debugfs linkstate\n");
			goto err1;
		}
	}

	file = debugfs_create_file("trbs", S_IRUGO | S_IWUSR, root,
			dwc, &dwc3_ep_trb_list_fops);
	if (!file) {
		dev_dbg(dwc->dev, "Can't create debugfs trbs\n");
		goto err1;
	}

	file = debugfs_create_file("requests", S_IRUGO | S_IWUSR, root,
			dwc, &dwc3_ep_req_list_fops);
	if (!file) {
		dev_dbg(dwc->dev, "Can't create debugfs requests\n");
		goto err1;
	}

	file = debugfs_create_file("queued_reqs", S_IRUGO | S_IWUSR, root,
			dwc, &dwc3_ep_req_queued_fops);
	if (!file) {
		dev_dbg(dwc->dev, "Can't create debugfs queued_reqs\n");
		goto err1;
	}

	file = debugfs_create_file("int_events", S_IRUGO | S_IWUSR, root,
			dwc, &dwc3_gadget_dbg_events_fops);
	if (!file) {
		dev_dbg(dwc->dev, "Can't create debugfs int_events\n");
		goto err1;
	}
#ifdef CONFIG_IPC_LOGGING
	dwc->dwc_ipc_log_ctxt = ipc_log_context_create(NUM_LOG_PAGES,
					dev_name(dwc->dev), 0);
	if (!dwc->dwc_ipc_log_ctxt) {
		dev_err(dwc->dev, "Error getting ipc_log_ctxt\n");
		goto err1;
	}
#endif
	dbg_dwc3_data[count] = dwc;
	count++;
	return 0;

err1:
	kfree(dwc->regset);
	dwc->regset = NULL;
	debugfs_remove_recursive(root);
	return -ENOMEM;
}

void dwc3_debugfs_exit(struct dwc3 *dwc)
{
	debugfs_remove_recursive(dwc->root);
	dwc->root = NULL;
	kfree(dwc->regset);
	dwc->regset = NULL;
#ifdef CONFIG_IPC_LOGGING
	ipc_log_context_destroy(dwc->dwc_ipc_log_ctxt);
#endif
	dwc->dwc_ipc_log_ctxt = NULL;
}
