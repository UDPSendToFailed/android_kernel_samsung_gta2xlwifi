/*
 * Based on arch/arm/lib/strchr.S
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
#define rep7f		x3
#define repchr		x4
#define data		x5
#define has_nul		x6
#define has_chr		x7
#define tmp_chr		x8
#define tmp1		x9
#define tmp2		x10
#define syndrome	x11

/*
 * Find the first occurrence of a character in a string.
 *
 * Parameters:
 *	x0 - str
 *	x1 - c
 * Returns:
 *	x0 - address of first occurrence of 'c' or 0
 */
ENTRY(strchr)
	and	chrin, chrin, #0xff

	/* Byte loop until 8-byte aligned */
1:	tst	srcin, #7
	b.eq	.Laligned
	ldrb	w2, [srcin], #1
	cmp	w2, chrin
	b.eq	.Lfound_byte
	cbnz	w2, 1b
	mov	result, #0
	ret

.Lfound_byte:
	sub	result, srcin, #1
	ret

	.p2align 4
.Laligned:
	mov	rep01, #REP8_01
	mov	rep7f, #REP8_7f
	mul	repchr, x1, rep01

	.p2align 4
2:	ldr	data, [srcin], #8
	sub	has_nul, data, rep01
	eor	tmp_chr, data, repchr
	sub	has_chr, tmp_chr, rep01
	orr	tmp1, data, rep7f
	orr	tmp2, tmp_chr, rep7f
	bic	has_nul, has_nul, tmp1
	bic	has_chr, has_chr, tmp2
	orr	syndrome, has_nul, has_chr
	cbz	syndrome, 2b

	/* Either found character or reached NUL terminator */
#ifdef __AARCH64EB__
	/* Big-endian: bits are already MSB-first */
#else
	rev	has_nul, has_nul
	rev	has_chr, has_chr
#endif
	clz	tmp1, has_nul
	clz	tmp2, has_chr

	/* If tmp2 (chr) <= tmp1 (nul), match is valid */
	sub	has_chr, tmp2, #64
	add	result, srcin, has_chr, asr #3
	cmp	tmp2, tmp1
	csel	result, result, xzr, ls
	ret
ENDPIPROC(strchr)
