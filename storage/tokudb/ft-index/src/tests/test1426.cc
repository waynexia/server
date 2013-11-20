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
#include "test.h"
/* Test for #1426. Make sure deletes and inserts in a FIFO work. */
/* This test is run using a special makefile rule that runs the TDB version and the BDB version, dumps their outputs, and compares them */

#include <db.h>
#include <memory.h>
#include <fcntl.h>

// |DB_INIT_TXN| DB_INIT_LOG  | DB_RECOVER
const int envflags = DB_CREATE|DB_INIT_MPOOL|DB_INIT_LOCK |DB_THREAD |DB_PRIVATE;

DB_ENV *env;
DB     *db;
DB_TXN * const null_txn = NULL;

static void
empty_cachetable (void)
// Make all the cachetable entries clean.
// Brute force it by closing and reopening everything.
{
    int r;
    r = db->close(db, 0);                                                 CKERR(r);
    r = env->close(env, 0);                                               CKERR(r);
    r = db_env_create(&env, 0);                                           CKERR(r);
#ifdef TOKUDB
    r = env->set_cachesize(env, 0, 10000000, 1);                          CKERR(r);
#endif
    r = env->open(env, TOKU_TEST_FILENAME, envflags, S_IRWXU+S_IRWXG+S_IRWXO);        CKERR(r);
    r = db_create(&db, env, 0);                                           CKERR(r);
    r = db->open(db, null_txn, "main", 0,     DB_BTREE, 0, 0666);         CKERR(r);
}

static void
do_insert_delete_fifo (void)
{
    int r;
    toku_os_recursive_delete(TOKU_TEST_FILENAME);
    toku_os_mkdir(TOKU_TEST_FILENAME, S_IRWXU+S_IRWXG+S_IRWXO);
    
    r = db_env_create(&env, 0);                                           CKERR(r);
#ifdef TOKUDB
    r = env->set_cachesize(env, 0, 10000000, 1);                          CKERR(r);
#endif
    r = env->open(env, TOKU_TEST_FILENAME, envflags, S_IRWXU+S_IRWXG+S_IRWXO);        CKERR(r);
    r = db_create(&db, env, 0);                                           CKERR(r);
    r = db->set_pagesize(db, 4096);                                       CKERR(r);
    r = db->open(db, null_txn, "main", 0,     DB_BTREE, DB_CREATE, 0666); CKERR(r);
    {
	uint64_t i;
	uint64_t n_deleted = 0;
	uint64_t N=20000; // total number to insert
	uint64_t M= 5000; // size of rolling fifo
	uint64_t D=  200; // number to delete at once
	for (i=0; i<N; i++) {
	    {
		char k[100],v[100];
		int keylen = snprintf(k, sizeof k, "%016" PRIu64 "key", i);
                uint32_t rand1 = myrandom();
                uint32_t rand2 = myrandom();
                uint32_t rand3 = myrandom();
		int vallen = snprintf(v, sizeof v, "%016" PRIu64 "val%08x%08x%08x", i, rand1, rand2, rand3);
		DBT kt, vt;
		r = db->put(db, null_txn, dbt_init(&kt, k, keylen) , dbt_init(&vt, v, vallen), 0);    CKERR(r);
	    }
	    if (i%D==0) {
		// Once every D steps, delete everything until there are only M things left.
		// Flush the data down the tree for all the values we will do
		{
		    uint64_t peek_here = n_deleted;
		    while (peek_here + M < i) {
			char k[100];
			int keylen = snprintf(k, sizeof k, "%016" PRIu64 "key", peek_here);
			DBT kt;
			DBT vt;
			memset(&vt, 0, sizeof(vt));
			vt.flags = DB_DBT_MALLOC;
			r = db->get(db, null_txn, dbt_init(&kt, k, keylen), &vt, 0); CKERR(r);
			peek_here++;
			toku_free(vt.data);
		    }
		}
		empty_cachetable();
		while (n_deleted + M < i) {
		    char k[100];
		    int keylen = snprintf(k, sizeof k, "%016" PRIu64 "key", n_deleted);
		    DBT kt;
		    r = db->del(db, null_txn, dbt_init(&kt, k, keylen), 0);
		    if (r!=0) printf("error %d %s", r, db_strerror(r));
		    CKERR(r);
		    n_deleted++;
		    empty_cachetable();
		}
	    }
	}
    }
    r = db->close(db, 0);                                                 CKERR(r);
    r = env->close(env, 0);                                               CKERR(r);
}

int
test_main (int argc, char *const argv[])
{
    parse_args(argc, argv);
    do_insert_delete_fifo();
    return 0;
}

