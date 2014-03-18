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
// this test demonstrates that the lock manager can detect a simple deadlock with 2 transactions on 2 threads
// the threads do:
// T(a) put 0, grabs write lock on 0
// T(b) put N-1, grabs write lock on N-1
// T(a) put N-1, try's to grab write lock on N-1, should return lock not granted
// T(b) put 0, try's to grab write lock on 0, should return deadlock
// T(b) abort
// T(a) gets lock W(N-1)
// T(A) commit

#include "test.h"
#include "toku_pthread.h"

struct test_seq {
    int state;
    toku_mutex_t lock;
    toku_cond_t cv;
};

static void test_seq_init(struct test_seq *seq) {
    seq->state = 0;
    toku_mutex_init(&seq->lock, NULL);
    toku_cond_init(&seq->cv, NULL);
}

static void test_seq_destroy(struct test_seq *seq) {
    toku_mutex_destroy(&seq->lock);
    toku_cond_destroy(&seq->cv);
}

static void test_seq_sleep(struct test_seq *seq, int new_state) {
    toku_mutex_lock(&seq->lock);
    while (seq->state != new_state) {
        toku_cond_wait(&seq->cv, &seq->lock);
    }
    toku_mutex_unlock(&seq->lock);
}

static void test_seq_next_state(struct test_seq *seq) {
    toku_mutex_lock(&seq->lock);
    seq->state++;
    toku_cond_broadcast(&seq->cv);
    toku_mutex_unlock(&seq->lock);
}

static void insert_row(DB *db, DB_TXN *txn, int k, int v, int expect_r) {
    DBT key; dbt_init(&key, &k, sizeof k);
    DBT value; dbt_init(&value, &v, sizeof v);
    int r = db->put(db, txn, &key, &value, 0); assert(r == expect_r);
}

struct run_txn_b_arg {
    struct test_seq *test_seq;
    DB_TXN *txn_b;
    DB *db;
    int n;
};

static void *run_txn_b(void *arg) {
    struct run_txn_b_arg *b_arg = (struct run_txn_b_arg *) arg;
    struct test_seq *test_seq = b_arg->test_seq;
    DB_TXN *txn_b = b_arg->txn_b;
    DB *db = b_arg->db;
    int n = b_arg->n;

    test_seq_sleep(test_seq, 1);
    insert_row(db, txn_b, htonl(n-1), n-1, 0);
    test_seq_next_state(test_seq);

    test_seq_sleep(test_seq, 3);
    insert_row(db, txn_b, htonl(0), 0, DB_LOCK_NOTGRANTED);
    test_seq_next_state(test_seq);

    test_seq_sleep(test_seq, 5);
    int r = txn_b->abort(txn_b); assert(r == 0);    

    return arg;
}

static void simple_deadlock(DB_ENV *db_env, DB *db, int do_txn, int n) {
    int r;

    DB_TXN *txn_init = NULL;
    if (do_txn) {
        r = db_env->txn_begin(db_env, NULL, &txn_init, 0); assert(r == 0);
    }
    
    for (int k = 0; k < n; k++) {
        insert_row(db, txn_init, htonl(k), k, 0);
    }

    if (do_txn) {
        r = txn_init->commit(txn_init, 0); assert(r == 0);
    }

    uint32_t txn_flags = 0;
#if USE_BDB
    txn_flags = DB_TXN_NOWAIT; // force no wait for BDB to avoid a bug described below
#endif

    DB_TXN *txn_a = NULL;
    if (do_txn) {
        r = db_env->txn_begin(db_env, NULL, &txn_a, txn_flags); assert(r == 0);
    }

    DB_TXN *txn_b = NULL;
    if (do_txn) {
        r = db_env->txn_begin(db_env, NULL, &txn_b, txn_flags); assert(r == 0);
    }

    struct test_seq test_seq; ZERO_STRUCT(test_seq); test_seq_init(&test_seq);

    toku_pthread_t tid;
    struct run_txn_b_arg arg = { &test_seq, txn_b, db, n};
    r = toku_pthread_create(&tid, NULL, run_txn_b, &arg);

    test_seq_sleep(&test_seq, 0);
    insert_row(db, txn_a, htonl(0), 0, 0);
    test_seq_next_state(&test_seq);

    test_seq_sleep(&test_seq, 2);
    // BDB does not time out this lock request, so the test hangs. it looks like a bug in bdb's __lock_get_internal.
    insert_row(db, txn_a, htonl(n-1), n-1, DB_LOCK_NOTGRANTED);
    test_seq_next_state(&test_seq);

    test_seq_sleep(&test_seq, 4);
    if (do_txn) {
        r = txn_a->abort(txn_a); assert(r == 0);
    }
    test_seq_next_state(&test_seq);

    void *ret = NULL;
    r = toku_pthread_join(tid, &ret); assert(r == 0);

    test_seq_destroy(&test_seq);
}

int test_main(int argc, char * const argv[]) {
    uint64_t cachesize = 0;
    uint32_t pagesize = 0;
    int do_txn = 1;
    int nrows = 1000;
    const char *db_env_dir = TOKU_TEST_FILENAME;
    const char *db_filename = "simple_deadlock";
    int db_env_open_flags = DB_CREATE | DB_PRIVATE | DB_INIT_MPOOL | DB_INIT_TXN | DB_INIT_LOCK | DB_INIT_LOG | DB_THREAD;

    // parse_args(argc, argv);
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
            verbose++;
            continue;
        }
        if (strcmp(argv[i], "-q") == 0 || strcmp(argv[i], "--quiet") == 0) {
            if (verbose > 0)
                verbose--;
            continue;
        }
        if (strcmp(argv[i], "-n") == 0 && i+1 < argc) {
            nrows = atoi(argv[++i]);
            continue;
        }
        assert(0);
    }

    // setup env
    int r;
    char rm_cmd[strlen(db_env_dir) + strlen("rm -rf ") + 1];
    snprintf(rm_cmd, sizeof(rm_cmd), "rm -rf %s", db_env_dir);
    r = system(rm_cmd); assert(r == 0);

    r = toku_os_mkdir(db_env_dir, S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH); assert(r == 0);

    DB_ENV *db_env = NULL;
    r = db_env_create(&db_env, 0); assert(r == 0);
    if (cachesize) {
        const uint64_t gig = 1 << 30;
        r = db_env->set_cachesize(db_env, cachesize / gig, cachesize % gig, 1); assert(r == 0);
    }
    if (!do_txn)
        db_env_open_flags &= ~(DB_INIT_TXN | DB_INIT_LOG);
#if USE_BDB
    r = db_env->set_flags(db_env, DB_TIME_NOTGRANTED, 1); assert(r == 0); // force DB_LOCK_DEADLOCK to DB_LOCK_NOTGRANTED
#endif
    r = db_env->open(db_env, db_env_dir, db_env_open_flags, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH); assert(r == 0);
#if defined(USE_TDB)
    r = db_env->set_lock_timeout(db_env, 0, nullptr); assert(r == 0); // no wait
#elif defined(USE_BDB)
    r = db_env->set_lk_detect(db_env, DB_LOCK_YOUNGEST); assert(r == 0);
    r = db_env->set_timeout(db_env, 10000, DB_SET_LOCK_TIMEOUT); assert(r == 0);
#endif

    // create the db
    DB *db = NULL;
    r = db_create(&db, db_env, 0); assert(r == 0);
    DB_TXN *create_txn = NULL;
    if (do_txn) {
        r = db_env->txn_begin(db_env, NULL, &create_txn, 0); assert(r == 0);
    }
    if (pagesize) {
        r = db->set_pagesize(db, pagesize); assert(r == 0);
    }
    r = db->open(db, create_txn, db_filename, NULL, DB_BTREE, DB_CREATE, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH); assert(r == 0);
    if (do_txn) {
        r = create_txn->commit(create_txn, 0); assert(r == 0);
    }

    // run test
    simple_deadlock(db_env, db, do_txn, nrows);

    // close env
    r = db->close(db, 0); assert(r == 0); db = NULL;
    r = db_env->close(db_env, 0); assert(r == 0); db_env = NULL;

    return 0;
}
