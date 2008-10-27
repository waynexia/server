/* Copyright (C) 2008 MySQL AB

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

#ifndef _waiting_threads_h
#define _waiting_threads_h

#include <my_global.h>
#include <my_sys.h>

#include <lf.h>

C_MODE_START

typedef struct st_wt_resource_id WT_RESOURCE_ID;

typedef struct st_wt_resource_type {
  int (*compare)(void *a, void *b);
  const void *(*make_key)(WT_RESOURCE_ID *id, uint *len);
} WT_RESOURCE_TYPE;

/* we want to compare this struct with memcmp, make it packed */
#pragma pack(1)
struct st_wt_resource_id {
  ulonglong value;
  WT_RESOURCE_TYPE *type;
};
#pragma pack()

#define WT_WAIT_STATS  24
#define WT_CYCLE_STATS 32
extern ulonglong wt_wait_table[WT_WAIT_STATS];
extern uint32    wt_wait_stats[WT_WAIT_STATS+1];
extern uint32    wt_cycle_stats[2][WT_CYCLE_STATS+1];
extern uint32    wt_success_stats;

/*
  'lock' protects 'owners', 'state', and 'waiter_count'
  'id' is read-only

  a resource is picked up from a hash in a lock-free manner
  it's returned pinned, so it cannot be freed at once
  but it may be freed right after the pin is removed
  to free a resource it should be
    1. have no owners
    2. have no waiters

  two ways to access a resource:
    1. find it in a hash
       - it's returned pinned.
        a) take a lock in exclusive mode
        b) check the state, it should be ACTIVE
        c) unpin
    2. by a direct reference
       - could only used if a resource cannot be freed
       e.g. accessing a resource by thd->waiting_for is safe,
       a resource cannot be freed as there's a thread waiting for it
*/
typedef struct st_wt_resource {
  WT_RESOURCE_ID  id;
  uint            waiter_count;
  enum { ACTIVE, FREE } state;
#ifndef DBUG_OFF
  pthread_mutex_t  *mutex;
#endif
  /*
    before the 'lock' all elements are mutable, after (and including) -
    immutable in the sense that lf_hash_insert() won't memcpy() over them.
    See wt_init().
  */
#ifdef WT_RWLOCKS_USE_MUTEXES
  /*
    we need a special rwlock-like 'lock' to allow readers bypass
    waiting writers, otherwise readers can deadlock.
    writer starvation is technically possible, but unlikely, because
    the contention is expected to be low.
  */
  struct {
    pthread_cond_t   cond;
    pthread_mutex_t  mutex;
    uint readers: 16;
    uint pending_writers: 15;
    uint write_locked: 1;
  } lock;
#else
  rw_lock_t lock;
#endif
  pthread_cond_t   cond;
  DYNAMIC_ARRAY    owners;
} WT_RESOURCE;

typedef struct st_wt_thd {
  /*
    XXX
    there's no protection (mutex) against concurrent access of
    the dynarray below. it is assumed that a caller will have it
    automatically (not to protect this array but to protect its
    own - caller's - data structures, and we'll get it for free.
    If not, we'll need to add a mutex
  */
  DYNAMIC_ARRAY   my_resources;
  /*
    'waiting_for' is modified under waiting_for->lock, and only by thd itself
    'waiting_for' is read lock-free (using pinning protocol), but a thd object
    can read its own 'waiting_for' without any locks or tricks.
  */
  WT_RESOURCE    *waiting_for;
  LF_PINS        *pins;

  /* pointers to values */
  ulong *timeout_short, *deadlock_search_depth_short;
  ulong *timeout_long, *deadlock_search_depth_long;

  /*
    weight relates to the desirability of a transaction being killed if it's
    part of a deadlock. In a deadlock situation transactions with lower weights
    are killed first.

    Examples of using the weight to implement different selection strategies:

    1. Latest
        Keep all weights equal.
    2. Random
        Assight weights at random.
        (variant: modify a weight randomly before every lock request)
    3. Youngest
        Set weight to -NOW()
    4. Minimum locks
        count locks granted in your lock manager, store the value as a weight
    5. Minimum work
        depends on the definition of "work". For example, store the number
        of rows modifies in this transaction (or a length of REDO log for a
        transaction) as a weight.

    It is only statistically relevant and is not protected by any locks.
  */
  ulong volatile weight;
  /*
    'killed' is indirectly protected by waiting_for->lock -
    a killed thread needs to clear its 'waiting_for', and thus needs a lock.
    That is a thread needs an exclusive lock to read 'killed' reliably.
    But other threads may change 'killed' from 0 to 1, a shared
    lock is enough for that.
   */
  my_bool volatile killed;
#ifndef DBUG_OFF
  const char     *name;
#endif
} WT_THD;

#define WT_TIMEOUT              ETIMEDOUT
#define WT_OK                   0
#define WT_DEADLOCK             -1
#define WT_DEPTH_EXCEEDED       -2

void wt_init(void);
void wt_end(void);
void wt_thd_lazy_init(WT_THD *, ulong *, ulong *, ulong *, ulong *);
void wt_thd_destroy(WT_THD *);
int wt_thd_will_wait_for(WT_THD *, WT_THD *, WT_RESOURCE_ID *);
int wt_thd_cond_timedwait(WT_THD *, pthread_mutex_t *);
void wt_thd_release(WT_THD *, WT_RESOURCE_ID *);
#define wt_thd_release_all(THD) wt_thd_release((THD), 0)
int wt_resource_id_memcmp(void *, void *);

C_MODE_END

#endif
