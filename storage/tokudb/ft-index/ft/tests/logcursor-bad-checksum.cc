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

#ident "Copyright (c) 2007, 2008 Tokutek Inc.  All rights reserved."

#include "logcursor.h"
#include "test.h"

// log a couple of timestamp entries and verify the log by walking 
// a cursor through the log entries

static void corrupt_the_checksum(void) {
    // change the LSN in the first log entry of log 0.  this will cause an checksum error.
    char logname[TOKU_PATH_MAX+1];
    int r;
    sprintf(logname,  "%s/log000000000000.tokulog%d", TOKU_TEST_FILENAME, TOKU_LOG_VERSION);
    FILE *f = fopen(logname, "r+b"); assert(f);
    r = fseek(f, 025, SEEK_SET); assert(r == 0);
    char c = 100;
    size_t n = fwrite(&c, sizeof c, 1, f); assert(n == sizeof c);
    r = fclose(f); assert(r == 0);
}

int
test_main (int argc, const char *argv[]) {
    default_parse_args(argc, argv);

    int r;
    toku_os_recursive_delete(TOKU_TEST_FILENAME);
    r = toku_os_mkdir(TOKU_TEST_FILENAME, S_IRWXU);    assert(r==0);
    TOKULOGGER logger;
    LSN lsn = ZERO_LSN;

    // log a couple of timestamp log entries

    r = toku_logger_create(&logger); 
    assert(r == 0);

    r = toku_logger_open(TOKU_TEST_FILENAME, logger);
    assert(r == 0);

    BYTESTRING bs0 = { .len = 5, .data = (char *) "hello" };
    toku_log_comment(logger, &lsn, 0, 0, bs0);

    r = toku_logger_close(&logger);
    assert(r == 0);

    // change the LSN and corrupt the checksum
    corrupt_the_checksum();

    if (!verbose) {
        // redirect stderr
        int devnul = open(DEV_NULL_FILE, O_WRONLY);
        assert(devnul >= 0);
        r = toku_dup2(devnul, fileno(stderr)); assert(r == fileno(stderr));
        r = close(devnul); assert(r == 0);
    }

    // walk forwards
    TOKULOGCURSOR lc = NULL;
    struct log_entry *le;
    
    r = toku_logcursor_create(&lc, TOKU_TEST_FILENAME);
    assert(r == 0 && lc != NULL);

    r = toku_logcursor_next(lc, &le);
    assert(r != 0);

    r = toku_logcursor_destroy(&lc);
    assert(r == 0 && lc == NULL);

    // walk backwards
    r = toku_logcursor_create(&lc, TOKU_TEST_FILENAME);
    assert(r == 0 && lc != NULL);

    r = toku_logcursor_prev(lc, &le);
    assert(r != 0);

    r = toku_logcursor_destroy(&lc);
    assert(r == 0 && lc == NULL);

    toku_os_recursive_delete(TOKU_TEST_FILENAME);

    return 0;
}
