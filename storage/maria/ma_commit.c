/* Copyright (C) 2007 MySQL AB

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

#include "maria_def.h"
#include "trnman.h"

/**
   @brief writes a COMMIT record to log and commits transaction in memory

   @param  trn              transaction

   @return Operation status
     @retval 0      ok
     @retval 1      error (disk error or out of memory)
*/

int ma_commit(TRN *trn)
{
  if (trn->undo_lsn == 0) /* no work done, rollback (cheaper than commit) */
    return trnman_rollback_trn(trn);
  /*
    - if COMMIT record is written before trnman_commit_trn():
    if Checkpoint comes in the middle it will see trn is not committed,
    then if crash, Recovery might roll back trn (if min(rec_lsn) is after
    COMMIT record) and this is not an issue as
    * transaction's updates were not made visible to other transactions
    * "commit ok" was not sent to client
    Alternatively, Recovery might commit trn (if min(rec_lsn) is before COMMIT
    record), which is ok too. All in all it means that "trn committed" is not
    100% equal to "COMMIT record written".
    - if COMMIT record is written after trnman_commit_trn():
    if crash happens between the two, trn will be rolled back which is an
    issue (transaction's updates were made visible to other transactions).
    So we need to go the first way.
  */
  /**
     @todo RECOVERY share's state is written to disk only in
     maria_lock_database(), so COMMIT record is not the last record of the
     transaction! It is probably an issue. Recovery of the state is a problem
     not yet solved.
  */
  LSN commit_lsn;
  LEX_STRING log_array[TRANSLOG_INTERNAL_PARTS];
  /*
    We do not store "thd->transaction.xid_state.xid" for now, it will be
    needed only when we support XA.
  */
  return
    translog_write_record(&commit_lsn, LOGREC_COMMIT,
                          trn, NULL, 0,
                          sizeof(log_array)/sizeof(log_array[0]),
                          log_array, NULL) ||
    translog_flush(commit_lsn) || trnman_commit_trn(trn);
  /*
    Note: if trnman_commit_trn() fails above, we have already
    written the COMMIT record, so Checkpoint and Recovery will see the
    transaction as committed.
  */
}
