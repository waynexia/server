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


#include <my_global.h>
#include <my_sys.h>
#include <m_string.h>
#include "trnman.h"

/*
  status variables:
  how many trns in the active list currently,
  in the committed list currently, allocated since startup.
*/
uint trnman_active_transactions, trnman_committed_transactions,
  trnman_allocated_transactions;

/* list of active transactions in the trid order */
static TRN active_list_min, active_list_max;
/* list of committed transactions in the trid order */
static TRN committed_list_min, committed_list_max;

/* a counter, used to generate transaction ids */
static TrID global_trid_generator;

/* the mutex for everything above */
static pthread_mutex_t LOCK_trn_list;

/* LIFO pool of unused TRN structured for reuse */
static TRN *pool;

/* a hash for committed transactions that maps trid to a TRN structure */
static LF_HASH trid_to_committed_trn;

/* an array that maps short_trid of an active transaction to a TRN structure */
static TRN **short_trid_to_active_trn;

/* locks for short_trid_to_active_trn and pool */
static my_atomic_rwlock_t LOCK_short_trid_to_trn, LOCK_pool;

/*
  Simple interface functions
  QQ: if they stay so simple, should we make them inline?
*/

uint trnman_increment_locked_tables(TRN *trn)
{
  return trn->locked_tables++;
}

my_bool trnman_has_locked_tables(TRN *trn)
{
  return trn->locked_tables != 0;
}

uint trnman_decrement_locked_tables(TRN *trn)
{
  return --trn->locked_tables;
}

void trnman_reset_locked_tables(TRN *trn)
{
  trn->locked_tables= 0;
}


/*
  NOTE
    Just as short_id doubles as loid, this function doubles as
    short_trid_to_LOCK_OWNER. See the compile-time assert below.
*/

#ifdef NOT_USED
static TRN *short_trid_to_TRN(uint16 short_trid)
{
  TRN *trn;
  compile_time_assert(offsetof(TRN, locks) == 0);
  my_atomic_rwlock_rdlock(&LOCK_short_trid_to_trn);
  trn= my_atomic_loadptr((void **)&short_trid_to_active_trn[short_trid]);
  my_atomic_rwlock_rdunlock(&LOCK_short_trid_to_trn);
  return (TRN *)trn;
}
#endif

static byte *trn_get_hash_key(const byte *trn, uint* len,
                              my_bool unused __attribute__ ((unused)))
{
  *len= sizeof(TrID);
  return (byte *) & ((*((TRN **)trn))->trid);
}

int trnman_init()
{
  DBUG_ENTER("trnman_init");

  short_trid_to_active_trn= (TRN **)my_malloc(SHORT_TRID_MAX*sizeof(TRN*),
                                     MYF(MY_WME|MY_ZEROFILL));
  if (unlikely(!short_trid_to_active_trn))
    DBUG_RETURN(1);
  short_trid_to_active_trn--; /* min short_trid is 1 */

  /*
    Initialize lists.
    active_list_max.min_read_from must be larger than any trid,
    so that when an active list is empty we would could free
    all committed list.
    And  committed_list_max itself can not be freed so
    committed_list_max.commit_trid must not be smaller that
    active_list_max.min_read_from
  */

  active_list_max.trid= active_list_min.trid= 0;
  active_list_max.min_read_from= ~(ulong) 0;
  active_list_max.next= active_list_min.prev= 0;
  active_list_max.prev= &active_list_min;
  active_list_min.next= &active_list_max;

  committed_list_max.commit_trid= ~(ulong) 0;
  committed_list_max.next= committed_list_min.prev= 0;
  committed_list_max.prev= &committed_list_min;
  committed_list_min.next= &committed_list_max;

  trnman_active_transactions= 0;
  trnman_committed_transactions= 0;
  trnman_allocated_transactions= 0;

  pool= 0;
  global_trid_generator= 0; /* set later by the recovery code */
  lf_hash_init(&trid_to_committed_trn, sizeof(TRN*), LF_HASH_UNIQUE,
               0, 0, trn_get_hash_key, 0);
  DBUG_PRINT("info", ("pthread_mutex_init LOCK_trn_list"));
  pthread_mutex_init(&LOCK_trn_list, MY_MUTEX_INIT_FAST);
  my_atomic_rwlock_init(&LOCK_short_trid_to_trn);
  my_atomic_rwlock_init(&LOCK_pool);

#ifdef NOT_USED
  lockman_init(&maria_lockman, (loid_to_lo_func *)&short_trid_to_TRN, 10000);
#endif

  DBUG_RETURN(0);
}

/*
  NOTE
    this could only be called in the "idle" state - no transaction can be
    running. See asserts below.
*/
void trnman_destroy()
{
  DBUG_ENTER("trnman_destroy");

  if (short_trid_to_active_trn == NULL) /* trnman already destroyed */
    DBUG_VOID_RETURN;
  DBUG_ASSERT(trid_to_committed_trn.count == 0);
  DBUG_ASSERT(trnman_active_transactions == 0);
  DBUG_ASSERT(trnman_committed_transactions == 0);
  DBUG_ASSERT(active_list_max.prev == &active_list_min);
  DBUG_ASSERT(active_list_min.next == &active_list_max);
  DBUG_ASSERT(committed_list_max.prev == &committed_list_min);
  DBUG_ASSERT(committed_list_min.next == &committed_list_max);
  while (pool)
  {
    TRN *trn= pool;
    pool= pool->next;
    DBUG_ASSERT(trn->locks.mutex == 0);
    DBUG_ASSERT(trn->locks.cond == 0);
    my_free((void *)trn, MYF(0));
  }
  lf_hash_destroy(&trid_to_committed_trn);
  DBUG_PRINT("info", ("pthread_mutex_destroy LOCK_trn_list"));
  pthread_mutex_destroy(&LOCK_trn_list);
  my_atomic_rwlock_destroy(&LOCK_short_trid_to_trn);
  my_atomic_rwlock_destroy(&LOCK_pool);
  my_free((void *)(short_trid_to_active_trn+1), MYF(0));
  short_trid_to_active_trn= NULL;
#ifdef NOT_USED
  lockman_destroy(&maria_lockman);
#endif
  DBUG_VOID_RETURN;
}

/*
  NOTE
    TrID is limited to 6 bytes. Initial value of the generator
    is set by the recovery code - being read from the last checkpoint
    (or 1 on a first run).
*/
static TrID new_trid()
{
  DBUG_ENTER("new_trid");
  DBUG_ASSERT(global_trid_generator < 0xffffffffffffLL);
  DBUG_PRINT("info", ("safe_mutex_assert_owner LOCK_trn_list"));
  safe_mutex_assert_owner(&LOCK_trn_list);
  DBUG_RETURN(++global_trid_generator);
}

static void set_short_trid(TRN *trn)
{
  int i= (global_trid_generator + (intptr)trn) * 312089 % SHORT_TRID_MAX + 1;
  my_atomic_rwlock_wrlock(&LOCK_short_trid_to_trn);
  for ( ; ; i= i % SHORT_TRID_MAX + 1) /* the range is [1..SHORT_TRID_MAX] */
  {
    void *tmp= NULL;
    if (short_trid_to_active_trn[i] == NULL &&
        my_atomic_casptr((void **)&short_trid_to_active_trn[i], &tmp, trn))
      break;
  }
  my_atomic_rwlock_wrunlock(&LOCK_short_trid_to_trn);
  trn->short_id= i;
}

/*
  DESCRIPTION
    start a new transaction, allocate and initialize transaction object
    mutex and cond will be used for lock waits
*/

TRN *trnman_new_trn(pthread_mutex_t *mutex, pthread_cond_t *cond,
                    void *stack_end)
{
  TRN *trn;
  DBUG_ENTER("trnman_new_trn");

  /*
    we have a mutex, to do simple things under it - allocate a TRN,
    increment trnman_active_transactions, set trn->min_read_from.

    Note that all the above is fast. generating short_trid may be slow,
    as it involves scanning a large array - so it's done outside of the
    mutex.
  */

  DBUG_PRINT("info", ("pthread_mutex_lock LOCK_trn_list"));
  pthread_mutex_lock(&LOCK_trn_list);

  /* Allocating a new TRN structure */
  trn= pool;
  /*
    Popping an unused TRN from the pool
    (ABA isn't possible, we're behind a mutex
  */
  my_atomic_rwlock_wrlock(&LOCK_pool);
  while (trn && !my_atomic_casptr((void **)&pool, (void **)&trn,
                                  (void *)trn->next))
    /* no-op */;
  my_atomic_rwlock_wrunlock(&LOCK_pool);

  /* Nothing in the pool ? Allocate a new one */
  if (!trn)
  {
    /*
      trn should be completely initalized at create time to allow
      one to keep a known state on it.
      (Like redo_lns, which is assumed to be 0 at start of row handling
      and reset to zero before end of row handling)
    */
    trn= (TRN *)my_malloc(sizeof(TRN), MYF(MY_WME | MY_ZEROFILL));
    if (unlikely(!trn))
    {
      DBUG_PRINT("info", ("pthread_mutex_unlock LOCK_trn_list"));
      pthread_mutex_unlock(&LOCK_trn_list);
      return 0;
    }
    trnman_allocated_transactions++;
  }
  trn->pins= lf_hash_get_pins(&trid_to_committed_trn, stack_end);
  if (!trn->pins)
  {
    trnman_free_trn(trn);
    return 0;
  }

  trnman_active_transactions++;

  trn->min_read_from= active_list_min.next->trid;

  trn->trid= new_trid();
  trn->short_id= 0;

  trn->next= &active_list_max;
  trn->prev= active_list_max.prev;
  active_list_max.prev= trn->prev->next= trn;
  DBUG_PRINT("info", ("pthread_mutex_unlock LOCK_trn_list"));
  pthread_mutex_unlock(&LOCK_trn_list);

  if (unlikely(!trn->min_read_from))
    trn->min_read_from= trn->trid;

  trn->commit_trid= 0;
  trn->rec_lsn= trn->undo_lsn= trn->first_undo_lsn= 0;

  trn->locks.mutex= mutex;
  trn->locks.cond= cond;
  trn->locks.waiting_for= 0;
  trn->locks.all_locks= 0;
#ifdef NOT_USED
  trn->locks.pins= lf_alloc_get_pins(&maria_lockman.alloc);
#endif

  trn->locked_tables= 0;

  /*
    only after the following function TRN is considered initialized,
    so it must be done the last
  */
  set_short_trid(trn);

  DBUG_RETURN(trn);
}

/*
  remove a trn from the active list.
  if necessary - move to committed list and set commit_trid

  NOTE
    Locks are released at the end. In particular, after placing the
    transaction in commit list, and after setting commit_trid. It's
    important, as commit_trid affects visibility.  Locks don't affect
    anything they simply delay execution of other threads - they could be
    released arbitrarily late. In other words, when locks are released it
    serves as a start banner for other threads, they start to run. So
    everything they may need must be ready at that point.

  RETURN
    0  ok
    1  error
*/
int trnman_end_trn(TRN *trn, my_bool commit)
{
  int res= 1;
  TRN *free_me= 0;
  LF_PINS *pins= trn->pins;
  DBUG_ENTER("trnman_end_trn");

  DBUG_ASSERT(trn->rec_lsn == 0);
  /* if a rollback, all UNDO records should have been executed */
  DBUG_ASSERT(commit || trn->undo_lsn == 0);
  DBUG_PRINT("info", ("pthread_mutex_lock LOCK_trn_list"));
  pthread_mutex_lock(&LOCK_trn_list);

  /* remove from active list */
  trn->next->prev= trn->prev;
  trn->prev->next= trn->next;

  /*
    if trn was the oldest active transaction, now that it goes away there
    may be committed transactions in the list which no active transaction
    needs to bother about - clean up the committed list
  */
  if (trn->prev == &active_list_min)
  {
    uint free_me_count;
    TRN *t;
    for (t= committed_list_min.next, free_me_count= 0;
         t->commit_trid < active_list_min.next->min_read_from;
         t= t->next, free_me_count++) /* no-op */;

    DBUG_ASSERT((t != committed_list_min.next && free_me_count > 0) ||
                (t == committed_list_min.next && free_me_count == 0));
    /* found transactions committed before the oldest active one */
    if (t != committed_list_min.next)
    {
      free_me= committed_list_min.next;
      committed_list_min.next= t;
      t->prev->next= 0;
      t->prev= &committed_list_min;
      trnman_committed_transactions-= free_me_count;
    }
  }

  /*
    if transaction is committed and it was not the only active transaction -
    add it to the committed list (which is used for read-from relation)
  */
  if (commit && active_list_min.next != &active_list_max)
  {
    trn->commit_trid= global_trid_generator;
    trn->next= &committed_list_max;
    trn->prev= committed_list_max.prev;
    trnman_committed_transactions++;

    res= lf_hash_insert(&trid_to_committed_trn, pins, &trn);
    /*
      By going on with life is res<0, we let other threads block on
      our rows (because they will never see us committed in
      trid_to_committed_trn) until they timeout. Though correct, this is not a
      good situation:
      - if connection reconnects and wants to check if its rows have been
      committed, it will not be able to do that (it will just lock on them) so
      connection stays permanently in doubt
      - internal structures trid_to_committed_trn and committed_list are
      desynchronized.
      So we should take Maria down immediately, the two problems being
      automatically solved at restart.
    */
    DBUG_ASSERT(res <= 0);
  }
  if (res)
  {
    /*
      res == 1 means the condition in the if() above
      was false.
      res == -1 means lf_hash_insert failed
    */
    trn->next= free_me;
    free_me= trn;
  }
  else
  {
    committed_list_max.prev= trn->prev->next= trn;
  }
  trnman_active_transactions--;
  DBUG_PRINT("info", ("pthread_mutex_unlock LOCK_trn_list"));
  pthread_mutex_unlock(&LOCK_trn_list);

  /* the rest is done outside of a critical section */
#ifdef NOT_USED
  lockman_release_locks(&maria_lockman, &trn->locks);
#endif
  trn->locks.mutex= 0;
  trn->locks.cond= 0;
  my_atomic_rwlock_rdlock(&LOCK_short_trid_to_trn);
  my_atomic_storeptr((void **)&short_trid_to_active_trn[trn->short_id], 0);
  my_atomic_rwlock_rdunlock(&LOCK_short_trid_to_trn);

  /*
    we, under the mutex, removed going-in-free_me transactions from the
    active and committed lists, thus nobody else may see them when it scans
    those lists, and thus nobody may want to free them. Now we don't
    need a mutex to access free_me list
  */
  /* QQ: send them to the purge thread */
  while (free_me)
  {
    TRN *t= free_me;
    free_me= free_me->next;

    /*
      ignore OOM here. it's harmless, and there's nothing we could do, anyway
    */
    (void)lf_hash_delete(&trid_to_committed_trn, pins, &t->trid, sizeof(TrID));

    trnman_free_trn(t);
  }

  lf_hash_put_pins(pins);
#ifdef NOT_USED
  lf_pinbox_put_pins(trn->locks.pins);
#endif

  DBUG_RETURN(res < 0);
}

/*
  free a trn (add to the pool, that is)
  note - we can never really free() a TRN if there's at least one other
  running transaction - see, e.g., how lock waits are implemented in
  lockman.c
  The same is true for other lock-free data structures too. We may need some
  kind of FLUSH command to reset them all - ensuring that no transactions are
  running. It may even be called automatically on checkpoints if no
  transactions are running.
*/
void trnman_free_trn(TRN *trn)
{
  TRN *tmp= pool;

  my_atomic_rwlock_wrlock(&LOCK_pool);
  do
  {
    /*
      without this volatile cast gcc-3.4.4 moved the assignment
      down after the loop at -O2
    */
    *(TRN * volatile *)&(trn->next)= tmp;
  } while (!my_atomic_casptr((void **)&pool, (void **)&tmp, trn));
  my_atomic_rwlock_wrunlock(&LOCK_pool);
}

/*
  NOTE
    here we access the hash in a lock-free manner.
    It's safe, a 'found' TRN can never be freed/reused before we access it.
    In fact, it cannot be freed before 'trn' ends, because a 'found' TRN
    can only be removed from the hash when:
                found->commit_trid < ALL (trn->min_read_from)
    that is, at least
                found->commit_trid < trn->min_read_from
    but
                found->trid >= trn->min_read_from
    and
                found->commit_trid > found->trid

  RETURN
    1   can
    0   cannot
   -1   error (OOM)
*/
int trnman_can_read_from(TRN *trn, TrID trid)
{
  TRN **found;
  my_bool can;
  LF_REQUIRE_PINS(3);

  if (trid < trn->min_read_from)
    return 1; /* can read */
  if (trid > trn->trid)
    return 0; /* cannot read */

  found= lf_hash_search(&trid_to_committed_trn, trn->pins, &trid, sizeof(trid));
  if (found == NULL)
    return 0; /* not in the hash of committed transactions = cannot read */
  if (found == MY_ERRPTR)
    return -1;

  can= (*found)->commit_trid < trn->trid;
  lf_hash_search_unpin(trn->pins);
  return can;
}

/* TODO: the stubs below are waiting for savepoints to be implemented */

void trnman_new_statement(TRN *trn __attribute__ ((unused)))
{
}

void trnman_rollback_statement(TRN *trn __attribute__ ((unused)))
{
}


/**
   @brief Allocates buffers and stores in them some info about transactions

   Does the allocation because the caller cannot know the size itself.
   Memory freeing is to be done by the caller (if the "str" member of the
   LEX_STRING is not NULL).
   The caller has the intention of doing checkpoints.

   @param[out]  str_act    pointer to where the allocated buffer,
                           and its size, will be put; buffer will be filled
                           with info about active transactions
   @param[out]  str_com    pointer to where the allocated buffer,
                           and its size, will be put; buffer will be filled
                           with info about committed transactions
   @param[out]  min_first_undo_lsn pointer to where the minimum
                           first_undo_lsn of all transactions will be put

   @return Operation status
     @retval 0      OK
     @retval 1      Error
*/

my_bool trnman_collect_transactions(LEX_STRING *str_act, LEX_STRING *str_com,
                                    LSN *min_rec_lsn, LSN *min_first_undo_lsn)
{
  my_bool error;
  TRN *trn;
  char *ptr;
  uint stored_transactions= 0;
  LSN minimum_rec_lsn= ULONGLONG_MAX, minimum_first_undo_lsn= ULONGLONG_MAX;
  DBUG_ENTER("trnman_collect_transactions");

  DBUG_ASSERT((NULL == str_act->str) && (NULL == str_com->str));

  /* validate the use of read_non_atomic() in general: */
  compile_time_assert((sizeof(LSN) == 8) && (sizeof(LSN_WITH_FLAGS) == 8));

  DBUG_PRINT("info", ("pthread_mutex_lock LOCK_trn_list"));
  pthread_mutex_lock(&LOCK_trn_list);
  str_act->length= 2 + /* number of active transactions */
    LSN_STORE_SIZE + /* minimum of their rec_lsn */
    (6 + /* long id */
     2 + /* short id */
     LSN_STORE_SIZE + /* undo_lsn */
#ifdef MARIA_VERSIONING /* not enabled yet */
     LSN_STORE_SIZE + /* undo_purge_lsn */
#endif
     LSN_STORE_SIZE /* first_undo_lsn */
     ) * trnman_active_transactions;
  str_com->length= 8 + /* number of committed transactions */
    (6 + /* long id */
#ifdef MARIA_VERSIONING /* not enabled yet */
     LSN_STORE_SIZE + /* undo_purge_lsn */
#endif
     LSN_STORE_SIZE /* first_undo_lsn */
     ) * trnman_committed_transactions;
  if ((NULL == (str_act->str= my_malloc(str_act->length, MYF(MY_WME)))) ||
      (NULL == (str_com->str= my_malloc(str_com->length, MYF(MY_WME)))))
    goto err;
  /* First, the active transactions */
  ptr= str_act->str + 2 + LSN_STORE_SIZE;
  for (trn= active_list_min.next; trn != &active_list_max; trn= trn->next)
  {
    /*
      trns with a short trid of 0 are not even initialized, we can ignore
      them. trns with undo_lsn==0 have done no writes, we can ignore them
      too. XID not needed now.
    */
    uint sid;
    LSN rec_lsn, undo_lsn, first_undo_lsn;
    if ((sid= trn->short_id) == 0)
    {
      /*
        Not even inited, has done nothing. Or it is the
        dummy_transaction_object, which does only non-transactional
        immediate-sync operations (CREATE/DROP/RENAME/REPAIR TABLE), and so
        can be forgotten for Checkpoint.
      */
      continue;
    }
#ifndef MARIA_CHECKPOINT
/*
  in the checkpoint patch (not yet ready) we will have a real implementation
  of lsn_read_non_atomic(); for now it's not needed
*/
#define lsn_read_non_atomic(A) (A)
#endif
      /* needed for low-water mark calculation */
    if (((rec_lsn= lsn_read_non_atomic(trn->rec_lsn)) > 0) &&
        (cmp_translog_addr(rec_lsn, minimum_rec_lsn) < 0))
      minimum_rec_lsn= rec_lsn;
    /*
      trn may have logged REDOs but not yet UNDO, that's why we read rec_lsn
      before deciding to ignore if undo_lsn==0.
    */
    if  ((undo_lsn= trn->undo_lsn) == 0) /* trn can be forgotten */
      continue;
    stored_transactions++;
    int6store(ptr, trn->trid);
    ptr+= 6;
    int2store(ptr, sid);
    ptr+= 2;
    lsn_store(ptr, undo_lsn); /* needed for rollback */
    ptr+= LSN_STORE_SIZE;
#ifdef MARIA_VERSIONING /* not enabled yet */
    /* to know where purging should start (last delete of this trn) */
    lsn_store(ptr, trn->undo_purge_lsn);
    ptr+= LSN_STORE_SIZE;
#endif
    /* needed for low-water mark calculation */
    if (((first_undo_lsn= lsn_read_non_atomic(trn->first_undo_lsn)) > 0) &&
        (cmp_translog_addr(first_undo_lsn, minimum_first_undo_lsn) < 0))
      minimum_first_undo_lsn= first_undo_lsn;
    lsn_store(ptr, first_undo_lsn);
    ptr+= LSN_STORE_SIZE;
    /**
       @todo RECOVERY: add a comment explaining why we can dirtily read some
       vars, inspired by the text of "assumption 8" in WL#3072
    */
  }
  str_act->length= ptr - str_act->str; /* as we maybe over-estimated */
  ptr= str_act->str;
  int2store(ptr, stored_transactions);
  ptr+= 2;
  /* this LSN influences how REDOs for any page can be ignored by Recovery */
  lsn_store(ptr, minimum_rec_lsn);
  /* one day there will also be a list of prepared transactions */
  /* do the same for committed ones */
  ptr= str_com->str;
  int8store(ptr, (ulonglong)trnman_committed_transactions);
  ptr+= 8;
  for (trn= committed_list_min.next; trn != &committed_list_max;
       trn= trn->next)
  {
    LSN first_undo_lsn;
    int6store(ptr, trn->trid);
    ptr+= 6;
#ifdef MARIA_VERSIONING /* not enabled yet */
    lsn_store(ptr, trn->undo_purge_lsn);
    ptr+= LSN_STORE_SIZE;
#endif
    first_undo_lsn= LSN_WITH_FLAGS_TO_LSN(trn->first_undo_lsn);
    if (cmp_translog_addr(first_undo_lsn, minimum_first_undo_lsn) < 0)
      minimum_first_undo_lsn= first_undo_lsn;
    lsn_store(ptr, first_undo_lsn);
    ptr+= LSN_STORE_SIZE;
  }
  /*
    TODO: if we see there exists no transaction (active and committed) we can
    tell the lock-free structures to do some freeing (my_free()).
  */
  error= 0;
  *min_rec_lsn= minimum_rec_lsn;
  *min_first_undo_lsn= minimum_first_undo_lsn;
  goto end;
err:
  error= 1;
end:
  DBUG_PRINT("info", ("pthread_mutex_unlock LOCK_trn_list"));
  pthread_mutex_unlock(&LOCK_trn_list);
  DBUG_RETURN(error);
}
