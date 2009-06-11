/*****************************************************************************

Copyright (c) 1994, 2009, Innobase Oy. All Rights Reserved.

This program is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free Software
Foundation; version 2 of the License.

This program is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with
this program; if not, write to the Free Software Foundation, Inc., 59 Temple
Place, Suite 330, Boston, MA 02111-1307 USA

*****************************************************************************/

/**********************************************************************
Random numbers and hashing

Created 1/20/1994 Heikki Tuuri
***********************************************************************/

#ifndef ut0rnd_h
#define ut0rnd_h

#include "univ.i"

#include "ut0byte.h"

/* The 'character code' for end of field or string (used
in folding records */
#define UT_END_OF_FIELD		257

/************************************************************
This is used to set the random number seed. */
UNIV_INLINE
void
ut_rnd_set_seed(
/*============*/
	ulint	 seed);		 /* in: seed */
/************************************************************
The following function generates a series of 'random' ulint integers. */
UNIV_INLINE
ulint
ut_rnd_gen_next_ulint(
/*==================*/
			/* out: the next 'random' number */
	ulint	rnd);	/* in: the previous random number value */
/*************************************************************
The following function generates 'random' ulint integers which
enumerate the value space (let there be N of them) of ulint integers
in a pseudo-random fashion. Note that the same integer is repeated
always after N calls to the generator. */
UNIV_INLINE
ulint
ut_rnd_gen_ulint(void);
/*==================*/
			/* out: the 'random' number */
/************************************************************
Generates a random integer from a given interval. */
UNIV_INLINE
ulint
ut_rnd_interval(
/*============*/
			/* out: the 'random' number */
	ulint	low,	/* in: low limit; can generate also this value */
	ulint	high);	/* in: high limit; can generate also this value */
/*************************************************************
Generates a random iboolean value. */
UNIV_INLINE
ibool
ut_rnd_gen_ibool(void);
/*=================*/
			/* out: the random value */
/***********************************************************
The following function generates a hash value for a ulint integer
to a hash table of size table_size, which should be a prime or some
random number to work reliably. */
UNIV_INLINE
ulint
ut_hash_ulint(
/*==========*/
				/* out: hash value */
	ulint	 key,		/* in: value to be hashed */
	ulint	 table_size);	/* in: hash table size */
/*****************************************************************
Folds a pair of ulints. */
UNIV_INLINE
ulint
ut_fold_ulint_pair(
/*===============*/
			/* out: folded value */
	ulint	n1,	/* in: ulint */
	ulint	n2)	/* in: ulint */
	__attribute__((const));
/*****************************************************************
Folds a dulint. */
UNIV_INLINE
ulint
ut_fold_dulint(
/*===========*/
			/* out: folded value */
	dulint	d)	/* in: dulint */
	__attribute__((const));
/*****************************************************************
Folds a character string ending in the null character. */
UNIV_INLINE
ulint
ut_fold_string(
/*===========*/
				/* out: folded value */
	const char*	str)	/* in: null-terminated string */
	__attribute__((pure));
/*****************************************************************
Folds a binary string. */
UNIV_INLINE
ulint
ut_fold_binary(
/*===========*/
				/* out: folded value */
	const byte*	str,	/* in: string of bytes */
	ulint		len)	/* in: length */
	__attribute__((pure));
/***************************************************************
Looks for a prime number slightly greater than the given argument.
The prime is chosen so that it is not near any power of 2. */
UNIV_INTERN
ulint
ut_find_prime(
/*==========*/
			/* out: prime */
	ulint	n)	/* in: positive number > 100 */
	__attribute__((const));


#ifndef UNIV_NONINL
#include "ut0rnd.ic"
#endif

#endif
