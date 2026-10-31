/*
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
#include <linux/const.h>
#include <asm/assembler.h>
#include <asm/page.h>
#include <asm/cpufeature.h>
#include <asm/alternative.h>

/*
 * Copy a page from src to dest (both are page aligned)
 *
 * Parameters:
 *	x0 - dest
 *	x1 - src
 */
ENTRY(copy_page)
	ldp	x2, x3, [x1], #16
	ldp	x4, x5, [x1], #16
	ldp	x6, x7, [x1], #16
	ldp	x8, x9, [x1], #16
	ldp	x10, x11, [x1], #16
	ldp	x12, x13, [x1], #16
	ldp	x14, x15, [x1], #16
	ldp	x16, x17, [x1], #16

	.align	4
1:
	stp	x2, x3, [x0], #16
	ldp	x2, x3, [x1], #16
	stp	x4, x5, [x0], #16
	ldp	x4, x5, [x1], #16
	stp	x6, x7, [x0], #16
	ldp	x6, x7, [x1], #16
	stp	x8, x9, [x0], #16
	ldp	x8, x9, [x1], #16
	stp	x10, x11, [x0], #16
	ldp	x10, x11, [x1], #16
	stp	x12, x13, [x0], #16
	ldp	x12, x13, [x1], #16
	stp	x14, x15, [x0], #16
	ldp	x14, x15, [x1], #16
	stp	x16, x17, [x0], #16
	ldp	x16, x17, [x1], #16

	tst	x1, #(PAGE_SIZE - 1)
	b.ne	1b

	stp	x2, x3, [x0], #16
	stp	x4, x5, [x0], #16
	stp	x6, x7, [x0], #16
	stp	x8, x9, [x0], #16
	stp	x10, x11, [x0], #16
	stp	x12, x13, [x0], #16
	stp	x14, x15, [x0], #16
	stp	x16, x17, [x0], #16

	ret
ENDPROC(copy_page)
