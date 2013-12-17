/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil -*- */
// vim: ft=cpp:expandtab:ts=8:sw=4:softtabstop=4:
#ident "$Id$"
/*
COPYING CONDITIONS NOTICE:

  This program is free software; you can redistribute it and/or modify
  it under the terms of version 2 of the GNU General Public License as
  published by the Free Software Foundation, and provided that the
  following conditions are met:

      * Redistributions of source code must retain this COPYING
        CONDITIONS NOTICE, the COPYRIGHT NOTICE (below), the
        DISCLAIMER (below), the UNIVERSITY PATENT NOTICE (below), the
        PATENT MARKING NOTICE (below), and the PATENT RIGHTS
        GRANT (below).

      * Redistributions in binary form must reproduce this COPYING
        CONDITIONS NOTICE, the COPYRIGHT NOTICE (below), the
        DISCLAIMER (below), the UNIVERSITY PATENT NOTICE (below), the
        PATENT MARKING NOTICE (below), and the PATENT RIGHTS
        GRANT (below) in the documentation and/or other materials
        provided with the distribution.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
  02110-1301, USA.

COPYRIGHT NOTICE:

  TokuDB, Tokutek Fractal Tree Indexing Library.
  Copyright (C) 2007-2013 Tokutek, Inc.

DISCLAIMER:

  This program is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

UNIVERSITY PATENT NOTICE:

  The technology is licensed by the Massachusetts Institute of
  Technology, Rutgers State University of New Jersey, and the Research
  Foundation of State University of New York at Stony Brook under
  United States of America Serial No. 11/760379 and to the patents
  and/or patent applications resulting from it.

PATENT MARKING NOTICE:

  This software is covered by US Patent No. 8,185,551.
  This software is covered by US Patent No. 8,489,638.

PATENT RIGHTS GRANT:

  "THIS IMPLEMENTATION" means the copyrightable works distributed by
  Tokutek as part of the Fractal Tree project.

  "PATENT CLAIMS" means the claims of patents that are owned or
  licensable by Tokutek, both currently or in the future; and that in
  the absence of this license would be infringed by THIS
  IMPLEMENTATION or by using or running THIS IMPLEMENTATION.

  "PATENT CHALLENGE" shall mean a challenge to the validity,
  patentability, enforceability and/or non-infringement of any of the
  PATENT CLAIMS or otherwise opposing any of the PATENT CLAIMS.

  Tokutek hereby grants to you, for the term and geographical scope of
  the PATENT CLAIMS, a non-exclusive, no-charge, royalty-free,
  irrevocable (except as stated in this section) patent license to
  make, have made, use, offer to sell, sell, import, transfer, and
  otherwise run, modify, and propagate the contents of THIS
  IMPLEMENTATION, where such license applies only to the PATENT
  CLAIMS.  This grant does not include claims that would be infringed
  only as a consequence of further modifications of THIS
  IMPLEMENTATION.  If you or your agent or licensee institute or order
  or agree to the institution of patent litigation against any entity
  (including a cross-claim or counterclaim in a lawsuit) alleging that
  THIS IMPLEMENTATION constitutes direct or contributory patent
  infringement, or inducement of patent infringement, then any rights
  granted to you under this License shall terminate as of the date
  such litigation is filed.  If you or your agent or exclusive
  licensee institute or order or agree to the institution of a PATENT
  CHALLENGE, then Tokutek may terminate any rights granted to you
  under this License.
*/

#ident "Copyright (c) 2007-2013 Tokutek Inc.  All rights reserved."
#ident "The technology is licensed by the Massachusetts Institute of Technology, Rutgers State University of New Jersey, and the Research Foundation of State University of New York at Stony Brook under United States of America Serial No. 11/760379 and to the patents and/or patent applications resulting from it."
// Fast data compression library
// Copyright (C) 2006-2011 Lasse Mikkel Reinhold
// lar@quicklz.com
//
// QuickLZ can be used for free under the GPL 1, 2 or 3 license (where anything 
// released into public must be open source) or under a commercial license if such 
// has been acquired (see http://www.quicklz.com/order.html). The commercial license 
// does not cover derived or ported versions created by third parties under GPL.

// 1.5.0 final

#include "quicklz.h"

#if QLZ_VERSION_MAJOR != 1 || QLZ_VERSION_MINOR != 5 || QLZ_VERSION_REVISION != 0
	#error quicklz.c and quicklz.h have different versions
#endif

#if (defined(__X86__) || defined(__i386__) || defined(i386) || defined(_M_IX86) || defined(__386__) || defined(__x86_64__) || defined(_M_X64))
	#define X86X64
#endif

#define MINOFFSET 2
#define UNCONDITIONAL_MATCHLEN 6
#define UNCOMPRESSED_END 4
#define CWORD_LEN 4

#if QLZ_COMPRESSION_LEVEL == 1 && defined QLZ_PTR_64 && QLZ_STREAMING_BUFFER == 0
	#define OFFSET_BASE source
	#define CAST (ui32)(size_t)
#else
	#define OFFSET_BASE 0
	#define CAST
#endif

int qlz_get_setting(int setting)
{
	switch (setting)
	{
		case 0: return QLZ_COMPRESSION_LEVEL;
		case 1: return sizeof(qlz_state_compress);
		case 2: return sizeof(qlz_state_decompress);
		case 3: return QLZ_STREAMING_BUFFER;
#ifdef QLZ_MEMORY_SAFE
		case 6: return 1;
#else
		case 6: return 0;
#endif
		case 7: return QLZ_VERSION_MAJOR;
		case 8: return QLZ_VERSION_MINOR;
		case 9: return QLZ_VERSION_REVISION;
	}
	return -1;
}

#if QLZ_COMPRESSION_LEVEL == 1
static int same(const unsigned char *src, size_t n)
{
	while(n > 0 && *(src + n) == *src)
		n--;
	return n == 0 ? 1 : 0;
}
#endif

static void reset_table_compress(qlz_state_compress *state)
{
	int i;
	for(i = 0; i < QLZ_HASH_VALUES; i++)
	{
#if QLZ_COMPRESSION_LEVEL == 1
		state->hash[i].offset = 0;
#else
		state->hash_counter[i] = 0;
                state->hash[i].offset[0] = 0;
#endif
	}
}

static void reset_table_decompress(qlz_state_decompress *state)
{
	(void)state;
#if QLZ_COMPRESSION_LEVEL == 2
	for(int i = 0; i < QLZ_HASH_VALUES; i++)
	{
		state->hash_counter[i] = 0;
	}
#endif
}

static __inline ui32 hash_func(ui32 i)
{
#if QLZ_COMPRESSION_LEVEL == 2
	return ((i >> 9) ^ (i >> 13) ^ i) & (QLZ_HASH_VALUES - 1);
#else
	return ((i >> 12) ^ i) & (QLZ_HASH_VALUES - 1);
#endif
}

static __inline ui32 fast_read(void const *src, ui32 bytes)
{
#ifndef X86X64
	unsigned char *p = (unsigned char*)src;
	switch (bytes)
	{
		case 4:
			return(*p | *(p + 1) << 8 | *(p + 2) << 16 | *(p + 3) << 24);
		case 3: 
			return(*p | *(p + 1) << 8 | *(p + 2) << 16);
		case 2:
			return(*p | *(p + 1) << 8);
		case 1: 
			return(*p);
	}
	return 0;
#else
	if (bytes >= 1 && bytes <= 4)
		return *((ui32*)src);
	else
		return 0;
#endif
}

static __inline ui32 hashat(const unsigned char *src)
{
	ui32 fetch, hash;
	fetch = fast_read(src, 3);
	hash = hash_func(fetch);
	return hash;
}

static __inline void fast_write(ui32 f, void *dst, size_t bytes)
{
#ifndef X86X64
	unsigned char *p = (unsigned char*)dst;

	switch (bytes)
	{
		case 4: 
			*p = (unsigned char)f;
			*(p + 1) = (unsigned char)(f >> 8);
			*(p + 2) = (unsigned char)(f >> 16);
			*(p + 3) = (unsigned char)(f >> 24);
			return;
		case 3:
			*p = (unsigned char)f;
			*(p + 1) = (unsigned char)(f >> 8);
			*(p + 2) = (unsigned char)(f >> 16);
			return;
		case 2:
			*p = (unsigned char)f;
			*(p + 1) = (unsigned char)(f >> 8);
			return;
		case 1:
			*p = (unsigned char)f;
			return;
	}
#else
	switch (bytes)
	{
		case 4: 
			*((ui32*)dst) = f;
			return;
		case 3:
			*((ui32*)dst) = f;
			return;
		case 2:
			*((ui16 *)dst) = (ui16)f;
			return;
		case 1:
			*((unsigned char*)dst) = (unsigned char)f;
			return;
	}
#endif
}


size_t qlz_size_decompressed(const char *source)
{
	ui32 n, r;
	n = (((*source) & 2) == 2) ? 4 : 1;
	r = fast_read(source + 1 + n, n);
	r = r & (0xffffffff >> ((4 - n)*8));
	return r;
}

size_t qlz_size_compressed(const char *source)
{
	ui32 n, r;
	n = (((*source) & 2) == 2) ? 4 : 1;
	r = fast_read(source + 1, n);
	r = r & (0xffffffff >> ((4 - n)*8));
	return r;
}

static
size_t qlz_size_header(const char *source)
{
	size_t n = 2*((((*source) & 2) == 2) ? 4 : 1) + 1;
	return n;
}


static __inline void memcpy_up(unsigned char *dst, const unsigned char *src, ui32 n)
{
	// Caution if modifying memcpy_up! Overlap of dst and src must be special handled.
#ifndef X86X64
	unsigned char *end = dst + n;
	while(dst < end)
	{
		*dst = *src;
		dst++;
		src++;
	}
#else
	ui32 f = 0;
	do
	{
		*(ui32 *)(dst + f) = *(ui32 *)(src + f);
		f += MINOFFSET + 1;
	}
	while (f < n);
#endif
}

static __inline void update_hash(qlz_state_decompress *state, const unsigned char *s)
{
#if QLZ_COMPRESSION_LEVEL == 1
	ui32 hash;
	hash = hashat(s);
	state->hash[hash].offset = s;
	state->hash_counter[hash] = 1;
#elif QLZ_COMPRESSION_LEVEL == 2
	ui32 hash;
	unsigned char c;
	hash = hashat(s);
	c = state->hash_counter[hash];
	state->hash[hash].offset[c & (QLZ_POINTERS - 1)] = s;
	c++;
	state->hash_counter[hash] = c;
#endif
	(void)state;
	(void)s;
}

#if QLZ_COMPRESSION_LEVEL <= 2
static void update_hash_upto(qlz_state_decompress *state, unsigned char **lh, const unsigned char *max)
{
	while(*lh < max)
	{
		(*lh)++;
		update_hash(state, *lh);
	}
}
#endif

static size_t qlz_compress_core(const unsigned char *source, unsigned char *destination, size_t size, qlz_state_compress *state)
{
	const unsigned char *last_byte = source + size - 1;
	const unsigned char *src = source;
	unsigned char *cword_ptr = destination;
	unsigned char *dst = destination + CWORD_LEN;
	ui32 cword_val = 1U << 31;
	const unsigned char *last_matchstart = last_byte - UNCONDITIONAL_MATCHLEN - UNCOMPRESSED_END; 
	ui32 fetch = 0;
	unsigned int lits = 0;

	(void) lits;

	if(src <= last_matchstart)
		fetch = fast_read(src, 3);
	
	while(src <= last_matchstart)
	{
		if ((cword_val & 1) == 1)
		{
			// store uncompressed if compression ratio is too low
			if (src > source + (size >> 1) && dst - destination > src - source - ((src - source) >> 5))
				return 0;

			fast_write((cword_val >> 1) | (1U << 31), cword_ptr, CWORD_LEN);

			cword_ptr = dst;
			dst += CWORD_LEN;
			cword_val = 1U << 31;
			fetch = fast_read(src, 3);
		}
#if QLZ_COMPRESSION_LEVEL == 1
		{
			const unsigned char *o;
			ui32 hash, cached;

			hash = hash_func(fetch);
			cached = fetch ^ state->hash[hash].cache;
			state->hash[hash].cache = fetch;

			o = state->hash[hash].offset + OFFSET_BASE;
			state->hash[hash].offset = CAST(src - OFFSET_BASE);

#ifdef X86X64
			if ((cached & 0xffffff) == 0 && o != OFFSET_BASE && (src - o > MINOFFSET || (src == o + 1 && lits >= 3 && src > source + 3 && same(src - 3, 6))))
			{
				if(cached != 0)
				{
#else
			if (cached == 0 && o != OFFSET_BASE && (src - o > MINOFFSET || (src == o + 1 && lits >= 3 && src > source + 3 && same(src - 3, 6))))
			{
				if (*(o + 3) != *(src + 3))
				{
#endif
					hash <<= 4;
					cword_val = (cword_val >> 1) | (1U << 31);
					fast_write((3 - 2) | hash, dst, 2);
					src += 3;
					dst += 2;
				}
				else
				{
					const unsigned char *old_src = src;
					size_t matchlen;
					hash <<= 4;

					cword_val = (cword_val >> 1) | (1U << 31);
					src += 4;

					if(*(o + (src - old_src)) == *src)
					{
						src++;
						if(*(o + (src - old_src)) == *src)
						{
							size_t q = last_byte - UNCOMPRESSED_END - (src - 5) + 1;
							size_t remaining = q > 255 ? 255 : q;
							src++;	
							while(*(o + (src - old_src)) == *src && (size_t)(src - old_src) < remaining)
								src++;
						}
					}

					matchlen = src - old_src;
					if (matchlen < 18)
					{
						fast_write((ui32)(matchlen - 2) | hash, dst, 2);
						dst += 2;
					} 
					else
					{
						fast_write((ui32)(matchlen << 16) | hash, dst, 3);
						dst += 3;
					}
				}
				fetch = fast_read(src, 3);
				lits = 0;
			}
			else
			{
				lits++;
				*dst = *src;
				src++;
				dst++;
				cword_val = (cword_val >> 1);
#ifdef X86X64
				fetch = fast_read(src, 3);
#else
				fetch = (fetch >> 8 & 0xffff) | (*(src + 2) << 16);
#endif
			}
		}
#elif QLZ_COMPRESSION_LEVEL >= 2
		{
			const unsigned char *o, *offset2;
			ui32 hash, matchlen, k, m, best_k = 0;
			unsigned char c;
			size_t remaining = (last_byte - UNCOMPRESSED_END - src + 1) > 255 ? 255 : (last_byte - UNCOMPRESSED_END - src + 1);
			(void)best_k;
		

			//hash = hashat(src);
			fetch = fast_read(src, 3);
			hash = hash_func(fetch);

			c = state->hash_counter[hash];

			offset2 = state->hash[hash].offset[0];
			if(offset2 < src - MINOFFSET && c > 0 && ((fast_read(offset2, 3) ^ fetch) & 0xffffff) == 0)
			{	
				matchlen = 3;
				if(*(offset2 + matchlen) == *(src + matchlen))
				{
					matchlen = 4;
					while(*(offset2 + matchlen) == *(src + matchlen) && matchlen < remaining)
						matchlen++;
				}
			}
			else
				matchlen = 0;
			for(k = 1; k < QLZ_POINTERS && c > k; k++)
			{
				o = state->hash[hash].offset[k];
#if QLZ_COMPRESSION_LEVEL == 3
				if(((fast_read(o, 3) ^ fetch) & 0xffffff) == 0 && o < src - MINOFFSET)
#elif QLZ_COMPRESSION_LEVEL == 2
				if(*(src + matchlen) == *(o + matchlen)	&& ((fast_read(o, 3) ^ fetch) & 0xffffff) == 0 && o < src - MINOFFSET)
#endif
				{	
					m = 3;
					while(*(o + m) == *(src + m) && m < remaining)
						m++;
#if QLZ_COMPRESSION_LEVEL == 3
					if ((m > matchlen) || (m == matchlen && o > offset2))
#elif QLZ_COMPRESSION_LEVEL == 2
					if (m > matchlen)
#endif
					{
						offset2 = o;
						matchlen = m;
						best_k = k;
					}
				}
			}
			o = offset2;
			state->hash[hash].offset[c & (QLZ_POINTERS - 1)] = src;
			c++;
			state->hash_counter[hash] = c;

#if QLZ_COMPRESSION_LEVEL == 3
			if(matchlen > 2 && src - o < 131071)
			{
				ui32 u;
				size_t offset = src - o;

				for(u = 1; u < matchlen; u++)
				{	
					hash = hashat(src + u);
					c = state->hash_counter[hash]++;
					state->hash[hash].offset[c & (QLZ_POINTERS - 1)] = src + u;
				}

				cword_val = (cword_val >> 1) | (1U << 31);
				src += matchlen;

				if(matchlen == 3 && offset <= 63)
				{
					*dst = (unsigned char)(offset << 2);
					dst++;
				}
				else if (matchlen == 3 && offset <= 16383)
				{
					ui32 f = (ui32)((offset << 2) | 1);
					fast_write(f, dst, 2);
					dst += 2;
				}		
				else if (matchlen <= 18 && offset <= 1023)
				{
					ui32 f = ((matchlen - 3) << 2) | ((ui32)offset << 6) | 2;
					fast_write(f, dst, 2);
					dst += 2;
				}

				else if(matchlen <= 33)
				{
					ui32 f = ((matchlen - 2) << 2) | ((ui32)offset << 7) | 3;
					fast_write(f, dst, 3);
					dst += 3;
				}
				else
				{
					ui32 f = ((matchlen - 3) << 7) | ((ui32)offset << 15) | 3;
					fast_write(f, dst, 4);
					dst += 4;
				}
			}
			else
			{
				*dst = *src;
				src++;
				dst++;
				cword_val = (cword_val >> 1);
			}
#elif QLZ_COMPRESSION_LEVEL == 2

			if(matchlen > 2)
			{
				cword_val = (cword_val >> 1) | (1U << 31);
				src += matchlen;			

				if (matchlen < 10)
				{			
					ui32 f = best_k | ((matchlen - 2) << 2) | (hash << 5);
					fast_write(f, dst, 2);
					dst += 2;
				}
				else
				{
					ui32 f = best_k | (matchlen << 16) | (hash << 5);
					fast_write(f, dst, 3);
					dst += 3;
				}
			}
			else
			{
				*dst = *src;
				src++;
				dst++;
				cword_val = (cword_val >> 1);
			}
#endif
		}
#endif
	}
	while (src <= last_byte)
	{
		if ((cword_val & 1) == 1)
		{
			fast_write((cword_val >> 1) | (1U << 31), cword_ptr, CWORD_LEN);
			cword_ptr = dst;
			dst += CWORD_LEN;
			cword_val = 1U << 31;
		}
#if QLZ_COMPRESSION_LEVEL < 3
		if (src <= last_byte - 3)
		{
#if QLZ_COMPRESSION_LEVEL == 1
			ui32 hash, fetchv;
			fetchv = fast_read(src, 3);
			hash = hash_func(fetch);
			state->hash[hash].offset = CAST(src - OFFSET_BASE);
			state->hash[hash].cache = fetchv;
#elif QLZ_COMPRESSION_LEVEL == 2
			ui32 hash;
			unsigned char c;
			hash = hashat(src);
			c = state->hash_counter[hash];
			state->hash[hash].offset[c & (QLZ_POINTERS - 1)] = src;
			c++;
			state->hash_counter[hash] = c;
#endif
		}
#endif
		*dst = *src;
		src++;
		dst++;
		cword_val = (cword_val >> 1);
	}

	while((cword_val & 1) != 1)
		cword_val = (cword_val >> 1);

	fast_write((cword_val >> 1) | (1U << 31), cword_ptr, CWORD_LEN);

	// min. size must be 9 bytes so that the qlz_size functions can take 9 bytes as argument
	return dst - destination < 9 ? 9 : dst - destination;
}

static size_t qlz_decompress_core(const unsigned char *source, unsigned char *destination, size_t size, qlz_state_decompress *state, const unsigned char *history)
{
	const unsigned char *src = source + qlz_size_header((const char *)source);
	unsigned char *dst = destination;
	const unsigned char *last_destination_byte = destination + size - 1;
	ui32 cword_val = 1;
	const unsigned char *last_matchstart = last_destination_byte - UNCONDITIONAL_MATCHLEN - UNCOMPRESSED_END;
	unsigned char *last_hashed = destination - 1;
	const unsigned char *last_source_byte = source + qlz_size_compressed((const char *)source) - 1;
	static const ui32 bitlut[16] = {4, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0};

	(void) last_source_byte;
	(void) last_hashed;
	(void) state;
	(void) history;

	for(;;) 
	{
		ui32 fetch;

		if (cword_val == 1)
		{
#ifdef QLZ_MEMORY_SAFE
			if(src + CWORD_LEN - 1 > last_source_byte)
				return 0;
#endif
			cword_val = fast_read(src, CWORD_LEN);
			src += CWORD_LEN;
		}

#ifdef QLZ_MEMORY_SAFE
			if(src + 4 - 1 > last_source_byte)
				return 0;
#endif

		fetch = fast_read(src, 4);

		if ((cword_val & 1) == 1)
		{
			ui32 matchlen;
			const unsigned char *offset2;

#if QLZ_COMPRESSION_LEVEL == 1
			ui32 hash;
			cword_val = cword_val >> 1;
			hash = (fetch >> 4) & 0xfff;
			offset2 = (const unsigned char *)(size_t)state->hash[hash].offset;

			if((fetch & 0xf) != 0)
			{
				matchlen = (fetch & 0xf) + 2;
				src += 2;
			}
			else
			{
				matchlen = *(src + 2);
				src += 3;							
			}	

#elif QLZ_COMPRESSION_LEVEL == 2
			ui32 hash;
			unsigned char c;
			cword_val = cword_val >> 1;
			hash = (fetch >> 5) & 0x7ff;
			c = (unsigned char)(fetch & 0x3);
			offset2 = state->hash[hash].offset[c];

			if((fetch & (28)) != 0)
			{
				matchlen = ((fetch >> 2) & 0x7) + 2;
				src += 2;
			}
			else
			{
				matchlen = *(src + 2);
				src += 3;							
			}	

#elif QLZ_COMPRESSION_LEVEL == 3
			ui32 offset;
			cword_val = cword_val >> 1;
			if ((fetch & 3) == 0)
			{
				offset = (fetch & 0xff) >> 2;
				matchlen = 3;
				src++;
			}
			else if ((fetch & 2) == 0)
			{
				offset = (fetch & 0xffff) >> 2;
				matchlen = 3;
				src += 2;
			}
			else if ((fetch & 1) == 0)
			{
				offset = (fetch & 0xffff) >> 6;
				matchlen = ((fetch >> 2) & 15) + 3;
				src += 2;
			}
			else if ((fetch & 127) != 3)
			{
				offset = (fetch >> 7) & 0x1ffff;
				matchlen = ((fetch >> 2) & 0x1f) + 2;
				src += 3;
			}
			else
			{
				offset = (fetch >> 15);
				matchlen = ((fetch >> 7) & 255) + 3;
				src += 4;
			}

			offset2 = dst - offset;
#endif
	
#ifdef QLZ_MEMORY_SAFE
			if(offset2 < history || offset2 > dst - MINOFFSET - 1)
				return 0;

			if(matchlen > (ui32)(last_destination_byte - dst - UNCOMPRESSED_END + 1))
				return 0;
#endif

			memcpy_up(dst, offset2, matchlen);
			dst += matchlen;

#if QLZ_COMPRESSION_LEVEL <= 2
			update_hash_upto(state, &last_hashed, dst - matchlen);
			last_hashed = dst - 1;
#endif
		}
		else
		{
			if (dst < last_matchstart)
			{
				unsigned int n = bitlut[cword_val & 0xf];
#ifdef X86X64
				*(ui32 *)dst = *(ui32 *)src;
#else
				memcpy_up(dst, src, 4);
#endif
				cword_val = cword_val >> n;
				dst += n;
				src += n;
#if QLZ_COMPRESSION_LEVEL <= 2
				update_hash_upto(state, &last_hashed, dst - 3);		
#endif
			}
			else
			{			
				while(dst <= last_destination_byte)
				{
					if (cword_val == 1)
					{
						src += CWORD_LEN;
						cword_val = 1U << 31;
					}
#ifdef QLZ_MEMORY_SAFE
					if(src >= last_source_byte + 1)
						return 0;
#endif
					*dst = *src;
					dst++;
					src++;
					cword_val = cword_val >> 1;
				}

#if QLZ_COMPRESSION_LEVEL <= 2
				update_hash_upto(state, &last_hashed, last_destination_byte - 3); // todo, use constant
#endif
				return size;
			}

		}
	}
}

size_t qlz_compress(const void *source, char *destination, size_t size, qlz_state_compress *state)
{
	size_t r;
	ui32 compressed;
	size_t base;

	if(size == 0 || size > 0xffffffff - 400)
		return 0;

	if(size < 216)
		base = 3;
	else
		base = 9;

#if QLZ_STREAMING_BUFFER > 0
	if (state->stream_counter + size - 1 >= QLZ_STREAMING_BUFFER)
#endif
	{
		reset_table_compress(state);
		r = base + qlz_compress_core((const unsigned char *)source, (unsigned char*)destination + base, size, state);
#if QLZ_STREAMING_BUFFER > 0
		reset_table_compress(state);
#endif
		if(r == base)
		{
			memcpy(destination + base, source, size);
			r = size + base;
			compressed = 0;
		}
		else
		{
			compressed = 1;
		}
		state->stream_counter = 0;
	}
#if QLZ_STREAMING_BUFFER > 0
	else
	{
		unsigned char *src = state->stream_buffer + state->stream_counter;

		memcpy(src, source, size);
		r = base + qlz_compress_core(src, (unsigned char*)destination + base, size, state);

 		if(r == base)
		{
			memcpy(destination + base, src, size);
			r = size + base;
			compressed = 0;
			reset_table_compress(state);
		}
		else
		{
			compressed = 1;
		}
		state->stream_counter += size;
	}
#endif
	if(base == 3)
	{
		*destination = (unsigned char)(0 | compressed);
		*(destination + 1) = (unsigned char)r;
		*(destination + 2) = (unsigned char)size;
	}
	else
	{
		*destination = (unsigned char)(2 | compressed);
		fast_write((ui32)r, destination + 1, 4);
		fast_write((ui32)size, destination + 5, 4);
	}
	
	*destination |= (QLZ_COMPRESSION_LEVEL << 2);
	*destination |= (1 << 6);
	*destination |= ((QLZ_STREAMING_BUFFER == 0 ? 0 : (QLZ_STREAMING_BUFFER == 100000 ? 1 : (QLZ_STREAMING_BUFFER == 1000000 ? 2 : 3))) << 4);

// 76543210
// 01SSLLHC

	return r;
}

size_t qlz_decompress(const char *source, void *destination, qlz_state_decompress *state)
{
	size_t dsiz = qlz_size_decompressed(source);

#if QLZ_STREAMING_BUFFER > 0
	if (state->stream_counter + qlz_size_decompressed(source) - 1 >= QLZ_STREAMING_BUFFER) 
#endif
	{
		if((*source & 1) == 1)
		{
			reset_table_decompress(state);
			dsiz = qlz_decompress_core((const unsigned char *)source, (unsigned char *)destination, dsiz, state, (const unsigned char *)destination);
		}
		else
		{
			memcpy(destination, source + qlz_size_header(source), dsiz);
		}
		state->stream_counter = 0;
		reset_table_decompress(state);
	}
#if QLZ_STREAMING_BUFFER > 0
	else
	{
		unsigned char *dst = state->stream_buffer + state->stream_counter;
		if((*source & 1) == 1)
		{
			dsiz = qlz_decompress_core((const unsigned char *)source, dst, dsiz, state, (const unsigned char *)state->stream_buffer);
		}
		else
		{
			memcpy(dst, source + qlz_size_header(source), dsiz);
			reset_table_decompress(state);
		}
		memcpy(destination, dst, dsiz);
		state->stream_counter += dsiz;
	}
#endif
	return dsiz;
}

