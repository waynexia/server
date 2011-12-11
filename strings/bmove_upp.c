/* Copyright (c) 2000 TXT DataKonsult Ab & Monty Program Ab
   Use is subject to license terms.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA
*/

/*  File   : bmove.c
    Author : Michael widenius
    Updated: 1987-03-20
    Defines: bmove_upp()

    bmove_upp(dst, src, len) moves exactly "len" bytes from the source
    "src-len" to the destination "dst-len" counting downwards.
*/

#include "strings_def.h"

#if defined(MC68000) && defined(DS90)

/* 0 <= len <= 65535 */
void bmove_upp(byte *dst, const byte *src,uint len)
{
asm("		movl	12(a7),d0	");
asm("		subql	#1,d0		");
asm("		blt	.L5		");
asm("		movl	4(a7),a1	");
asm("		movl	8(a7),a0	");
asm(".L4:	movb	-(a0),-(a1)	");
asm("		dbf	d0,.L4		");
asm(".L5:				");
}
#else

void bmove_upp(register uchar *dst, register const uchar *src,
               register size_t len)
{
  while (len-- != 0) *--dst = *--src;
}

#endif
