/* Copyright (c) 2010, Oracle and/or its affiliates. All rights reserved.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA */

#include "sql_reload.h"
#include "sql_priv.h"
#include "mysqld.h"      // select_errors
#include "sql_class.h"   // THD
#include "sql_acl.h"     // acl_reload
#include "sql_servers.h" // servers_reload
#include "sql_connect.h" // reset_mqh
#include "sql_base.h"    // close_cached_tables
#include "sql_db.h"      // my_dbopt_cleanup
#include "hostname.h"    // hostname_cache_refresh
#include "sql_repl.h"    // reset_master, reset_slave
#include "debug_sync.h"


/**
  Reload/resets privileges and the different caches.

  @param thd Thread handler (can be NULL!)
  @param options What should be reset/reloaded (tables, privileges, slave...)
  @param tables Tables to flush (if any)
  @param write_to_binlog True if we can write to the binlog.
               
  @note Depending on 'options', it may be very bad to write the
    query to the binlog (e.g. FLUSH SLAVE); this is a
    pointer where reload_acl_and_cache() will put 0 if
    it thinks we really should not write to the binlog.
    Otherwise it will put 1.

  @return Error status code
    @retval 0 Ok
    @retval !=0  Error; thd->killed is set or thd->is_error() is true
*/

bool reload_acl_and_cache(THD *thd, unsigned long options,
                          TABLE_LIST *tables, bool *write_to_binlog)
{
  bool result=0;
  select_errors=0;				/* Write if more errors */
  bool tmp_write_to_binlog= 1;

  DBUG_ASSERT(!thd || !thd->in_sub_stmt);

#ifndef NO_EMBEDDED_ACCESS_CHECKS
  if (options & REFRESH_GRANT)
  {
    THD *tmp_thd= 0;
    /*
      If reload_acl_and_cache() is called from SIGHUP handler we have to
      allocate temporary THD for execution of acl_reload()/grant_reload().
    */
    if (!thd && (thd= (tmp_thd= new THD)))
    {
      thd->thread_stack= (char*) &tmp_thd;
      thd->store_globals();
    }

    if (thd)
    {
      bool reload_acl_failed= acl_reload(thd);
      bool reload_grants_failed= grant_reload(thd);
      bool reload_servers_failed= servers_reload(thd);

      if (reload_acl_failed || reload_grants_failed || reload_servers_failed)
      {
        result= 1;
        /*
          When an error is returned, my_message may have not been called and
          the client will hang waiting for a response.
        */
        my_error(ER_UNKNOWN_ERROR, MYF(0), "FLUSH PRIVILEGES failed");
      }
    }

    if (tmp_thd)
    {
      delete tmp_thd;
      /* Remember that we don't have a THD */
      my_pthread_setspecific_ptr(THR_THD,  0);
      thd= 0;
    }
    reset_mqh((LEX_USER *)NULL, TRUE);
  }
#endif
  if (options & REFRESH_LOG)
  {
    /*
      Flush the normal query log, the update log, the binary log,
      the slow query log, the relay log (if it exists) and the log
      tables.
    */

    options|= REFRESH_BINARY_LOG;
    options|= REFRESH_RELAY_LOG;
    options|= REFRESH_SLOW_LOG;
    options|= REFRESH_GENERAL_LOG;
    options|= REFRESH_ENGINE_LOG;
    options|= REFRESH_ERROR_LOG;
  }

  if (options & REFRESH_ERROR_LOG)
    if (flush_error_log())
      result= 1;

  if ((options & REFRESH_SLOW_LOG) && opt_slow_log)
    logger.flush_slow_log();

  if ((options & REFRESH_GENERAL_LOG) && opt_log)
    logger.flush_general_log();

  if (options & REFRESH_ENGINE_LOG)
    if (ha_flush_logs(NULL))
      result= 1;

  if (options & REFRESH_BINARY_LOG)
  {
    /*
      Writing this command to the binlog may result in infinite loops
      when doing mysqlbinlog|mysql, and anyway it does not really make
      sense to log it automatically (would cause more trouble to users
      than it would help them)
    */
    tmp_write_to_binlog= 0;
    if (mysql_bin_log.is_open())
      mysql_bin_log.rotate_and_purge(RP_FORCE_ROTATE);
  }
  if (options & REFRESH_RELAY_LOG)
  {
#ifdef HAVE_REPLICATION
    mysql_mutex_lock(&LOCK_active_mi);
    rotate_relay_log(active_mi);
    mysql_mutex_unlock(&LOCK_active_mi);
#endif
  }
#ifdef HAVE_QUERY_CACHE
  if (options & REFRESH_QUERY_CACHE_FREE)
  {
    query_cache.pack();				// FLUSH QUERY CACHE
    options &= ~REFRESH_QUERY_CACHE;    // Don't flush cache, just free memory
  }
  if (options & (REFRESH_TABLES | REFRESH_QUERY_CACHE))
  {
    query_cache.flush();			// RESET QUERY CACHE
  }
#endif /*HAVE_QUERY_CACHE*/

  DBUG_ASSERT(!thd || thd->locked_tables_mode ||
              !thd->mdl_context.has_locks() ||
              thd->handler_tables_hash.records ||
              thd->global_read_lock.is_acquired());

  /*
    Note that if REFRESH_READ_LOCK bit is set then REFRESH_TABLES is set too
    (see sql_yacc.yy)
  */
  if (options & (REFRESH_TABLES | REFRESH_READ_LOCK)) 
  {
    if ((options & REFRESH_READ_LOCK) && thd)
    {
      /*
        On the first hand we need write lock on the tables to be flushed,
        on the other hand we must not try to aspire a global read lock
        if we have a write locked table as this would lead to a deadlock
        when trying to reopen (and re-lock) the table after the flush.
      */
      if (thd->locked_tables_mode)
      {
        my_error(ER_LOCK_OR_ACTIVE_TRANSACTION, MYF(0));
        return 1;
      }
      /*
	Writing to the binlog could cause deadlocks, as we don't log
	UNLOCK TABLES
      */
      tmp_write_to_binlog= 0;
      if (thd->global_read_lock.lock_global_read_lock(thd))
	return 1;                               // Killed
      if (close_cached_tables(thd, tables,
                              ((options & REFRESH_FAST) ?  FALSE : TRUE),
                              thd->variables.lock_wait_timeout))
        result= 1;

      if (thd->global_read_lock.make_global_read_lock_block_commit(thd)) // Killed
      {
        /* Don't leave things in a half-locked state */
        thd->global_read_lock.unlock_global_read_lock(thd);
        return 1;
      }
    }
    else
    {
      if (thd && thd->locked_tables_mode)
      {
        /*
          If we are under LOCK TABLES we should have a write
          lock on tables which we are going to flush.
        */
        if (tables)
        {
          for (TABLE_LIST *t= tables; t; t= t->next_local)
            if (!find_table_for_mdl_upgrade(thd->open_tables, t->db,
                                            t->table_name, FALSE))
              return 1;
        }
        else
        {
          for (TABLE *tab= thd->open_tables; tab; tab= tab->next)
          {
            if (! tab->mdl_ticket->is_upgradable_or_exclusive())
            {
              my_error(ER_TABLE_NOT_LOCKED_FOR_WRITE, MYF(0),
                       tab->s->table_name.str);
              return 1;
            }
          }
        }
      }

      if (close_cached_tables(thd, tables,
                              ((options & REFRESH_FAST) ?  FALSE : TRUE),
                              (thd ? thd->variables.lock_wait_timeout :
                               LONG_TIMEOUT)))
        result= 1;
    }
    my_dbopt_cleanup();
  }
  if (options & REFRESH_HOSTS)
    hostname_cache_refresh();
  if (thd && (options & REFRESH_STATUS))
    refresh_status(thd);
  if (options & REFRESH_THREADS)
    flush_thread_cache();
#ifdef HAVE_REPLICATION
  if (options & REFRESH_MASTER)
  {
    DBUG_ASSERT(thd);
    tmp_write_to_binlog= 0;
    if (reset_master(thd))
    {
      result=1;
    }
  }
#endif
#ifdef OPENSSL
   if (options & REFRESH_DES_KEY_FILE)
   {
     if (des_key_file && load_des_key_file(des_key_file))
         result= 1;
   }
#endif
#ifdef HAVE_REPLICATION
 if (options & REFRESH_SLAVE)
 {
   tmp_write_to_binlog= 0;
   mysql_mutex_lock(&LOCK_active_mi);
   if (reset_slave(thd, active_mi))
     result=1;
   mysql_mutex_unlock(&LOCK_active_mi);
 }
#endif
 if (options & REFRESH_USER_RESOURCES)
   reset_mqh((LEX_USER *) NULL, 0);             /* purecov: inspected */
  if (options & REFRESH_TABLE_STATS)
  {
    mysql_mutex_lock(&LOCK_global_table_stats);
    free_global_table_stats();
    init_global_table_stats();
    mysql_mutex_unlock(&LOCK_global_table_stats);
  }
  if (options & REFRESH_INDEX_STATS)
  {
    mysql_mutex_lock(&LOCK_global_index_stats);
    free_global_index_stats();
    init_global_index_stats();
    mysql_mutex_unlock(&LOCK_global_index_stats);
  }
  if (options & (REFRESH_USER_STATS | REFRESH_CLIENT_STATS))
  {
    mysql_mutex_lock(&LOCK_global_user_client_stats);
    if (options & REFRESH_USER_STATS)
    {
      free_global_user_stats();
      init_global_user_stats();
    }
    if (options & REFRESH_CLIENT_STATS)
    {
      free_global_client_stats();
      init_global_client_stats();
    }
    mysql_mutex_unlock(&LOCK_global_user_client_stats);
  }
 *write_to_binlog= tmp_write_to_binlog;
 /*
   If the query was killed then this function must fail.
 */
 return result || (thd ? thd->killed : 0);
}


/**
  Implementation of FLUSH TABLES <table_list> WITH READ LOCK.

  In brief: take exclusive locks, expel tables from the table
  cache, reopen the tables, enter the 'LOCKED TABLES' mode,
  downgrade the locks.
  Note: the function is written to be called from
  mysql_execute_command(), it is not reusable in arbitrary
  execution context.

  Required privileges
  -------------------
  Since the statement implicitly enters LOCK TABLES mode,
  it requires LOCK TABLES privilege on every table.
  But since the rest of FLUSH commands require
  the global RELOAD_ACL, it also requires RELOAD_ACL.

  Compatibility with the global read lock
  ---------------------------------------
  We don't wait for the GRL, since neither the
  5.1 combination that this new statement is intended to
  replace (LOCK TABLE <list> WRITE; FLUSH TABLES;),
  nor FLUSH TABLES WITH READ LOCK do.
  @todo: this is not implemented, Dmitry disagrees.
  Currently we wait for GRL in another connection,
  but are compatible with a GRL in our own connection.

  Behaviour under LOCK TABLES
  ---------------------------
  Bail out: i.e. don't perform an implicit UNLOCK TABLES.
  This is not consistent with LOCK TABLES statement, but is
  in line with behaviour of FLUSH TABLES WITH READ LOCK, and we
  try to not introduce any new statements with implicit
  semantics.

  Compatibility with parallel updates
  -----------------------------------
  As a result, we will wait for all open transactions
  against the tables to complete. After the lock downgrade,
  new transactions will be able to read the tables, but not
  write to them.

  Differences from FLUSH TABLES <list>
  -------------------------------------
  - you can't flush WITH READ LOCK a non-existent table
  - you can't flush WITH READ LOCK under LOCK TABLES
  - currently incompatible with the GRL (@todo: fix)

  Effect on views and temporary tables.
  ------------------------------------
  You can only apply this command to existing base tables.
  If a view with such name exists, ER_WRONG_OBJECT is returned.
  If a temporary table with such name exists, it's ignored:
  if there is a base table, it's used, otherwise ER_NO_SUCH_TABLE
  is returned.

  Implicit commit
  ---------------
  This statement causes an implicit commit before and
  after it.

  HANDLER SQL
  -----------
  If this connection has HANDLERs open against
  some of the tables being FLUSHed, these handlers
  are implicitly flushed (lose their position).
*/

bool flush_tables_with_read_lock(THD *thd, TABLE_LIST *all_tables)
{
  Lock_tables_prelocking_strategy lock_tables_prelocking_strategy;
  TABLE_LIST *table_list;
  MDL_request_list mdl_requests;

  /*
    This is called from SQLCOM_FLUSH, the transaction has
    been committed implicitly.
  */

  if (thd->locked_tables_mode)
  {
    my_error(ER_LOCK_OR_ACTIVE_TRANSACTION, MYF(0));
    goto error;
  }

  /*
    Acquire SNW locks on tables to be flushed. We can't use
    lock_table_names() here as this call will also acquire global IX
    and database-scope IX locks on the tables, and this will make
    this statement incompatible with FLUSH TABLES WITH READ LOCK.
  */
  for (table_list= all_tables; table_list;
       table_list= table_list->next_global)
    mdl_requests.push_front(&table_list->mdl_request);

  if (thd->mdl_context.acquire_locks(&mdl_requests,
                                     thd->variables.lock_wait_timeout))
    goto error;

  DEBUG_SYNC(thd,"flush_tables_with_read_lock_after_acquire_locks");

  for (table_list= all_tables; table_list;
       table_list= table_list->next_global)
  {
    /* Request removal of table from cache. */
    tdc_remove_table(thd, TDC_RT_REMOVE_UNUSED,
                     table_list->db,
                     table_list->table_name, FALSE);

    /* Skip views and temporary tables. */
    table_list->required_type= FRMTYPE_TABLE; /* Don't try to flush views. */
    table_list->open_type= OT_BASE_ONLY;      /* Ignore temporary tables. */
  }

  /*
    Before opening and locking tables the below call also waits
    for old shares to go away, so the fact that we don't pass
    MYSQL_LOCK_IGNORE_FLUSH flag to it is important.
  */
  if  (open_and_lock_tables(thd, all_tables, FALSE,
                            MYSQL_OPEN_HAS_MDL_LOCK,
                            &lock_tables_prelocking_strategy) ||
       thd->locked_tables_list.init_locked_tables(thd))
  {
    goto error;
  }
  thd->variables.option_bits|= OPTION_TABLE_LOCK;

  /*
    We don't downgrade MDL_SHARED_NO_WRITE here as the intended
    post effect of this call is identical to LOCK TABLES <...> READ,
    and we didn't use thd->in_lock_talbes and
    thd->sql_command= SQLCOM_LOCK_TABLES hacks to enter the LTM.
  */

  return FALSE;

error:
  return TRUE;
}



