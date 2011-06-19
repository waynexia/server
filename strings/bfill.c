/* Copyright Richard A. O'Keefe.
   Copyright (c) 2000 TXT DataKonsult Ab & Monty Program Ab
   Copyright (c) 2009-2011, Monty Program Ab

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions are
   met:

   1. Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.

   2. Redistributions in binary form must the following disclaimer in
     the documentation and/or other materials provided with the
     distribution.

   THIS SOFTWARE IS PROVIDED BY <COPYRIGHT HOLDER> ``AS IS'' AND ANY
   EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
   IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
   PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDER> OR
   CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
   USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
   ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
   OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
   OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
   SUCH DAMAGE.
*/

/*  File   : bfill.c
    Author : Richard A. O'Keefe.
	     Michael Widenius;	ifdef MC68000
    Updated: 23 April 1984
    Defines: bfill()

    bfill(dst, len, fill) moves "len" fill characters to "dst".
    Thus to set a buffer to 80 spaces, do bfill(buff, 80, ' ').

    Note: the "b" routines are there to exploit certain VAX order codes,
    but the MOVC5 instruction will only move 65535 characters.	 The asm
    code is presented for your interest and amusement.
*/

#include "strings_def.h"

#if !defined(bfill) && !defined(HAVE_BFILL)

#if VaxAsm

void bfill(dst, len, fill)
char *dst;
uint len;
int fill; /* actually char */
{
  asm("movc5 $0,*4(ap),12(ap),8(ap),*4(ap)");
}

#elif defined(MC68000) && defined(DS90)

void bfill(dst, len,fill)			/* Optimized with long-fill */
char *dst;
uint len;
pchar fill;
{
asm("		movl	8.(a7),d1	");
asm("		jeq	.L9		");
asm("		movl	4.(a7),a0	");
asm("		moveq	#0,d0		");
asm("		movb	15.(a7),d0	");
asm("		movl	d2,a1		");
asm("		movw	d0,d2		");
asm("		aslw	#8,d0		");
asm("		orw	d2,d0		");
asm("		movl	d0,d2		");
asm("		swap	d0		");
asm("		orl	d2,d0		");
asm("		movl	a0,d2		");
asm("		btst	#0,d2		");
asm("		jeq	.L1		");
asm("		movb	d0,(a0)+	");
asm("		subql	#1,d1		");
asm(".L1:	movl	d1,d2		");
asm("		lsrl	#2,d2		");
asm("		jcc	.L2		");
asm("		movw	d0,(a0)+	");
asm("		jra	.L2		");
asm(".L3:	movl	d0,(a0)+	");
asm(".L2:	dbra	d2,.L3		");
asm("		addqw	#1,d2		");
asm("		subql	#1,d2		");
asm("		jcc	.L3		");
asm("		andl	#1,d1		");
asm("		jeq	.L8		");
asm("		movb	d0,(a0)		");
asm(".L8:	movl	a1,d2		");
asm(".L9:	rts			");
}
#else

void bfill(dst, len, fill)
register byte *dst;
register uint len;
register pchar fill;
{
  while (len-- != 0) *dst++ = fill;
}

#endif
#endif
