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

#include <stdlib.h>
#include <string.h>
#include <portability/toku_pthread.h>

#include "locktree.h"
#include "lock_request.h"

#include <util/status.h>

namespace toku {

void locktree::manager::create(lt_create_cb create_cb, lt_destroy_cb destroy_cb, lt_escalate_cb escalate_cb, void *escalate_extra) {
    m_max_lock_memory = DEFAULT_MAX_LOCK_MEMORY;
    m_current_lock_memory = 0;
    escalator_init();
    m_lock_wait_time_ms = DEFAULT_LOCK_WAIT_TIME;
    m_mem_tracker.set_manager(this);

    m_locktree_map.create();
    m_lt_create_callback = create_cb;
    m_lt_destroy_callback = destroy_cb;
    m_lt_escalate_callback = escalate_cb;
    m_lt_escalate_callback_extra = escalate_extra;

    ZERO_STRUCT(m_mutex);
    toku_mutex_init(&m_mutex, nullptr);

    ZERO_STRUCT(status);
    ZERO_STRUCT(m_lt_counters);
}

void locktree::manager::destroy(void) {
    escalator_destroy();
    invariant(m_current_lock_memory == 0);
    invariant(m_locktree_map.size() == 0);
    m_locktree_map.destroy();
    toku_mutex_destroy(&m_mutex);
}

void locktree::manager::mutex_lock(void) {
    toku_mutex_lock(&m_mutex);
}

void locktree::manager::mutex_unlock(void) {
    toku_mutex_unlock(&m_mutex);
}

size_t locktree::manager::get_max_lock_memory(void) {
    return m_max_lock_memory;
}

int locktree::manager::set_max_lock_memory(size_t max_lock_memory) {
    int r = 0;
    mutex_lock();
    if (max_lock_memory < m_current_lock_memory) {
        r = EDOM;
    } else {
        m_max_lock_memory = max_lock_memory;
    }
    mutex_unlock();
    return r;
}

uint64_t locktree::manager::get_lock_wait_time(void) {
    return m_lock_wait_time_ms;
}

void locktree::manager::set_lock_wait_time(uint64_t lock_wait_time_ms) {
    m_lock_wait_time_ms = lock_wait_time_ms;
}

int locktree::manager::find_by_dict_id(locktree *const &lt, const DICTIONARY_ID &dict_id) {
    if (lt->m_dict_id.dictid < dict_id.dictid) {
        return -1;
    } else if (lt->m_dict_id.dictid == dict_id.dictid) {
        return 0;
    } else {
        return 1;
    }
}

locktree *locktree::manager::locktree_map_find(const DICTIONARY_ID &dict_id) {
    locktree *lt;
    int r = m_locktree_map.find_zero<DICTIONARY_ID, find_by_dict_id>(dict_id, &lt, nullptr);
    return r == 0 ? lt : nullptr;
}

void locktree::manager::locktree_map_put(locktree *lt) {
    int r = m_locktree_map.insert<DICTIONARY_ID, find_by_dict_id>(lt, lt->m_dict_id, nullptr);
    invariant_zero(r);
}

void locktree::manager::locktree_map_remove(locktree *lt) {
    uint32_t idx;
    locktree *found_lt;
    int r = m_locktree_map.find_zero<DICTIONARY_ID, find_by_dict_id>(
            lt->m_dict_id, &found_lt, &idx);
    invariant_zero(r);
    invariant(found_lt == lt);
    r = m_locktree_map.delete_at(idx);
    invariant_zero(r);
}

locktree *locktree::manager::get_lt(DICTIONARY_ID dict_id, DESCRIPTOR desc,
        ft_compare_func cmp, void *on_create_extra) {

    // hold the mutex around searching and maybe
    // inserting into the locktree map
    mutex_lock();

    locktree *lt = locktree_map_find(dict_id);
    if (lt == nullptr) {
        XCALLOC(lt);
        lt->create(&m_mem_tracker, dict_id, desc, cmp);
        invariant(lt->m_reference_count == 1);

        // new locktree created - call the on_create callback
        // and put it in the locktree map
        if (m_lt_create_callback) {
            int r = m_lt_create_callback(lt, on_create_extra);
            if (r != 0) {
                (void) toku_sync_sub_and_fetch(&lt->m_reference_count, 1);
                lt->destroy();
                toku_free(lt);
                lt = nullptr;
            }
        }
        if (lt) {
            locktree_map_put(lt);
        }
    } else {
        reference_lt(lt);
    }

    mutex_unlock();

    return lt;
}

void locktree::manager::reference_lt(locktree *lt) {
    // increment using a sync fetch and add.
    // the caller guarantees that the lt won't be
    // destroyed while we increment the count here.
    //
    // the caller can do this by already having an lt
    // reference or by holding the manager mutex.
    //
    // if the manager's mutex is held, it is ok for the
    // reference count to transition from 0 to 1 (no race),
    // since we're serialized with other opens and closes.
    toku_sync_fetch_and_add(&lt->m_reference_count, 1);
}

static void add_lt_counters(locktree::lt_counters *x, locktree::lt_counters *y) {
    x->wait_count += y->wait_count;
    x->wait_time += y->wait_time;
    x->long_wait_count += y->long_wait_count;
    x->long_wait_time += y->long_wait_time;
    x->timeout_count += y->timeout_count;
}

void locktree::manager::release_lt(locktree *lt) {
    bool do_destroy = false;
    DICTIONARY_ID dict_id = lt->m_dict_id;

    // Release a reference on the locktree. If the count transitions to zero,
    // then we *may* need to do the cleanup.
    //
    // Grab the manager's mutex and look for a locktree with this locktree's
    // dictionary id. Since dictionary id's never get reused, any locktree 
    // found must be the one we just released a reference on.
    //
    // At least two things could have happened since we got the mutex:
    // - Another thread gets a locktree with the same dict_id, increments
    // the reference count. In this case, we shouldn't destroy it.
    // - Another thread gets a locktree with the same dict_id and then
    // releases it quickly, transitioning the reference count from zero to
    // one and back to zero. In this case, only one of us should destroy it.
    // It doesn't matter which. We originally missed this case, see #5776.
    //
    // After 5776, the high level rule for release is described below.
    //
    // If a thread releases a locktree and notices the reference count transition
    // to zero, then that thread must immediately:
    // - assume the locktree object is invalid
    // - grab the manager's mutex
    // - search the locktree map for a locktree with the same dict_id and remove
    // it, if it exists. the destroy may be deferred.
    // - release the manager's mutex
    //
    // This way, if many threads transition the same locktree's reference count
    // from 1 to zero and wait behind the manager's mutex, only one of them will
    // do the actual destroy and the others will happily do nothing.
    uint32_t refs = toku_sync_sub_and_fetch(&lt->m_reference_count, 1);
    if (refs == 0) {
        mutex_lock();
        locktree *find_lt = locktree_map_find(dict_id);
        if (find_lt != nullptr) {
            // A locktree is still in the map with that dict_id, so it must be
            // equal to lt. This is true because dictionary ids are never reused.
            // If the reference count is zero, it's our responsibility to remove
            // it and do the destroy. Otherwise, someone still wants it.
            invariant(find_lt == lt);
            if (lt->m_reference_count == 0) {
                locktree_map_remove(lt);
                do_destroy = true;
            }
        }
        add_lt_counters(&m_lt_counters, &lt->m_lock_request_info.counters);
        mutex_unlock();
    }

    // if necessary, do the destroy without holding the mutex
    if (do_destroy) {
        if (m_lt_destroy_callback) {
            m_lt_destroy_callback(lt);
        }
        lt->destroy();
        toku_free(lt);
    }
}

// test-only version of lock escalation
void locktree::manager::run_escalation_for_test(void) {
    run_escalation();
}

void locktree::manager::run_escalation(void) {
    uint64_t t0 = toku_current_time_microsec();
    if (1) {
        // run escalation on the background thread
        int r;
        toku_mutex_lock(&m_escalator_mutex);
        toku_cond_broadcast(&m_escalator_work);
        struct timeval tv;
        r = gettimeofday(&tv, 0);
        assert_zero(r);
        uint64_t t = tv.tv_sec * 1000000 + tv.tv_usec;
        t += 100000; // 100 milliseconds
        toku_timespec_t wakeup_time;
        wakeup_time.tv_sec = t / 1000000;
        wakeup_time.tv_nsec = (t % 1000000) * 1000;
        r = toku_cond_timedwait(&m_escalator_done, &m_escalator_mutex, &wakeup_time);
        toku_mutex_unlock(&m_escalator_mutex);
    } else {
        // run escalation on this thread
        mutex_lock();
        escalate_all_locktrees();
        mutex_unlock();
    }
    uint64_t t1 = toku_current_time_microsec();
    add_escalator_wait_time(t1 - t0);
}

void locktree::manager::memory_tracker::set_manager(manager *mgr) {
    m_mgr = mgr;
}

int locktree::manager::memory_tracker::check_current_lock_constraints(void) {
    int r = 0;
    // check if we're out of locks without the mutex first. then, grab the
    // mutex and check again. if we're still out of locks, run escalation.
    // return an error if we're still out of locks after escalation.
    if (out_of_locks()) {
        m_mgr->run_escalation();
        if (out_of_locks()) {
            r = TOKUDB_OUT_OF_LOCKS;
        }
    }
    return r;
}

void locktree::manager::memory_tracker::note_mem_used(uint64_t mem_used) {
    (void) toku_sync_fetch_and_add(&m_mgr->m_current_lock_memory, mem_used);
}

void locktree::manager::memory_tracker::note_mem_released(uint64_t mem_released) {
    uint64_t old_mem_used = toku_sync_fetch_and_sub(&m_mgr->m_current_lock_memory, mem_released);
    invariant(old_mem_used >= mem_released);
}

bool locktree::manager::memory_tracker::out_of_locks(void) const {
    return m_mgr->m_current_lock_memory >= m_mgr->m_max_lock_memory;
}

int locktree::manager::iterate_pending_lock_requests(
        lock_request_iterate_callback callback, void *extra) {
    mutex_lock();
    int r = 0;
    size_t num_locktrees = m_locktree_map.size();
    for (size_t i = 0; i < num_locktrees && r == 0; i++) {
        locktree *lt;
        r = m_locktree_map.fetch(i, &lt);
        invariant_zero(r);

        struct lt_lock_request_info *info = &lt->m_lock_request_info;
        toku_mutex_lock(&info->mutex);

        size_t num_requests = info->pending_lock_requests.size();
        for (size_t k = 0; k < num_requests && r == 0; k++) {
            lock_request *req;
            r = info->pending_lock_requests.fetch(k, &req);
            invariant_zero(r);
            r = callback(lt->m_dict_id, req->get_txnid(),
                         req->get_left_key(), req->get_right_key(),
                         req->get_conflicting_txnid(), req->get_start_time(), extra);
        }

        toku_mutex_unlock(&info->mutex);
    }
    mutex_unlock();
    return r;
}

static void *escalator_thread(void *arg) {
    locktree::manager *mgr = reinterpret_cast<locktree::manager*>(arg);
    mgr->escalator_work();
    return arg;
}

void locktree::manager::escalator_init(void) {
    ZERO_STRUCT(m_escalator_mutex);
    toku_mutex_init(&m_escalator_mutex, nullptr);
    toku_cond_init(&m_escalator_work, nullptr);
    toku_cond_init(&m_escalator_done, nullptr);
    m_escalator_killed = false;
    m_escalation_count = 0;
    m_escalation_time = 0;
    m_wait_escalation_count = 0;
    m_wait_escalation_time = 0;
    m_long_wait_escalation_count = 0;
    m_long_wait_escalation_time = 0;
    m_escalation_latest_result = 0;
    int r = toku_pthread_create(&m_escalator_id, nullptr, escalator_thread, this);
    assert_zero(r);
}

void locktree::manager::escalator_destroy(void) {
    toku_mutex_lock(&m_escalator_mutex);
    m_escalator_killed = true;
    toku_cond_broadcast(&m_escalator_work);
    toku_mutex_unlock(&m_escalator_mutex);
    void *ret;
    int r = toku_pthread_join(m_escalator_id, &ret);
    assert_zero(r);
    toku_mutex_destroy(&m_escalator_mutex);
    toku_cond_destroy(&m_escalator_work);
    toku_cond_destroy(&m_escalator_done);
}

void locktree::manager::escalate_all_locktrees(void) {
    // there are too many row locks in the system and we need to tidy up.
    //
    // a simple implementation of escalation does not attempt
    // to reduce the memory foot print of each txn's range buffer.
    // doing so would require some layering hackery (or a callback)
    // and more complicated locking. for now, just escalate each
    // locktree individually, in-place.
    tokutime_t t0 = toku_time_now();
    size_t num_locktrees = m_locktree_map.size();
    for (size_t i = 0; i < num_locktrees; i++) {
        locktree *lt;
        int r = m_locktree_map.fetch(i, &lt);
        invariant_zero(r);
        lt->escalate(m_lt_escalate_callback, m_lt_escalate_callback_extra);
    }
    tokutime_t t1 = toku_time_now();

    toku_mutex_lock(&m_escalator_mutex);
    m_escalation_count++;
    m_escalation_time += (t1 - t0);
    m_escalation_latest_result = m_current_lock_memory;
    toku_mutex_unlock(&m_escalator_mutex);
}

void locktree::manager::add_escalator_wait_time(uint64_t t) {
    toku_mutex_lock(&m_escalator_mutex);
    m_wait_escalation_count += 1;
    m_wait_escalation_time += t;
    if (t >= 1000000) {
        m_long_wait_escalation_count += 1;
        m_long_wait_escalation_time += t;
    }
    toku_mutex_unlock(&m_escalator_mutex);
}

void locktree::manager::escalator_work(void) {
    toku_mutex_lock(&m_escalator_mutex);
    while (!m_escalator_killed) {
        toku_cond_wait(&m_escalator_work, &m_escalator_mutex);
        if (!m_escalator_killed) {
            toku_mutex_unlock(&m_escalator_mutex);
            mutex_lock();
            escalate_all_locktrees();
            mutex_unlock();
            toku_mutex_lock(&m_escalator_mutex);
            toku_cond_broadcast(&m_escalator_done);
        }
    }
    toku_mutex_unlock(&m_escalator_mutex);
}

#define STATUS_INIT(k,c,t,l,inc) TOKUDB_STATUS_INIT(status, k, c, t, "locktree: " l, inc)

void locktree::manager::status_init(void) {
    STATUS_INIT(LTM_SIZE_CURRENT,             LOCKTREE_MEMORY_SIZE, UINT64,   "memory size", TOKU_ENGINE_STATUS|TOKU_GLOBAL_STATUS);
    STATUS_INIT(LTM_SIZE_LIMIT,               LOCKTREE_MEMORY_SIZE_LIMIT, UINT64,   "memory size limit", TOKU_ENGINE_STATUS|TOKU_GLOBAL_STATUS);
    STATUS_INIT(LTM_ESCALATION_COUNT,         LOCKTREE_ESCALATION_NUM, UINT64, "number of times lock escalation ran", TOKU_ENGINE_STATUS|TOKU_GLOBAL_STATUS);
    STATUS_INIT(LTM_ESCALATION_TIME,          LOCKTREE_ESCALATION_SECONDS, TOKUTIME, "time spent running escalation (seconds)", TOKU_ENGINE_STATUS|TOKU_GLOBAL_STATUS);
    STATUS_INIT(LTM_ESCALATION_LATEST_RESULT, LOCKTREE_LATEST_POST_ESCALATION_MEMORY_SIZE, UINT64,   "latest post-escalation memory size", TOKU_ENGINE_STATUS|TOKU_GLOBAL_STATUS);
    STATUS_INIT(LTM_NUM_LOCKTREES,            LOCKTREE_OPEN_CURRENT, UINT64,   "number of locktrees open now", TOKU_ENGINE_STATUS|TOKU_GLOBAL_STATUS);
    STATUS_INIT(LTM_LOCK_REQUESTS_PENDING,    LOCKTREE_PENDING_LOCK_REQUESTS, UINT64,   "number of pending lock requests", TOKU_ENGINE_STATUS|TOKU_GLOBAL_STATUS);
    STATUS_INIT(LTM_STO_NUM_ELIGIBLE,         LOCKTREE_STO_ELIGIBLE_NUM, UINT64,   "number of locktrees eligible for the STO", TOKU_ENGINE_STATUS|TOKU_GLOBAL_STATUS);
    STATUS_INIT(LTM_STO_END_EARLY_COUNT,      LOCKTREE_STO_ENDED_NUM, UINT64,   "number of times a locktree ended the STO early", TOKU_ENGINE_STATUS|TOKU_GLOBAL_STATUS);
    STATUS_INIT(LTM_STO_END_EARLY_TIME,       LOCKTREE_STO_ENDED_SECONDS, TOKUTIME, "time spent ending the STO early (seconds)", TOKU_ENGINE_STATUS|TOKU_GLOBAL_STATUS);

    STATUS_INIT(LTM_WAIT_COUNT,               LOCKTREE_WAIT_COUNT, UINT64, "number of wait locks", TOKU_ENGINE_STATUS|TOKU_GLOBAL_STATUS);
    STATUS_INIT(LTM_WAIT_TIME,                LOCKTREE_WAIT_TIME, UINT64, "time waiting for locks", TOKU_ENGINE_STATUS|TOKU_GLOBAL_STATUS);
    STATUS_INIT(LTM_LONG_WAIT_COUNT,          LOCKTREE_LONG_WAIT_COUNT, UINT64, "number of long wait locks", TOKU_ENGINE_STATUS|TOKU_GLOBAL_STATUS);
    STATUS_INIT(LTM_LONG_WAIT_TIME,           LOCKTREE_LONG_WAIT_TIME, UINT64, "long time waiting for locks", TOKU_ENGINE_STATUS|TOKU_GLOBAL_STATUS);
    STATUS_INIT(LTM_TIMEOUT_COUNT,            LOCKTREE_TIMEOUT_COUNT, UINT64, "number of lock timeouts", TOKU_ENGINE_STATUS|TOKU_GLOBAL_STATUS);

    STATUS_INIT(LTM_WAIT_ESCALATION_COUNT,    LOCKTREE_WAIT_ESCALATION_COUNT, UINT64, "number of waits on lock escalation", TOKU_ENGINE_STATUS|TOKU_GLOBAL_STATUS);
    STATUS_INIT(LTM_WAIT_ESCALATION_TIME,     LOCKTREE_WAIT_ESCALATION_TIME, UINT64, "time waiting on lock escalation", TOKU_ENGINE_STATUS|TOKU_GLOBAL_STATUS);
    STATUS_INIT(LTM_LONG_WAIT_ESCALATION_COUNT,    LOCKTREE_LONG_WAIT_ESCALATION_COUNT, UINT64, "number of long waits on lock escalation", TOKU_ENGINE_STATUS|TOKU_GLOBAL_STATUS);
    STATUS_INIT(LTM_LONG_WAIT_ESCALATION_TIME,     LOCKTREE_LONG_WAIT_ESCALATION_TIME, UINT64, "long time waiting on lock escalation", TOKU_ENGINE_STATUS|TOKU_GLOBAL_STATUS);

    status.initialized = true;
}

#undef STATUS_INIT

#define STATUS_VALUE(x) status.status[x].value.num

void locktree::manager::get_status(LTM_STATUS statp) {
    if (!status.initialized) {
        status_init();
    }

    STATUS_VALUE(LTM_SIZE_CURRENT) = m_current_lock_memory;
    STATUS_VALUE(LTM_SIZE_LIMIT) = m_max_lock_memory;
    STATUS_VALUE(LTM_ESCALATION_COUNT) = m_escalation_count;
    STATUS_VALUE(LTM_ESCALATION_TIME) = m_escalation_time;
    STATUS_VALUE(LTM_ESCALATION_LATEST_RESULT) = m_escalation_latest_result;
    STATUS_VALUE(LTM_WAIT_ESCALATION_COUNT) = m_wait_escalation_count;
    STATUS_VALUE(LTM_WAIT_ESCALATION_TIME) = m_wait_escalation_time;
    STATUS_VALUE(LTM_LONG_WAIT_ESCALATION_COUNT) = m_long_wait_escalation_count;
    STATUS_VALUE(LTM_LONG_WAIT_ESCALATION_TIME) = m_long_wait_escalation_time;    

    mutex_lock();

    uint64_t lock_requests_pending = 0;
    uint64_t sto_num_eligible = 0;
    uint64_t sto_end_early_count = 0;
    tokutime_t sto_end_early_time = 0;

    struct lt_counters lt_counters = m_lt_counters;
    
    size_t num_locktrees = m_locktree_map.size();
    for (size_t i = 0; i < num_locktrees; i++) {
        locktree *lt;
        int r = m_locktree_map.fetch(i, &lt);
        invariant_zero(r);

        toku_mutex_lock(&lt->m_lock_request_info.mutex);
        lock_requests_pending += lt->m_lock_request_info.pending_lock_requests.size();
        add_lt_counters(&lt_counters, &lt->m_lock_request_info.counters);
        toku_mutex_unlock(&lt->m_lock_request_info.mutex);

        sto_num_eligible += lt->sto_txnid_is_valid_unsafe() ? 1 : 0;
        sto_end_early_count += lt->m_sto_end_early_count;
        sto_end_early_time += lt->m_sto_end_early_time;
    }

    mutex_unlock();

    STATUS_VALUE(LTM_NUM_LOCKTREES) = num_locktrees;
    STATUS_VALUE(LTM_LOCK_REQUESTS_PENDING) = lock_requests_pending;
    STATUS_VALUE(LTM_STO_NUM_ELIGIBLE) = sto_num_eligible;
    STATUS_VALUE(LTM_STO_END_EARLY_COUNT) = sto_end_early_count;
    STATUS_VALUE(LTM_STO_END_EARLY_TIME) = sto_end_early_time;
    STATUS_VALUE(LTM_WAIT_COUNT) = lt_counters.wait_count;
    STATUS_VALUE(LTM_WAIT_TIME) = lt_counters.wait_time;
    STATUS_VALUE(LTM_LONG_WAIT_COUNT) = lt_counters.long_wait_count;
    STATUS_VALUE(LTM_LONG_WAIT_TIME) = lt_counters.long_wait_time;
    STATUS_VALUE(LTM_TIMEOUT_COUNT) = lt_counters.timeout_count;
    *statp = status;
}
#undef STATUS_VALUE

} /* namespace toku */
