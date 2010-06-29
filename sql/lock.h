#ifndef LOCK_INCLUDED
#define LOCK_INCLUDED

#include "thr_lock.h"                           /* thr_lock_type */

// Forward declarations
struct TABLE;
struct TABLE_LIST;
class THD;
typedef struct st_mysql_lock MYSQL_LOCK;

/* mysql_lock_tables() and open_table() flags bits */
#define MYSQL_OPEN_IGNORE_GLOBAL_READ_LOCK      0x0001
#define MYSQL_OPEN_IGNORE_FLUSH                 0x0002
#define MYSQL_OPEN_TEMPORARY_ONLY               0x0004
#define MYSQL_LOCK_IGNORE_GLOBAL_READ_ONLY      0x0008
#define MYSQL_LOCK_LOG_TABLE                    0x0010
/**
  Do not try to acquire a metadata lock on the table: we
  already have one.
*/
#define MYSQL_OPEN_HAS_MDL_LOCK                 0x0020
/**
  If in locked tables mode, ignore the locked tables and get
  a new instance of the table.
*/
#define MYSQL_OPEN_GET_NEW_TABLE                0x0040
/** Don't look up the table in the list of temporary tables. */
#define MYSQL_OPEN_SKIP_TEMPORARY               0x0080
/** Fail instead of waiting when conficting metadata lock is discovered. */
#define MYSQL_OPEN_FAIL_ON_MDL_CONFLICT         0x0100
/** Open tables using MDL_SHARED lock instead of one specified in parser. */
#define MYSQL_OPEN_FORCE_SHARED_MDL             0x0200
/**
  Open tables using MDL_SHARED_HIGH_PRIO lock instead of one specified
  in parser.
*/
#define MYSQL_OPEN_FORCE_SHARED_HIGH_PRIO_MDL   0x0400
/**
  When opening or locking the table, use the maximum timeout
  (LONG_TIMEOUT = 1 year) rather than the user-supplied timeout value.
*/
#define MYSQL_LOCK_IGNORE_TIMEOUT               0x0800

/** Please refer to the internals manual. */
#define MYSQL_OPEN_REOPEN  (MYSQL_OPEN_IGNORE_FLUSH |\
                            MYSQL_OPEN_IGNORE_GLOBAL_READ_LOCK |\
                            MYSQL_LOCK_IGNORE_GLOBAL_READ_ONLY |\
                            MYSQL_LOCK_IGNORE_TIMEOUT |\
                            MYSQL_OPEN_GET_NEW_TABLE |\
                            MYSQL_OPEN_SKIP_TEMPORARY |\
                            MYSQL_OPEN_HAS_MDL_LOCK)


MYSQL_LOCK *mysql_lock_tables(THD *thd, TABLE **table, uint count, uint flags);
void mysql_unlock_tables(THD *thd, MYSQL_LOCK *sql_lock);
void mysql_unlock_read_tables(THD *thd, MYSQL_LOCK *sql_lock);
void mysql_unlock_some_tables(THD *thd, TABLE **table,uint count);
void mysql_lock_remove(THD *thd, MYSQL_LOCK *locked,TABLE *table);
void mysql_lock_abort(THD *thd, TABLE *table, bool upgrade_lock);
bool mysql_lock_abort_for_thread(THD *thd, TABLE *table);
MYSQL_LOCK *mysql_lock_merge(MYSQL_LOCK *a,MYSQL_LOCK *b);
void broadcast_refresh(void);
/* Lock based on name */
bool lock_table_names(THD *thd, TABLE_LIST *table_list);
void unlock_table_names(THD *thd);
/* Lock based on stored routine name */
bool lock_routine_name(THD *thd, bool is_function, const char *db,
                       const char *name);

#endif /* LOCK_INCLUDED */
