/* Copyright (c) 2005 PrimeBase Technologies GmbH
 *
 * PrimeBase XT
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 * 2008-01-24	Paul McCullagh
 *
 * Row lock functions.
 *
 * H&G2JCtL
 */
#ifndef __xt_lock_h__
#define __xt_lock_h__

#include "xt_defs.h"
#include "util_xt.h"
#include "locklist_xt.h"
#include "pthread_xt.h"

struct XTThread;
struct XTDatabase;
struct XTOpenTable;
struct XTXactData;
struct XTTable;

/* Possibilities are 2 = align 4 or 2 = align 8 */
#define XT_XS_LOCK_SHIFT		2
#define XT_XS_LOCK_ALIGN		(1 << XT_XS_LOCK_SHIFT)

/* This lock is fast for reads but slow for writes.
 * Use this lock in situations where you have 99% reads,
 * and then some potentially long writes.
 */
typedef struct XTRWMutex {
#ifdef DEBUG
	struct XTThread				*xs_lock_thread;
	u_int						xs_inited;
#endif
#ifdef XT_THREAD_LOCK_INFO
	XTThreadLockInfoRec			xs_lock_info;
	const char				    *xs_name;
#endif
	xt_mutex_type				xs_lock;
	xt_cond_type				xs_cond;
	volatile xtWord4			xs_state;
	volatile xtThreadID			xs_xlocker;
	union {
#if XT_XS_LOCK_ALIGN == 4
		volatile xtWord4		*xs_rlock_align;
#else
		volatile  xtWord8		*xs_rlock_align;
#endif
		volatile  xtWord1		*xs_rlock;
	}							x;
} XTRWMutexRec, *XTRWMutexPtr;

#ifdef XT_THREAD_LOCK_INFO
#define xt_rwmutex_init_with_autoname(a,b) xt_rwmutex_init(a,b,LOCKLIST_ARG_SUFFIX(b))
void xt_rwmutex_init(struct XTThread *self, XTRWMutexPtr xsl, const char *name);
#else
#define xt_rwmutex_init_with_autoname(a,b) xt_rwmutex_init(a,b)
void xt_rwmutex_init(struct XTThread *self, XTRWMutexPtr xsl);
#endif
void xt_rwmutex_free(struct XTThread *self, XTRWMutexPtr xsl);
xtBool xt_rwmutex_xlock(XTRWMutexPtr xsl, xtThreadID thd_id);
xtBool xt_rwmutex_slock(XTRWMutexPtr xsl, xtThreadID thd_id);
xtBool xt_rwmutex_unlock(XTRWMutexPtr xsl, xtThreadID thd_id);

#ifdef XT_WIN
#define XT_SPL_WIN32_ASM
#else
#if defined(__GNUC__) && (defined(__x86_64__) || defined(__i386__))
#define XT_SPL_GNUC_X86
#else
#define XT_SPL_DEFAULT
#endif
#endif

#ifdef XT_SOLARIS
/* Use Sun atomic operations library
 * http://docs.sun.com/app/docs/doc/816-5168/atomic-ops-3c?a=view
 */
#define XT_SPL_SOLARIS_LIB
#endif

#ifdef XT_SPL_SOLARIS_LIB
#include <atomic.h>
#endif

typedef struct XTSpinLock {
	volatile xtWord4			spl_lock;
#ifdef XT_SPL_DEFAULT
	xt_mutex_type				spl_mutex;
#endif
#ifdef DEBUG
	struct XTThread				*spl_locker;
#endif
#ifdef XT_THREAD_LOCK_INFO
	XTThreadLockInfoRec			spl_lock_info;
	const char				    *spl_name;
#endif
} XTSpinLockRec, *XTSpinLockPtr;

#ifdef XT_THREAD_LOCK_INFO
#define xt_spinlock_init_with_autoname(a,b) xt_spinlock_init(a,b,LOCKLIST_ARG_SUFFIX(b))
void	xt_spinlock_init(struct XTThread *self, XTSpinLockPtr sp, const char *name);
#else
#define xt_spinlock_init_with_autoname(a,b) xt_spinlock_init(a,b)
void	xt_spinlock_init(struct XTThread *self, XTSpinLockPtr sp);
#endif
void	xt_spinlock_free(struct XTThread *self, XTSpinLockPtr sp);
xtBool	xt_spinlock_spin(XTSpinLockPtr spl);
#ifdef DEBUG
void	xt_spinlock_set_thread(XTSpinLockPtr spl);
#endif

/*
 * This macro is to remind me where it was safe
 * to use a read lock!
 */
#define xt_lck_slock		xt_spinlock_lock

/* I call these operations flushed because the result
 * is written atomically.
 * But the operations themselves are not atomic!
 */
inline void xt_flushed_inc1(volatile xtWord1 *mptr)
{
#ifdef XT_SPL_WIN32_ASM
	__asm MOV  ECX, mptr
	__asm MOV  DL, BYTE PTR [ECX]
	__asm INC  DL
	__asm XCHG DL, BYTE PTR [ECX]
#elif defined(XT_SPL_GNUC_X86)
	xtWord1 val;

	asm volatile ("movb %1,%0" : "=r" (val) : "m" (*mptr) : "memory");
	val++;
	asm volatile ("xchgb %1,%0" : "=r" (val) : "m" (*mptr), "0" (val) : "memory");
#elif defined(XT_SPL_SOLARIS_LIB)
	atomic_inc_8(mptr);
#else
	*mptr++;
#endif
}

inline xtWord1 xt_flushed_dec1(volatile xtWord1 *mptr)
{
	xtWord1 val;

#ifdef XT_SPL_WIN32_ASM
	__asm MOV  ECX, mptr
	__asm MOV  DL, BYTE PTR [ECX]
	__asm DEC  DL
	__asm MOV  val, DL
	__asm XCHG DL, BYTE PTR [ECX]
#elif defined(XT_SPL_GNUC_X86)
	xtWord1 val2;

	asm volatile ("movb %1, %0" : "=r" (val) : "m" (*mptr) : "memory");
	val--;
	asm volatile ("xchgb %1,%0" : "=r" (val2) : "m" (*mptr), "0" (val) : "memory");
	/* Should work, but compiler makes a mistake?
	 * asm volatile ("xchgb %1, %0" : : "r" (val), "m" (*mptr) : "memory");
	 */
#elif defined(XT_SPL_SOLARIS_LIB)
	val = atomic_dec_8_nv(mptr);
#else
	val = --(*mptr);
#endif
	return val;
}

inline void xt_atomic_inc2(volatile xtWord2 *mptr)
{
#ifdef XT_SPL_WIN32_ASM
	__asm LOCK INC	WORD PTR mptr
#elif defined(XT_SPL_GNUC_X86)
        asm volatile ("lock; incw %0" : : "m" (*mptr) : "memory");
#elif defined(__GNUC__)
	__sync_fetch_and_add(mptr, 1);
#elif defined(XT_SPL_SOLARIS_LIB)
	atomic_inc_16_nv(mptr);
#else
	(*mptr)++;
#endif
}

inline void xt_atomic_dec2(volatile xtWord2 *mptr)
{
#ifdef XT_SPL_WIN32_ASM
	__asm LOCK DEC	WORD PTR mptr
#elif defined(XT_SPL_GNUC_X86)
	asm volatile ("lock; decw %0" : : "m" (*mptr) : "memory");
#elif defined(__GNUC__)
	__sync_fetch_and_sub(mptr, 1);
#elif defined(XT_SPL_SOLARIS_LIB)
	val1 = atomic_dec_16_nv(mptr);
#else
	val1 = --(*mptr);
#endif
}

/* Atomic test and set 2 byte word! */
inline xtWord2 xt_atomic_tas2(volatile xtWord2 *mptr, xtWord2 val)
{
#ifdef XT_SPL_WIN32_ASM
	__asm MOV  ECX, mptr
	__asm MOV  DX, val
	__asm XCHG DX, WORD PTR [ECX]
	__asm MOV  val, DX
#elif defined(XT_SPL_GNUC_X86)
	asm volatile ("xchgw %1,%0" : "=r" (val) : "m" (*mptr), "0" (val) : "memory");
#elif defined(XT_SPL_SOLARIS_LIB)
	val = atomic_swap_16(mptr, val);
#else
	/* Yikes! */
	xtWord2 nval = val;

	val = *mptr;
	*mptr = nval;
#endif
	return val;
}

inline void xt_atomic_set4(volatile xtWord4 *mptr, xtWord4 val)
{
#ifdef XT_SPL_WIN32_ASM
	__asm MOV  ECX, mptr
	__asm MOV  EDX, val
	__asm XCHG EDX, DWORD PTR [ECX]
	//__asm MOV  DWORD PTR [ECX], EDX
#elif defined(XT_SPL_GNUC_X86)
	asm volatile ("xchgl %1,%0" : "=r" (val) : "m" (*mptr), "0" (val) : "memory");
	//asm volatile ("movl %0,%1" : "=r" (val) : "m" (*mptr) : "memory");
#elif defined(XT_SPL_SOLARIS_LIB)
	atomic_swap_32(mptr, val);
#else
	*mptr = val;
#endif
}

inline xtWord4 xt_atomic_get4(volatile xtWord4 *mptr)
{
	xtWord4 val;

#ifdef XT_SPL_WIN32_ASM
	__asm MOV ECX, mptr
	__asm MOV EDX, DWORD PTR [ECX]
	__asm MOV val, EDX
#elif defined(XT_SPL_GNUC_X86)
	asm volatile ("movl %1,%0" : "=r" (val) : "m" (*mptr) : "memory");
#else
	val = *mptr;
#endif
	return val;
}

/* Code for test and set is derived from code by Larry Zhou and
 * Google: http://code.google.com/p/google-perftools
 */
inline xtWord4 xt_spinlock_set(XTSpinLockPtr spl)
{
	xtWord4				prv;
	volatile xtWord4	*lck;
				
	lck = &spl->spl_lock;
#ifdef XT_SPL_WIN32_ASM
	__asm MOV  ECX, lck
	__asm MOV  EDX, 1
	__asm XCHG EDX, DWORD PTR [ECX]
	__asm MOV  prv, EDX
#elif defined(XT_SPL_GNUC_X86)
	prv = 1;
	asm volatile ("xchgl %1,%0" : "=r" (prv) : "m" (*lck), "0" (prv) : "memory");
#elif defined(XT_SPL_SOLARIS_LIB)
	prv = atomic_swap_32(lck, 1);
#else
	/* The default implementation just uses a mutex, and
	 * does not spin! */
	xt_lock_mutex_ns(&spl->spl_mutex);
	/* We have the lock */
	*lck = 1;
	prv = 0;
#endif
#ifdef DEBUG
	if (!prv)
		xt_spinlock_set_thread(spl);
#endif
	return prv;
}

inline xtWord4 xt_spinlock_reset(XTSpinLockPtr spl)
{
	xtWord4				prv;
	volatile xtWord4	*lck;
				
#ifdef DEBUG
	spl->spl_locker = NULL;
#endif
	lck = &spl->spl_lock;
#ifdef XT_SPL_WIN32_ASM
	__asm MOV  ECX, lck
	__asm MOV  EDX, 0
	__asm XCHG EDX, DWORD PTR [ECX]
	__asm MOV  prv, EDX
#elif defined(XT_SPL_GNUC_X86)
	prv = 0;
	asm volatile ("xchgl %1,%0" : "=r" (prv) : "m" (*lck), "0" (prv) : "memory");
#elif defined(XT_SPL_SOLARIS_LIB)
	prv = atomic_swap_32(lck, 0);
#else
	*lck = 0;
	xt_unlock_mutex_ns(&spl->spl_mutex);
	prv = 1;
#endif
	return prv;
}

/*
 * Return FALSE, and register an error on failure.
 */
inline xtBool xt_spinlock_lock(XTSpinLockPtr spl)
{
	if (!xt_spinlock_set(spl)) {
#ifdef XT_THREAD_LOCK_INFO
		xt_thread_lock_info_add_owner(&spl->spl_lock_info);
#endif
		return OK;
	}
#ifdef XT_THREAD_LOCK_INFO
	xtBool spin_result = xt_spinlock_spin(spl);
	if (spin_result)
		xt_thread_lock_info_add_owner(&spl->spl_lock_info);
	return spin_result;
#else
	return xt_spinlock_spin(spl);
#endif
}

inline void xt_spinlock_unlock(XTSpinLockPtr spl)
{
	xt_spinlock_reset(spl);
#ifdef XT_THREAD_LOCK_INFO
	xt_thread_lock_info_release_owner(&spl->spl_lock_info);
#endif
}

void xt_unit_test_read_write_locks(struct XTThread *self);
void xt_unit_test_mutex_locks(struct XTThread *self);
void xt_unit_test_create_threads(struct XTThread *self);

#define XT_FAST_LOCK_MAX_WAIT	100

typedef struct XTFastLock {
	XTSpinLockRec				fal_spinlock;
	struct XTThread				*fal_locker;

	XTSpinLockRec				fal_wait_lock;
	u_int						fal_wait_count;
	u_int						fal_wait_wakeup;
	u_int						fal_wait_alloc;
	struct XTThread				*fal_wait_list[XT_FAST_LOCK_MAX_WAIT];
#ifdef XT_THREAD_LOCK_INFO
	XTThreadLockInfoRec			fal_lock_info;
	const char				    *fal_name;
#endif
} XTFastLockRec, *XTFastLockPtr;

#ifdef XT_THREAD_LOCK_INFO
#define xt_fastlock_init_with_autoname(a,b) xt_fastlock_init(a,b,LOCKLIST_ARG_SUFFIX(b))
void	xt_fastlock_init(struct XTThread *self, XTFastLockPtr spl, const char *name);
#else
#define xt_fastlock_init_with_autoname(a,b) xt_fastlock_init(a,b)
void	xt_fastlock_init(struct XTThread *self, XTFastLockPtr spl);
#endif
void	xt_fastlock_free(struct XTThread *self, XTFastLockPtr spl);
void	xt_fastlock_wakeup(XTFastLockPtr spl);
xtBool	xt_fastlock_spin(XTFastLockPtr spl, struct XTThread *thread);

inline xtBool xt_fastlock_lock(XTFastLockPtr fal, struct XTThread *thread)
{
	if (!xt_spinlock_set(&fal->fal_spinlock)) {
		fal->fal_locker = thread;
#ifdef XT_THREAD_LOCK_INFO
	xt_thread_lock_info_add_owner(&fal->fal_lock_info);
#endif
		return OK;
	}
#ifdef XT_THREAD_LOCK_INFO
	xtBool spin_result = xt_fastlock_spin(fal, thread);
	if (spin_result)
		xt_thread_lock_info_add_owner(&fal->fal_lock_info);
	return spin_result;
#else
	return xt_fastlock_spin(fal, thread);
#endif
}

inline void xt_fastlock_unlock(XTFastLockPtr fal, struct XTThread *thread __attribute__((unused)))
{
	if (fal->fal_wait_count)
		xt_fastlock_wakeup(fal);
	else {
		fal->fal_locker = NULL;
		xt_spinlock_reset(&fal->fal_spinlock);
	}
#ifdef XT_THREAD_LOCK_INFO
	xt_thread_lock_info_release_owner(&fal->fal_lock_info);
#endif
}

typedef struct XTSpinRWLock {
	XTSpinLockRec				srw_lock;
	volatile xtThreadID			srw_xlocker;
	XTSpinLockRec				srw_state_lock;
	volatile u_int				srw_state;
	union {
#if XT_XS_LOCK_ALIGN == 4
		volatile xtWord4		*srw_rlock_align;
#else
		volatile  xtWord8		*srw_rlock_align;
#endif
		volatile  xtWord1		*srw_rlock;
	} x;

#ifdef XT_THREAD_LOCK_INFO
	XTThreadLockInfoRec			srw_lock_info;
	const char				    *srw_name;
#endif

} XTSpinRWLockRec, *XTSpinRWLockPtr;

#ifdef XT_THREAD_LOCK_INFO
#define xt_spinrwlock_init_with_autoname(a,b) xt_spinrwlock_init(a,b,LOCKLIST_ARG_SUFFIX(b))
void xt_spinrwlock_init(struct XTThread *self, XTSpinRWLockPtr xsl, const char *name);
#else
#define xt_spinrwlock_init_with_autoname(a,b) xt_spinrwlock_init(a,b)
void xt_spinrwlock_init(struct XTThread *self, XTSpinRWLockPtr xsl);
#endif
void xt_spinrwlock_free(struct XTThread *self, XTSpinRWLockPtr xsl);
xtBool xt_spinrwlock_xlock(XTSpinRWLockPtr xsl, xtThreadID thd_id);
xtBool xt_spinrwlock_slock(XTSpinRWLockPtr xsl, xtThreadID thd_id);
xtBool xt_spinrwlock_unlock(XTSpinRWLockPtr xsl, xtThreadID thd_id);

typedef struct XTFastRWLock {
	XTFastLockRec				frw_lock;
	struct XTThread				*frw_xlocker;
	XTSpinLockRec				frw_state_lock;
	volatile u_int				frw_state;
	u_int						frw_read_waiters;
	union {
#if XT_XS_LOCK_ALIGN == 4
		volatile xtWord4		*frw_rlock_align;
#else
		volatile  xtWord8		*frw_rlock_align;
#endif
		volatile  xtWord1		*frw_rlock;
	} x;

#ifdef XT_THREAD_LOCK_INFO
	XTThreadLockInfoRec			frw_lock_info;
	const char				    *frw_name;
#endif

} XTFastRWLockRec, *XTFastRWLockPtr;

#ifdef XT_THREAD_LOCK_INFO
#define xt_fastrwlock_init_with_autoname(a,b) xt_fastrwlock_init(a,b,LOCKLIST_ARG_SUFFIX(b))
void xt_fastrwlock_init(struct XTThread *self, XTFastRWLockPtr frw, const char *name);
#else
#define xt_fastrwlock_init_with_autoname(a,b) xt_fastrwlock_init(a,b)
void xt_fastrwlock_init(struct XTThread *self, XTFastRWLockPtr frw);
#endif

void xt_fastrwlock_free(struct XTThread *self, XTFastRWLockPtr frw);
xtBool xt_fastrwlock_xlock(XTFastRWLockPtr frw, struct XTThread *thread);
xtBool xt_fastrwlock_slock(XTFastRWLockPtr frw, struct XTThread *thread);
xtBool xt_fastrwlock_unlock(XTFastRWLockPtr frw, struct XTThread *thread);

typedef struct XTAtomicRWLock {
	volatile xtWord2			arw_reader_count;
	volatile xtWord2			arw_xlock_set;

#ifdef XT_THREAD_LOCK_INFO
	XTThreadLockInfoRec			arw_lock_info;
	const char				    *arw_name;
#endif
#ifdef DEBUG
	xtThreadID					arw_locker;
#endif
} XTAtomicRWLockRec, *XTAtomicRWLockPtr;

#ifdef XT_THREAD_LOCK_INFO
#define xt_atomicrwlock_init_with_autoname(a,b) xt_atomicrwlock_init(a,b,LOCKLIST_ARG_SUFFIX(b))
void xt_atomicrwlock_init(struct XTThread *self, XTAtomicRWLockPtr xsl, const char *name);
#else
#define xt_atomicrwlock_init_with_autoname(a,b) xt_atomicrwlock_init(a,b)
void xt_atomicrwlock_init(struct XTThread *self, XTAtomicRWLockPtr xsl);
#endif
void xt_atomicrwlock_free(struct XTThread *self, XTAtomicRWLockPtr xsl);
xtBool xt_atomicrwlock_xlock(XTAtomicRWLockPtr xsl, xtThreadID thr_id);
xtBool xt_atomicrwlock_slock(XTAtomicRWLockPtr xsl);
xtBool xt_atomicrwlock_unlock(XTAtomicRWLockPtr xsl, xtBool xlocked);

/*
 * -----------------------------------------------------------------------
 * ROW LOCKS
 */

/*
 * [(9)]
 *
 * These are perminent row locks. They are set on rows for 2 reasons:
 *
 * 1. To lock a row that is being updated. The row is locked
 *    when it is read, until the point that it is updated. If the row
 *    is not updated, the lock is removed.
 *    This prevents an update coming between which will cause an error
 *    on the first thread.
 *
 * 2. The locks are used to implement SELECT FOR UPDATE.
 */

/*
 * A lock that is set in order to perform an update is a temporary lock.
 * This lock will be removed once the update of the record is done.
 * The objective is to prevent some other thread from changine the
 * record between the time the record is read and updated. This is to
 * prevent unncessary "Record was updated" errors.
 *
 * A permanent lock is set by a SELECT FOR UPDATE. These locks are
 * held until the end of the transaction.
 *
 * However, a SELECT FOR UPDATE will pop its lock stack before
 * waiting for a transaction that has updated a record.
 * This is to prevent the deadlock that can occur because a
 * SELECT FOR UPDATE locks groups of records (I mean in general the
 * locks used are group locks).
 *
 * This means a SELECT FOR UPDATE can get ahead of an UPDATE as far as
 * locking is concerned. Example:
 *
 * Record 1,2 and 3 are in group A.
 *
 * T1: UPDATES record 2.
 * T2: SELECT FOR UPDATE record 1, which locks group A.
 * T2: SELECT FOR UPDATE record 2, which must wait for T1.
 * T1: UPDATES record 3, which musts wait because of group lock A.
 *
 * To avoid deadlock, T2 releases its group lock A before waiting for
 * record 2. It then regains the lock after waiting for record 2.
 *
 * (NOTE: Locks are no longer released. Please check this comment:
 * {RELEASING-LOCKS} in lock_xt.cc. )
 *
 * However, release group A lock mean first releasing all locks gained
 * after group a lock.
 *
 * For example: a thread locks groups: A, B and C. To release group B
 * lock the thread must release C as well. Afterwards, it must gain
 * B and C again, in that order. This is to ensure that the lock
 * order is NOT changed!
 *
 */
#define XT_LOCK_ERR					-1
#define XT_NO_LOCK					0
#define XT_TEMP_LOCK				1								/* A temporary lock */
#define XT_PERM_LOCK				2								/* A permanent lock */

typedef struct XTRowLockList : public XTBasicList {
	void	xt_remove_all_locks(struct XTDatabase *db, struct XTThread *thread);
} XTRowLockListRec, *XTRowLockListPtr;

#define XT_USE_LIST_BASED_ROW_LOCKS

#ifdef XT_USE_LIST_BASED_ROW_LOCKS
/*
 * This method stores each lock, and avoids conflicts.
 * But it is a bit more expensive in time.
 */

#ifdef DEBUG
#define XT_TEMP_LOCK_BYTES				10
#define XT_ROW_LOCK_GROUP_COUNT			5
#else
#define XT_TEMP_LOCK_BYTES				0xFFFF
#define XT_ROW_LOCK_GROUP_COUNT			23
#endif

typedef struct XTLockWait {
	/* Information about the lock to be aquired: */
	struct XTThread			*lw_thread;
	struct XTOpenTable		*lw_ot;
	xtRowID					lw_row_id;

	/* This is the lock currently held, and the transaction ID: */
	int						lw_curr_lock;
	xtXactID				lw_xn_id;

	/* This is information about the updating transaction: */
	xtBool					lw_row_updated;
	xtXactID				lw_updating_xn_id;

	/* Pointers for the lock list: */
	struct XTLockWait		*lw_next;
	struct XTLockWait		*lw_prev;
} XTLockWaitRec, *XTLockWaitPtr;

typedef struct XTLockItem {
	xtRowID					li_row_id;				/* The row list is sorted in this value. */
	xtWord2					li_count;				/* The number of consecutive rows locked. FFFF means a temporary lock. */
	xtWord2					li_thread_id;			/* The thread that holds this lock. */
} XTLockItemRec, *XTLockItemPtr;

typedef struct XTLockGroup {
	XTSpinLockRec			lg_lock;				/* A lock for the list. */
	XTLockWaitPtr			lg_wait_queue;			/* A queue of threads waiting for a lock in this group. */
	XTLockWaitPtr			lg_wait_queue_end;		/* The end of the thread queue. */
	size_t						lg_list_size;			/* The size of the list. */
	size_t						lg_list_in_use;			/* Number of slots on the list in use. */
	XTLockItemPtr			lg_list;				/* List of locks. */
} XTLockGroupRec, *XTLockGroupPtr;

struct XTLockWait;

typedef struct XTRowLocks {
	XTLockGroupRec			rl_groups[XT_ROW_LOCK_GROUP_COUNT];

	void	xt_cancel_temp_lock(XTLockWaitPtr lw);
	xtBool	xt_set_temp_lock(struct XTOpenTable *ot, XTLockWaitPtr lw, XTRowLockListPtr lock_list);
	void	xt_remove_temp_lock(struct XTOpenTable *ot, xtBool updated);
	xtBool	xt_make_lock_permanent(struct XTOpenTable *ot, XTRowLockListPtr lock_list);

	xtBool	rl_lock_row(XTLockGroupPtr group, XTLockWaitPtr lw, XTRowLockListPtr lock_list, int *result);
	void	rl_grant_locks(XTLockGroupPtr group, struct XTThread *thread);
#ifdef DEBUG_LOCK_QUEUE
	void	rl_check(XTLockWaitPtr lw);
#endif
} XTRowLocksRec, *XTRowLocksPtr;

#define XT_USE_TABLE_REF

typedef struct XTPermRowLock {
#ifdef XT_USE_TABLE_REF
	struct XTTable			*pr_table;
#else
	xtTableID				pr_tab_id;
#endif
	xtWord1					pr_group[XT_ROW_LOCK_GROUP_COUNT];
} XTPermRowLockRec, *XTPermRowLockPtr;

#else // XT_ROW_LOCK_GROUP_COUNT

/* Hash based row locking. This method allows conflics, even
 * when there is none.
 */
typedef struct XTRowLocks {
	xtWord1					tab_lock_perm[XT_ROW_LOCK_COUNT];		/* Byte set to 1 for permanent locks. */
	struct XTXactData		*tab_row_locks[XT_ROW_LOCK_COUNT];		/* The transactions that have locked the specific rows. */

	int		xt_set_temp_lock(struct XTOpenTable *ot, xtRowID row, xtXactID *xn_id, XTRowLockListPtr lock_list);
	void	xt_remove_temp_lock(struct XTOpenTable *ot);
	xtBool	xt_make_lock_permanent(struct XTOpenTable *ot, XTRowLockListPtr lock_list);
	int		xt_is_locked(struct XTOpenTable *ot, xtRowID row, xtXactID *xn_id);
} XTRowLocksRec, *XTRowLocksPtr;

typedef struct XTPermRowLock {
	xtTableID				pr_tab_id;
	xtWord4					pr_group;
} XTPermRowLockRec, *XTPermRowLockPtr;

#endif // XT_ROW_LOCK_GROUP_COUNT

xtBool			xt_init_row_locks(XTRowLocksPtr rl);
void			xt_exit_row_locks(XTRowLocksPtr rl);

xtBool			xt_init_row_lock_list(XTRowLockListPtr rl);
void			xt_exit_row_lock_list(XTRowLockListPtr rl);

#define XT_NO_LOCK				0
#define XT_WANT_LOCK			1
#define XT_HAVE_LOCK			2
#define XT_WAITING				3

#endif
