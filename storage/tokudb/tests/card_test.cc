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
// test tokudb cardinality in status dictionary
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <assert.h>
#include <memory.h>
#include <errno.h>
#include <sys/stat.h>
#include <db.h>
typedef unsigned long long ulonglong;
#include <tokudb_status.h>
#include <tokudb_buffer.h>

#include "fake_mysql.h"

#if __APPLE__
typedef unsigned long ulong;
#endif
#include <tokudb_card.h>

// verify that we can create and close a status dictionary
static void test_create(DB_ENV *env) {
    int error;

    DB_TXN *txn = NULL;
    error = env->txn_begin(env, NULL, &txn, 0);
    assert(error == 0);

    DB *status_db = NULL;
    error = tokudb::create_status(env, &status_db, "status.db", txn);
    assert(error == 0);

    error = txn->commit(txn, 0);
    assert(error == 0);

    error = tokudb::close_status(&status_db);
    assert(error == 0);
}

// verify that no card row in status works
static void test_no_card(DB_ENV *env) {
    int error;

    DB_TXN *txn = NULL;
    error = env->txn_begin(env, NULL, &txn, 0);
    assert(error == 0);

    DB *status_db = NULL;
    error = tokudb::open_status(env, &status_db, "status.db", txn);
    assert(error == 0);

    error = tokudb::get_card_from_status(status_db, txn, 0, NULL);
    assert(error == DB_NOTFOUND);

    error = txn->commit(txn, 0);
    assert(error == 0);

    error = tokudb::close_status(&status_db);
    assert(error == 0);
}

// verify that a card row with 0 array elements works
static void test_0(DB_ENV *env) {
    int error;

    DB_TXN *txn = NULL;
    error = env->txn_begin(env, NULL, &txn, 0);
    assert(error == 0);

    DB *status_db = NULL;
    error = tokudb::open_status(env, &status_db, "status.db", txn);
    assert(error == 0);

    tokudb::set_card_in_status(status_db, txn, 0, NULL);

    error = tokudb::get_card_from_status(status_db, txn, 0, NULL);
    assert(error == 0);

    error = txn->commit(txn, 0);
    assert(error == 0);

    error = tokudb::close_status(&status_db);
    assert(error == 0);
}

// verify that writing and reading card info works for several sized card arrays
static void test_10(DB_ENV *env) {
    int error;

    for (uint64_t i = 0; i < 20; i++) {

        uint64_t rec_per_key[i];
        for (uint64_t j = 0; j < i; j++) 
            rec_per_key[j] = j == 0 ? 10+i : 10 * rec_per_key[j-1];

        DB_TXN *txn = NULL;
        error = env->txn_begin(env, NULL, &txn, 0);
        assert(error == 0);

        DB *status_db = NULL;
        error = tokudb::open_status(env, &status_db, "status.db", txn);
        assert(error == 0);

        tokudb::set_card_in_status(status_db, txn, i, rec_per_key);

        uint64_t stored_rec_per_key[i];
        error = tokudb::get_card_from_status(status_db, txn, i, stored_rec_per_key);
        assert(error == 0);

        for (uint64_t j = 0; j < i; j++) 
            assert(rec_per_key[j] == stored_rec_per_key[j]);
        
        error = txn->commit(txn, 0);
        assert(error == 0);
        
        error = tokudb::close_status(&status_db);
        assert(error == 0);

        error = env->txn_begin(env, NULL, &txn, 0);
        assert(error == 0);

        error = tokudb::open_status(env, &status_db, "status.db", txn);
        assert(error == 0);
        
        tokudb::set_card_in_status(status_db, txn, i, rec_per_key);

        error = tokudb::get_card_from_status(status_db, txn, i, stored_rec_per_key);
        assert(error == 0);

        for (uint64_t j = 0; j < i; j++) 
            assert(rec_per_key[j] == stored_rec_per_key[j]);
        
        error = txn->commit(txn, 0);
        assert(error == 0);
        
        error = tokudb::close_status(&status_db);
        assert(error == 0);
    }
}

int main() {
    int error;

    error = system("rm -rf " __FILE__ ".testdir");
    assert(error == 0);

    error = mkdir(__FILE__ ".testdir", S_IRWXU+S_IRWXG+S_IRWXO);
    assert(error == 0);

    DB_ENV *env = NULL;
    error = db_env_create(&env, 0);
    assert(error == 0);

    error = env->open(env, __FILE__ ".testdir", DB_INIT_MPOOL + DB_INIT_LOG + DB_INIT_LOCK + DB_INIT_TXN + DB_PRIVATE + DB_CREATE, S_IRWXU+S_IRWXG+S_IRWXO); 
    assert(error == 0);

    test_create(env);
    test_no_card(env);
    test_0(env);
    test_10(env);

    error = env->close(env, 0);
    assert(error == 0);
    
    return 0;
}
