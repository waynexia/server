/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil -*- */
// vim: ft=cpp:expandtab:ts=8:sw=4:softtabstop=4:
#ifndef TOKU_LOGGGER_H
#define TOKU_LOGGGER_H

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

#include <toku_portability.h>
#include <errno.h>

#include <db.h>
#include "fttypes.h"
#include "memory.h"
#include "x1764.h"

struct roll_entry;

#include "logger.h"
#include "rollback.h"
#include "recover.h"
#include "txn.h"

static inline int toku_copy_BYTESTRING(BYTESTRING *target, BYTESTRING val) {
    target->len = val.len;
    target->data = (char *) toku_memdup(val.data, (size_t)val.len);
    if (target->data==0) {
        return get_error_errno();
    }
    return 0;
}
static inline void toku_free_TXNID(TXNID txnid __attribute__((__unused__))) {}
static inline void toku_free_TXNID_PAIR(TXNID_PAIR txnid __attribute__((__unused__))) {}
static inline void toku_free_LSN(LSN lsn __attribute__((__unused__))) {}
static inline void toku_free_uint64_t(uint64_t u __attribute__((__unused__))) {}
static inline void toku_free_uint32_t(uint32_t u __attribute__((__unused__))) {}
static inline void toku_free_uint8_t(uint8_t u __attribute__((__unused__))) {}
static inline void toku_free_FILENUM(FILENUM u __attribute__((__unused__))) {}
static inline void toku_free_BLOCKNUM(BLOCKNUM u __attribute__((__unused__))) {}
static inline void toku_free_bool(bool u __attribute__((__unused__))) {}
static inline void toku_free_XIDP(XIDP xidp) { toku_free(xidp); }
static inline void toku_free_BYTESTRING(BYTESTRING val) { toku_free(val.data); }
static inline void toku_free_FILENUMS(FILENUMS val) { toku_free(val.filenums); }

int toku_maybe_upgrade_log (const char *env_dir, const char *log_dir, LSN * lsn_of_clean_shutdown, bool * upgrade_in_progress);
uint64_t toku_log_upgrade_get_footprint(void);


#endif
