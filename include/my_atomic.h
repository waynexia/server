/* Copyright (C) 2006 MySQL AB

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA */

/*
  This header defines five atomic operations:

  my_atomic_add#(&var, what)
    add 'what' to *var, and return the old value of *var

  my_atomic_fas#(&var, what)
    'Fetch And Store'
    store 'what' in *var, and return the old value of *var

  my_atomic_cas#(&var, &old, new)
    'Compare And Swap'
    if *var is equal to *old, then store 'new' in *var, and return TRUE
    otherwise store *var in *old, and return FALSE

  my_atomic_load#(&var)
    return *var

  my_atomic_store#(&var, what)
    store 'what' in *var

  '#' is substituted by a size suffix - 8, 16, 32, or ptr
  (e.g. my_atomic_add8, my_atomic_fas32, my_atomic_casptr).

  NOTE This operations are not always atomic, so they always must be
  enclosed in my_atomic_rwlock_rdlock(lock)/my_atomic_rwlock_rdunlock(lock)
  or my_atomic_rwlock_wrlock(lock)/my_atomic_rwlock_wrunlock(lock).
  Hint: if a code block makes intensive use of atomic ops, it make sense
  to take/release rwlock once for the whole block, not for every statement.

  On architectures where these operations are really atomic, rwlocks will
  be optimized away.
*/

#ifndef my_atomic_rwlock_init

#define intptr         void *

#ifndef MY_ATOMIC_MODE_RWLOCKS
#include "atomic/nolock.h"
#endif

#ifndef make_atomic_cas_body
#include "atomic/rwlock.h"
#endif

#ifndef make_atomic_add_body
#define make_atomic_add_body(S)                                 \
  int ## S tmp=*a;                                              \
  while (!my_atomic_cas ## S(a, &tmp, tmp+v));                  \
  v=tmp;
#endif

#ifdef __GNUC__
/*
  we want to be able to use my_atomic_xxx functions with
  both signed and unsigned integers. But gcc will issue a warning
  "passing arg N of `my_atomic_XXX' as [un]signed due to prototype"
  if the signedness of the argument doesn't match the prototype, or
  "pointer targets in passing argument N of my_atomic_XXX differ in signedness"
  if int* is used where uint* is expected (or vice versa).
  Let's shut these warnings up
*/
#define make_transparent_unions(S)                              \
        typedef union {                                         \
          int  ## S  i;                                         \
          uint ## S  u;                                         \
        } U_ ## S   __attribute__ ((transparent_union));        \
        typedef union {                                         \
          int  ## S volatile *i;                                \
          uint ## S volatile *u;                                \
        } Uv_ ## S   __attribute__ ((transparent_union));
#define uintptr intptr
make_transparent_unions(8)
make_transparent_unions(16)
make_transparent_unions(32)
make_transparent_unions(ptr)
#undef uintptr
#undef make_transparent_unions
#define a       U_a.i
#define cmp     U_cmp.i
#define v       U_v.i
#define set     U_set.i
#else
#define U_8    int8
#define U_16   int16
#define U_32   int32
#define U_ptr  intptr
#define Uv_8   int8
#define Uv_16  int16
#define Uv_32  int32
#define Uv_ptr intptr
#define U_a    volatile *a
#define U_cmp  *cmp
#define U_v    v
#define U_set  set
#endif /* __GCC__ transparent_union magic */

#ifdef HAVE_INLINE

#define make_atomic_add(S)                                      \
STATIC_INLINE int ## S my_atomic_add ## S(                      \
                        Uv_ ## S U_a, U_ ## S U_v)              \
{                                                               \
  make_atomic_add_body(S);                                      \
  return v;                                                     \
}

#define make_atomic_fas(S)                                      \
STATIC_INLINE int ## S my_atomic_fas ## S(                      \
                         Uv_ ## S U_a, U_ ## S U_v)             \
{                                                               \
  make_atomic_fas_body(S);                                      \
  return v;                                                     \
}

#define make_atomic_cas(S)                                      \
STATIC_INLINE int my_atomic_cas ## S(Uv_ ## S U_a,              \
                            Uv_ ## S U_cmp, U_ ## S U_set)      \
{                                                               \
  int8 ret;                                                     \
  make_atomic_cas_body(S);                                      \
  return ret;                                                   \
}

#define make_atomic_load(S)                                     \
STATIC_INLINE int ## S my_atomic_load ## S(Uv_ ## S U_a)        \
{                                                               \
  int ## S ret;                                                 \
  make_atomic_load_body(S);                                     \
  return ret;                                                   \
}

#define make_atomic_store(S)                                    \
STATIC_INLINE void my_atomic_store ## S(                        \
                     Uv_ ## S U_a, U_ ## S U_v)                 \
{                                                               \
  make_atomic_store_body(S);                                    \
}

#else /* no inline functions */

#define make_atomic_add(S)                                      \
extern int ## S my_atomic_add ## S(Uv_ ## S, U_ ## S);

#define make_atomic_fas(S)                                      \
extern int ## S my_atomic_fas ## S(Uv_ ## S, U_ ## S);

#define make_atomic_cas(S)                                      \
extern int my_atomic_cas ## S(Uv_ ## S, Uv_ ## S, U_ ## S);

#define make_atomic_load(S)                                     \
extern int ## S my_atomic_load ## S(Uv_ ## S);

#define make_atomic_store(S)                                    \
extern void my_atomic_store ## S(Uv_ ## S, U_ ## S);

#endif

make_atomic_cas( 8)
make_atomic_cas(16)
make_atomic_cas(32)
make_atomic_cas(ptr)

make_atomic_add( 8)
make_atomic_add(16)
make_atomic_add(32)

make_atomic_load( 8)
make_atomic_load(16)
make_atomic_load(32)
make_atomic_load(ptr)

make_atomic_store( 8)
make_atomic_store(16)
make_atomic_store(32)
make_atomic_store(ptr)

make_atomic_fas( 8)
make_atomic_fas(16)
make_atomic_fas(32)
make_atomic_fas(ptr)

#ifdef _atomic_h_cleanup_
#include _atomic_h_cleanup_
#undef _atomic_h_cleanup_
#endif

#undef U_8
#undef U_16
#undef U_32
#undef U_ptr
#undef a
#undef cmp
#undef v
#undef set
#undef U_a
#undef U_cmp
#undef U_v
#undef U_set
#undef make_atomic_add
#undef make_atomic_cas
#undef make_atomic_load
#undef make_atomic_store
#undef make_atomic_fas
#undef make_atomic_add_body
#undef make_atomic_cas_body
#undef make_atomic_load_body
#undef make_atomic_store_body
#undef make_atomic_fas_body
#undef intptr

#ifndef LF_BACKOFF
#define LF_BACKOFF (1)
#endif

#define MY_ATOMIC_OK       0
#define MY_ATOMIC_NOT_1CPU 1
extern int my_atomic_initialize();

#endif

