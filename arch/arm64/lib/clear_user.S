/*
 * Based on arch/arm/lib/clear_user.S
 *
 * Copyright (C) 2012 ARM Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <linux/linkage.h>

#include <asm/uaccess.h>

	.text

/* Prototype: int __arch_clear_user(void *addr, size_t sz)
 * Purpose  : clear some user memory
 * Params   : addr - user memory address to clear
 *          : sz   - number of bytes to clear
 * Returns  : number of bytes NOT cleared
 *
 * Optimized for Cortex-A53 128-bit internal write bus and LSU pipelining.
 * Alignment fixed up by hardware, with 16-byte alignment prefix for maximum
 * Store Buffer (STB) coalescing throughput.
 */
ENTRY(__arch_clear_user)
	uaccess_enable_not_uao x2, x3, x4
	add	x4, x0, x1			// x4 = end address (addr + sz)
	cbz	x1, .Lclear_exit

	cmp	x1, #16
	b.lo	.Ltiny15

	cmp	x1, #64
	b.lo	.Ltail63

	/*
	 * Align x0 to 16 bytes for 128-bit STP write bus coalescing.
	 * Any unaligned prefix (1 to 15 bytes) is cleared first.
	 */
	neg	x5, x0
	ands	x5, x5, #15
	b.eq	.Lloop64_start
	sub	x1, x1, x5

	tbz	x5, #0, 1f
	uao_user_alternative 9f, strb, sttrb, wzr, x0, 1
1:	tbz	x5, #1, 2f
	uao_user_alternative 9f, strh, sttrh, wzr, x0, 2
2:	tbz	x5, #2, 3f
	uao_user_alternative 9f, str, sttr, wzr, x0, 4
3:	tbz	x5, #3, .Lloop64_start
	uao_user_alternative 9f, str, sttr, xzr, x0, 8

.Lloop64_start:
	subs	x1, x1, #64
	b.mi	.Ltail63_restore

.Lloop64:
	uao_stp	9f, xzr, xzr, x0, 16
	uao_stp	9f, xzr, xzr, x0, 16
	uao_stp	9f, xzr, xzr, x0, 16
	uao_stp	9f, xzr, xzr, x0, 16
	subs	x1, x1, #64
	b.pl	.Lloop64

.Ltail63_restore:
	adds	x1, x1, #64
	b.eq	.Lclear_exit

.Ltail63:
	tbz	x1, #5, 11f
	uao_stp	9f, xzr, xzr, x0, 16
	uao_stp	9f, xzr, xzr, x0, 16
11:	tbz	x1, #4, 12f
	uao_stp	9f, xzr, xzr, x0, 16
12:

.Ltiny15:
	tbz	x1, #3, 13f
	uao_user_alternative 9f, str, sttr, xzr, x0, 8
13:	tbz	x1, #2, 14f
	uao_user_alternative 9f, str, sttr, wzr, x0, 4
14:	tbz	x1, #1, 15f
	uao_user_alternative 9f, strh, sttrh, wzr, x0, 2
15:	tbz	x1, #0, .Lclear_exit
	uao_user_alternative 9f, strb, sttrb, wzr, x0, 1

.Lclear_exit:
	mov	x0, #0
	uaccess_disable_not_uao x2, x3
	ret
ENDPROC(__arch_clear_user)

	.section .fixup,"ax"
	.align	2
9:	sub	x0, x4, x0			// bytes not cleared = end - current
	cmp	x0, #0
	csel	x0, x0, xzr, pl			// clamp to >= 0
ALTERNATIVE("nop", __stringify(SET_PSTATE_PAN(1)), ARM64_ALT_PAN_NOT_UAO, \
	    CONFIG_ARM64_PAN)
	ret
	.previous
