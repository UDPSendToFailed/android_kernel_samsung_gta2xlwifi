/*
 * Based on arch/arm/lib/strrchr.S
 *
 * Copyright (C) 1995-2000 Russell King
 * Copyright (C) 2013 ARM Ltd.
 * Copyright (C) 2026 UDPSendToFailed <udpsendtofailed@gmail.com>
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
#include <asm/assembler.h>

#define REP8_01 0x0101010101010101
#define REP8_7f 0x7f7f7f7f7f7f7f7f

#define srcin		x0
#define chrin		w1
#define result		x0

#define rep01		x2
#define rep7f		x12
#define repchr		x4
#define data		x5
#define has_nul		x6
#define has_chr		x7
#define tmp_chr		x8
#define tmp1		x9
#define tmp2		x10
#define last_match	x3

/*
 * Find the last occurrence of a character in a string.
 *
 * Parameters:
 *	x0 - str
 *	x1 - c
 * Returns:
 *	x0 - address of last occurrence of 'c' or 0
 */
ENTRY(strrchr)
	mov	last_match, #0
	and	chrin, chrin, #0xff

	/* Byte loop until 8-byte aligned */
1:	tst	srcin, #7
	b.eq	.Laligned
	ldrb	w2, [srcin], #1
	cmp	w2, chrin
	sub	x4, srcin, #1
	csel	last_match, x4, last_match, eq
	cbnz	w2, 1b
	mov	result, last_match
	ret

	.p2align 4
.Laligned:
	mov	rep01, #REP8_01
	mov	rep7f, #REP8_7f
	mul	repchr, x1, rep01

	.p2align 4
2:	ldr	data, [srcin], #8
	sub	has_nul, data, rep01
	orr	tmp1, data, rep7f
	bic	has_nul, has_nul, tmp1
	cbnz	has_nul, .Lnul_found

	/*
	 * No NUL in this word. Check exact character match.
	 * We use the exact zero-byte test to prevent borrow propagation across
	 * bytes (which occurs in classic (y - 0x01) & ~(y | 0x7f) when c is adjacent to c^1):
	 *   tmp2 = (tmp_chr & 0x7f) + 0x7f; tmp2 = tmp2 | tmp_chr | 0x7f; has_chr = ~tmp2;
	 * In this formulation, each byte's sum cannot exceed 254 (0xFE), so carries and
	 * borrows never cross byte boundaries.
	 */
	eor	tmp_chr, data, repchr
	and	tmp2, tmp_chr, rep7f
	add	tmp2, tmp2, rep7f
	orr	tmp2, tmp2, tmp_chr
	orr	tmp2, tmp2, rep7f
	mvn	has_chr, tmp2
	cbz	has_chr, 2b

	/* Match found in full 8-byte word (no NUL terminator yet) */
#ifdef __AARCH64EB__
	rbit	tmp1, has_chr
	clz	tmp1, tmp1
	lsr	tmp1, tmp1, #3
	sub	tmp2, srcin, #8
	add	last_match, tmp2, tmp1
#else
	clz	tmp1, has_chr
	mov	tmp2, #63
	sub	tmp1, tmp2, tmp1
	lsr	tmp1, tmp1, #3
	sub	tmp2, srcin, #8
	add	last_match, tmp2, tmp1
#endif
	b	2b

.Lnul_found:
	/* NUL found in this word. Check each byte up to NUL */
	sub	srcin, srcin, #8
3:	ldrb	w2, [srcin], #1
	cmp	w2, chrin
	sub	x4, srcin, #1
	csel	last_match, x4, last_match, eq
	cbnz	w2, 3b
	mov	result, last_match
	ret
ENDPIPROC(strrchr)
