/* Copyright (C) 2000-2002 MySQL AB

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA */


/* This file includes all reserved words and functions */

#include "lex_symbol.h"

/* We don't want to include sql_yacc.h into gen_lex_hash */

#ifdef NO_YACC_SYMBOLS
#define SYM(A) 0
#define CREATE_FUNC(A) 0
#else
#define SYM(A) A
#define CREATE_FUNC(A) (void*) (A)
#endif

/*
  Symbols are broken into separated arrays to allow field names with
  same name as functions.
  These are kept sorted for human lookup (the symbols are hashed).
*/

static SYMBOL symbols[] = {
  { "&&",		SYM(AND),0,0},
  { "<",		SYM(LT),0,0},
  { "<=",		SYM(LE),0,0},
  { "<>",		SYM(NE),0,0},
  { "!=",		SYM(NE),0,0},
  { "=",		SYM(EQ),0,0},
  { ">",		SYM(GT_SYM),0,0},
  { ">=",		SYM(GE),0,0},
  { "<<",		SYM(SHIFT_LEFT),0,0},
  { ">>",		SYM(SHIFT_RIGHT),0,0},
  { "<=>",		SYM(EQUAL_SYM),0,0},
  { "ACTION",		SYM(ACTION),0,0},
  { "ADD",		SYM(ADD),0,0},
  { "AGGREGATE",	SYM(AGGREGATE_SYM),0,0},
  { "ALL",		SYM(ALL),0,0},
  { "ALTER",		SYM(ALTER),0,0},
  { "AFTER",		SYM(AFTER_SYM),0,0},
  { "AGAINST",          SYM(AGAINST),0,0},
  { "ANALYZE",		SYM(ANALYZE_SYM),0,0},
  { "AND",		SYM(AND),0,0},
  { "ANY",              SYM(ANY_SYM),0,0},
  { "AS",		SYM(AS),0,0},
  { "ASC",		SYM(ASC),0,0},
  { "ASCII",		SYM(ASCII_SYM),0,0},
  { "AVG",		SYM(AVG_SYM),0,0},
  { "AVG_ROW_LENGTH",	SYM(AVG_ROW_LENGTH),0,0},
  { "AUTO_INCREMENT",	SYM(AUTO_INC),0,0},
  { "BACKUP",	        SYM(BACKUP_SYM),0,0},
  { "BEFORE",	        SYM(BEFORE_SYM),0,0},
  { "BEGIN",	        SYM(BEGIN_SYM),0,0},
  { "BERKELEYDB",       SYM(BERKELEY_DB_SYM),0,0},
  { "BDB",              SYM(BERKELEY_DB_SYM),0,0},
  { "BETWEEN",		SYM(BETWEEN_SYM),0,0},
  { "BIGINT",		SYM(BIGINT),0,0},
  { "BIT",		SYM(BIT_SYM),0,0},
  { "BINARY",		SYM(BINARY),0,0},
  { "BINLOG",		SYM(BINLOG_SYM),0,0},
  { "BLOB",		SYM(BLOB_SYM),0,0},
  { "BOOL",		SYM(BOOL_SYM),0,0},
  { "BOOLEAN",		SYM(BOOLEAN_SYM),0,0},
  { "BOTH",		SYM(BOTH),0,0},
  { "BTREE",		SYM(BTREE_SYM),0,0},
  { "BY",		SYM(BY),0,0},
  { "BYTE",		SYM(BYTE_SYM), 0, 0},
  { "CACHE",		SYM(CACHE_SYM),0,0},
  { "CASCADE",		SYM(CASCADE),0,0},
  { "CASE",		SYM(CASE_SYM),0,0},
  { "CHAR",		SYM(CHAR_SYM),0,0},
  { "CHARACTER",	SYM(CHAR_SYM),0,0},
  { "CHARSET",		SYM(CHARSET),0,0},
  { "CHANGE",		SYM(CHANGE),0,0},
  { "CHANGED",		SYM(CHANGED),0,0},
  { "CHECK",		SYM(CHECK_SYM),0,0},
  { "CHECKSUM",		SYM(CHECKSUM_SYM),0,0},
  { "CIPHER",		SYM(CIPHER_SYM),0,0},
  { "CLIENT",		SYM(CLIENT_SYM),0,0},
  { "CLOSE",		SYM(CLOSE_SYM),0,0},
  { "COLLATE",		SYM(COLLATE_SYM),0,0},
  { "COLLATION",	SYM(COLLATION_SYM),0,0},
  { "COLUMN",		SYM(COLUMN_SYM),0,0},
  { "COLUMNS",		SYM(COLUMNS),0,0},
  { "COMMENT",		SYM(COMMENT_SYM),0,0},
  { "COMMIT",		SYM(COMMIT_SYM),0,0},
  { "COMMITTED",	SYM(COMMITTED_SYM),0,0},
  { "COMPRESSED",	SYM(COMPRESSED_SYM),0,0},
  { "CONCURRENT",	SYM(CONCURRENT),0,0},
  { "CONSTRAINT",	SYM(CONSTRAINT),0,0},
  { "CREATE",		SYM(CREATE),0,0},
  { "CROSS",		SYM(CROSS),0,0},
  { "CUBE",		SYM(CUBE_SYM),0,0},
  { "CURRENT_DATE",	SYM(CURDATE),0,0},
  { "CURRENT_TIME",	SYM(CURTIME),0,0},
  { "CURRENT_TIMESTAMP", SYM(NOW_SYM),0,0},
  { "DATA",		SYM(DATA_SYM),0,0},
  { "DATABASE",		SYM(DATABASE),0,0},
  { "DATABASES",	SYM(DATABASES),0,0},
  { "DATE",		SYM(DATE_SYM),0,0},
  { "DATETIME",		SYM(DATETIME),0,0},
  { "DAY",		SYM(DAY_SYM),0,0},
  { "DAY_HOUR",		SYM(DAY_HOUR_SYM),0,0},
  { "DAY_MICROSECOND",	SYM(DAY_MICROSECOND_SYM),0,0},
  { "DAY_MINUTE",	SYM(DAY_MINUTE_SYM),0,0},
  { "DAY_SECOND",	SYM(DAY_SECOND_SYM),0,0},
  { "DEC",		SYM(DECIMAL_SYM),0,0},
  { "DECIMAL",		SYM(DECIMAL_SYM),0,0},
  { "DES_KEY_FILE",	SYM(DES_KEY_FILE),0,0},
  { "DEFAULT",		SYM(DEFAULT),0,0},
  { "DELAYED",		SYM(DELAYED_SYM),0,0},
  { "DELAY_KEY_WRITE",	SYM(DELAY_KEY_WRITE_SYM),0,0},
  { "DELETE",		SYM(DELETE_SYM),0,0},
  { "DESC",		SYM(DESC),0,0},
  { "DESCRIBE",		SYM(DESCRIBE),0,0},
  { "DIRECTORY",	SYM(DIRECTORY_SYM),0,0},
  { "DISABLE",		SYM(DISABLE_SYM),0,0},
  { "DISCARD",		SYM(DISCARD),0,0},
  { "DISTINCT",		SYM(DISTINCT),0,0},
  { "DISTINCTROW",	SYM(DISTINCT),0,0},	/* Access likes this */
  { "DIV",		SYM(DIV_SYM),0,0},
  { "DO",		SYM(DO_SYM),0,0},
  { "DOUBLE",		SYM(DOUBLE_SYM),0,0},
  { "DUAL",		SYM(DUAL_SYM),0,0},
  { "DROP",		SYM(DROP),0,0},
  { "DUMPFILE",		SYM(DUMPFILE),0,0},
  { "DYNAMIC",		SYM(DYNAMIC_SYM),0,0},
  { "DUPLICATE",	SYM(DUPLICATE_SYM),0,0},
  { "ERRORS",		SYM(ERRORS),0,0},
  { "END",		SYM(END),0,0},
  { "ELSE",             SYM(ELSE),0,0},
  { "ESCAPE",		SYM(ESCAPE_SYM),0,0},
  { "ESCAPED",		SYM(ESCAPED),0,0},
  { "ENABLE",		SYM(ENABLE_SYM),0,0},
  { "ENCLOSED",		SYM(ENCLOSED),0,0},
  { "ENUM",		SYM(ENUM),0,0},
  { "EVENTS",		SYM(EVENTS_SYM),0,0},
  { "EXECUTE",		SYM(EXECUTE_SYM),0,0},
  { "EXPLAIN",		SYM(DESCRIBE),0,0},
  { "EXISTS",		SYM(EXISTS),0,0},
  { "EXTENDED",		SYM(EXTENDED_SYM),0,0},
  { "FAST",		SYM(FAST_SYM),0,0},
  { "FIELDS",		SYM(COLUMNS),0,0},
  { "FILE",		SYM(FILE_SYM),0,0},
  { "FIRST",		SYM(FIRST_SYM),0,0},
  { "FIXED",		SYM(FIXED_SYM),0,0},
  { "FLOAT",		SYM(FLOAT_SYM),0,0},
  { "FLOAT4",		SYM(FLOAT_SYM),0,0},
  { "FLOAT8",		SYM(DOUBLE_SYM),0,0},
  { "FLUSH",		SYM(FLUSH_SYM),0,0},
  { "FALSE",		SYM(FALSE_SYM),0,0},
  { "FOREIGN",		SYM(FOREIGN),0,0},
  { "FORCE",		SYM(FORCE_SYM),0,0},
  { "RAID_TYPE",	SYM(RAID_TYPE),0,0},
  { "RAID_CHUNKS",	SYM(RAID_CHUNKS),0,0},
  { "RAID_CHUNKSIZE",	SYM(RAID_CHUNKSIZE),0,0},
  { "ROW_FORMAT",	SYM(ROW_FORMAT_SYM),0,0},
  { "FROM",		SYM(FROM),0,0},
  { "FOR",		SYM(FOR_SYM),0,0},
  { "FULL",		SYM(FULL),0,0},
  { "FULLTEXT",		SYM(FULLTEXT_SYM),0,0},
  { "FUNCTION",		SYM(UDF_SYM),0,0},
  { "GEOMETRY",		SYM(GEOMETRY_SYM),0,0},
  { "GEOMETRYCOLLECTION",SYM(GEOMETRYCOLLECTION),0,0},
  { "GLOBAL",		SYM(GLOBAL_SYM),0,0},
  { "GRANT",		SYM(GRANT),0,0},
  { "GRANTS",	        SYM(GRANTS),0,0},
  { "GROUP",		SYM(GROUP),0,0},
  { "HAVING",		SYM(HAVING),0,0},
  { "HANDLER",		SYM(HANDLER_SYM),0,0},
  { "HASH",		SYM(HASH_SYM),0,0},
  { "HEAP",		SYM(HEAP_SYM),0,0},
  { "HELP",		SYM(HELP_SYM),0,0},
  { "HIGH_PRIORITY",	SYM(HIGH_PRIORITY),0,0},
  { "HOUR",		SYM(HOUR_SYM),0,0},
  { "HOUR_MICROSECOND",	SYM(HOUR_MICROSECOND_SYM),0,0},
  { "HOUR_MINUTE",	SYM(HOUR_MINUTE_SYM),0,0},
  { "HOUR_SECOND",	SYM(HOUR_SECOND_SYM),0,0},
  { "HOSTS",		SYM(HOSTS_SYM),0,0},
  { "IDENTIFIED",	SYM(IDENTIFIED_SYM),0,0},
  { "IGNORE",		SYM(IGNORE_SYM),0,0},
  { "IN",		SYM(IN_SYM),0,0},
  { "INDEX",		SYM(INDEX),0,0},
  { "INDEXES",		SYM(INDEXES),0,0},
  { "INFILE",		SYM(INFILE),0,0},
  { "INNER",		SYM(INNER_SYM),0,0},
  { "INNOBASE",		SYM(INNOBASE_SYM),0,0},
  { "INNODB",		SYM(INNOBASE_SYM),0,0},
  { "IMPORT",		SYM(IMPORT),0,0},
  { "INSERT",		SYM(INSERT),0,0},
  { "INSERT_METHOD",    SYM(INSERT_METHOD),0,0},
  { "INT",		SYM(INT_SYM),0,0},
  { "INTEGER",		SYM(INT_SYM),0,0},
  { "INTERVAL",		SYM(INTERVAL_SYM),0,0},
  { "INT1",		SYM(TINYINT),0,0},
  { "INT2",		SYM(SMALLINT),0,0},
  { "INT3",		SYM(MEDIUMINT),0,0},
  { "INT4",		SYM(INT_SYM),0,0},
  { "INT8",		SYM(BIGINT),0,0},
  { "INTO",		SYM(INTO),0,0},
  { "IO_THREAD",        SYM(RELAY_THREAD),0,0},
  { "IF",		SYM(IF),0,0},
  { "IS",		SYM(IS),0,0},
  { "ISOLATION",	SYM(ISOLATION),0,0},
  { "ISAM",		SYM(ISAM_SYM),0,0},
  { "ISSUER",		SYM(ISSUER_SYM),0,0},
  { "JOIN",		SYM(JOIN_SYM),0,0},
  { "KEY",		SYM(KEY_SYM),0,0},
  { "KEYS",		SYM(KEYS),0,0},
  { "KILL",		SYM(KILL_SYM),0,0},
  { "LAST",		SYM(LAST_SYM),0,0},
  { "LEADING",		SYM(LEADING),0,0},
  { "LEAVES",		SYM(LEAVES),0,0},
  { "LEFT",		SYM(LEFT),0,0},
  { "LEVEL",		SYM(LEVEL_SYM),0,0},
  { "LIKE",		SYM(LIKE),0,0},
  { "LINES",		SYM(LINES),0,0},
  { "LINESTRING",	SYM(LINESTRING),0,0},
  { "LIMIT",		SYM(LIMIT),0,0},
  { "LOAD",		SYM(LOAD),0,0},
  { "LOCAL",		SYM(LOCAL_SYM),0,0},
  { "LOCALTIME",	SYM(NOW_SYM),0,0},
  { "LOCALTIMESTAMP",	SYM(NOW_SYM),0,0},
  { "LOCK",		SYM(LOCK_SYM),0,0},
  { "LOCKS",		SYM(LOCKS_SYM),0,0},
  { "LOGS",		SYM(LOGS_SYM),0,0},
  { "LONG",		SYM(LONG_SYM),0,0},
  { "LONGBLOB",		SYM(LONGBLOB),0,0},
  { "LONGTEXT",		SYM(LONGTEXT),0,0},
  { "LOW_PRIORITY",	SYM(LOW_PRIORITY),0,0},
  { "MASTER",           SYM(MASTER_SYM),0,0},
  { "MASTER_CONNECT_RETRY",           SYM(MASTER_CONNECT_RETRY_SYM),0,0},
  { "MASTER_HOST",           SYM(MASTER_HOST_SYM),0,0},
  { "MASTER_LOG_FILE",           SYM(MASTER_LOG_FILE_SYM),0,0},
  { "MASTER_LOG_POS",           SYM(MASTER_LOG_POS_SYM),0,0},
  { "MASTER_PASSWORD",           SYM(MASTER_PASSWORD_SYM),0,0},
  { "MASTER_PORT",           SYM(MASTER_PORT_SYM),0,0},
  { "MASTER_SERVER_ID",           SYM(MASTER_SERVER_ID_SYM),0,0},
  { "MASTER_SSL",       SYM(MASTER_SSL_SYM),0,0},
  { "MASTER_SSL_CA",    SYM(MASTER_SSL_CA_SYM),0,0},
  { "MASTER_SSL_CAPATH",SYM(MASTER_SSL_CAPATH_SYM),0,0},
  { "MASTER_SSL_CERT",  SYM(MASTER_SSL_CERT_SYM),0,0},
  { "MASTER_SSL_CIPHER",SYM(MASTER_SSL_CIPHER_SYM),0,0},
  { "MASTER_SSL_KEY",   SYM(MASTER_SSL_KEY_SYM),0,0},
  { "MASTER_USER",           SYM(MASTER_USER_SYM),0,0},
  { "MAX_ROWS",		SYM(MAX_ROWS),0,0},
  { "MAX_QUERIES_PER_HOUR", SYM(MAX_QUERIES_PER_HOUR), 0,0},
  { "MAX_UPDATES_PER_HOUR", SYM(MAX_UPDATES_PER_HOUR), 0,0},
  { "MAX_CONNECTIONS_PER_HOUR", SYM(MAX_CONNECTIONS_PER_HOUR), 0,0},
  { "MATCH",		SYM(MATCH),0,0},
  { "MEDIUMBLOB",	SYM(MEDIUMBLOB),0,0},
  { "MEDIUMTEXT",	SYM(MEDIUMTEXT),0,0},
  { "MEDIUMINT",	SYM(MEDIUMINT),0,0},
  { "MERGE",		SYM(MERGE_SYM),0,0},
  { "MEDIUM",		SYM(MEDIUM_SYM),0,0},
  { "MEMORY",		SYM(MEMORY_SYM),0,0},
  { "MICROSECOND",	SYM(MICROSECOND_SYM),0,0},
  { "MIDDLEINT",	SYM(MEDIUMINT),0,0},	/* For powerbuilder */
  { "MIN_ROWS",		SYM(MIN_ROWS),0,0},
  { "MINUTE",		SYM(MINUTE_SYM),0,0},
  { "MINUTE_MICROSECOND", SYM(MINUTE_MICROSECOND_SYM),0,0},
  { "MINUTE_SECOND",	SYM(MINUTE_SECOND_SYM),0,0},
  { "MOD",		SYM(MOD_SYM),0,0},
  { "MODE",		SYM(MODE_SYM),0,0},
  { "MODIFY",		SYM(MODIFY_SYM),0,0},
  { "MONTH",		SYM(MONTH_SYM),0,0},
  { "MULTILINESTRING",	SYM(MULTILINESTRING),0,0},
  { "MULTIPOINT",	SYM(MULTIPOINT),0,0},
  { "MULTIPOLYGON",	SYM(MULTIPOLYGON),0,0},
  { "MRG_MYISAM",	SYM(MERGE_SYM),0,0},
  { "MYISAM",		SYM(MYISAM_SYM),0,0},
  { "NAMES",		SYM(NAMES_SYM),0,0},
  { "NATURAL",		SYM(NATURAL),0,0},
  { "NATIONAL",		SYM(NATIONAL_SYM),0,0},
  { "NEXT",		SYM(NEXT_SYM),0,0},
  { "NEW",              SYM(NEW_SYM),0,0},
  { "NCHAR",		SYM(NCHAR_SYM),0,0},
  { "NO",		SYM(NO_SYM),0,0},
  { "NONE",		SYM(NONE_SYM),0,0},
  { "NOT",		SYM(NOT),0,0},
  { "NULL",		SYM(NULL_SYM),0,0},
  { "NUMERIC",		SYM(NUMERIC_SYM),0,0},
  { "NVARCHAR",		SYM(NVARCHAR_SYM),0,0},
  { "OFFSET",		SYM(OFFSET_SYM),0,0},
  { "OLD_PASSWORD",	SYM(OLD_PASSWORD),0,0},
  { "ON",		SYM(ON),0,0},
  { "OPEN",		SYM(OPEN_SYM),0,0},
  { "OPTIMIZE",		SYM(OPTIMIZE),0,0},
  { "OPTION",		SYM(OPTION),0,0},
  { "OPTIONALLY",	SYM(OPTIONALLY),0,0},
  { "OR",		SYM(OR),0,0},
  { "ORDER",		SYM(ORDER_SYM),0,0},
  { "OUTER",		SYM(OUTER),0,0},
  { "OUTFILE",		SYM(OUTFILE),0,0},
  { "PACK_KEYS",	SYM(PACK_KEYS_SYM),0,0},
  { "PARTIAL",		SYM(PARTIAL),0,0},
  { "PASSWORD",		SYM(PASSWORD),0,0},
  { "POINT",		SYM(POINT_SYM),0,0},
  { "POLYGON",		SYM(POLYGON),0,0},
  { "PURGE",		SYM(PURGE),0,0},
  { "PRECISION",	SYM(PRECISION),0,0},
  { "PREV",		SYM(PREV_SYM),0,0},
  { "PRIMARY",		SYM(PRIMARY_SYM),0,0},
  { "PROCEDURE",	SYM(PROCEDURE),0,0},
  { "PROCESS"	,	SYM(PROCESS),0,0},
  { "PROCESSLIST",	SYM(PROCESSLIST_SYM),0,0},
  { "PRIVILEGES",	SYM(PRIVILEGES),0,0},
  { "QUERY",		SYM(QUERY_SYM),0,0},
  { "QUICK",	        SYM(QUICK),0,0},
  { "RAID0",		SYM(RAID_0_SYM),0,0},
  { "READ",		SYM(READ_SYM),0,0},
  { "REAL",		SYM(REAL),0,0},
  { "REFERENCES",	SYM(REFERENCES),0,0},
  { "RELAY_LOG_FILE",   SYM(RELAY_LOG_FILE_SYM),0,0},
  { "RELAY_LOG_POS",    SYM(RELAY_LOG_POS_SYM),0,0},
  { "RELAY_THREAD",     SYM(RELAY_THREAD),0,0},
  { "RELOAD",		SYM(RELOAD),0,0},
  { "REGEXP",		SYM(REGEXP),0,0},
  { "RENAME",		SYM(RENAME),0,0},
  { "REPAIR",		SYM(REPAIR),0,0},
  { "REPLACE",		SYM(REPLACE),0,0},
  { "REPLICATION",	SYM(REPLICATION),0,0},
  { "REPEATABLE",	SYM(REPEATABLE_SYM),0,0},
  { "REQUIRE",	        SYM(REQUIRE_SYM),0,0},
  { "RESET",		SYM(RESET_SYM),0,0},
  { "USER_RESOURCES",	SYM(RESOURCES),0,0},
  { "RESTORE",		SYM(RESTORE_SYM),0,0},
  { "RESTRICT",		SYM(RESTRICT),0,0},
  { "RETURNS",		SYM(UDF_RETURNS_SYM),0,0},
  { "REVOKE",		SYM(REVOKE),0,0},
  { "RIGHT",		SYM(RIGHT),0,0},
  { "RLIKE",		SYM(REGEXP),0,0},	/* Like in mSQL2 */
  { "ROLLBACK",		SYM(ROLLBACK_SYM),0,0},
  { "ROLLUP",		SYM(ROLLUP_SYM),0,0},
  { "ROW",		SYM(ROW_SYM),0,0},
  { "ROWS",		SYM(ROWS_SYM),0,0},
  { "RTREE",		SYM(RTREE_SYM),0,0},
  { "SAVEPOINT",	SYM(SAVEPOINT_SYM),0,0},
  { "SECOND",		SYM(SECOND_SYM),0,0},
  { "SECOND_MICROSECOND", SYM(SECOND_MICROSECOND_SYM),0,0},
  { "SEPARATOR",	SYM(SEPARATOR_SYM),0,0},
  { "SELECT",		SYM(SELECT_SYM),0,0},
  { "SERIAL",		SYM(SERIAL_SYM),0,0},
  { "SERIALIZABLE",	SYM(SERIALIZABLE_SYM),0,0},
  { "SESSION",		SYM(SESSION_SYM),0,0},
  { "SET",		SYM(SET),0,0},
  { "SIGNED",		SYM(SIGNED_SYM),0,0},
  { "SIMPLE",		SYM(SIMPLE_SYM),0,0},
  { "SHARE",		SYM(SHARE_SYM),0,0},
  { "SHOW",		SYM(SHOW),0,0},
  { "SHUTDOWN",		SYM(SHUTDOWN),0,0},
  { "SLAVE",            SYM(SLAVE),0,0},
  { "SMALLINT",		SYM(SMALLINT),0,0},
  { "SOME",             SYM(ANY_SYM),0,0},
  { "SONAME",		SYM(UDF_SONAME_SYM),0,0},
  { "SPATIAL",		SYM(SPATIAL_SYM),0,0},
  { "SQL_BIG_RESULT",	SYM(SQL_BIG_RESULT),0,0},
  { "SQL_BUFFER_RESULT", SYM(SQL_BUFFER_RESULT),0,0},
  { "SQL_CACHE",        SYM(SQL_CACHE_SYM), 0, 0},
  { "SQL_CALC_FOUND_ROWS", SYM(SQL_CALC_FOUND_ROWS),0,0},
  { "SQL_NO_CACHE",	SYM(SQL_NO_CACHE_SYM), 0, 0},
  { "SQL_SMALL_RESULT", SYM(SQL_SMALL_RESULT),0,0},
  { "SQL_THREAD",	SYM(SQL_THREAD),0,0},
  { "SOUNDS",		SYM(SOUNDS_SYM),0,0},
  { "SSL",		SYM(SSL_SYM),0,0},
  { "STRAIGHT_JOIN",	SYM(STRAIGHT_JOIN),0,0},
  { "START",		SYM(START_SYM),0,0},
  { "STARTING",		SYM(STARTING),0,0},
  { "STATUS",		SYM(STATUS_SYM),0,0},
  { "STRING",		SYM(STRING_SYM),0,0},
  { "STOP",		SYM(STOP_SYM),0,0},
  { "STRIPED",		SYM(RAID_STRIPED_SYM),0,0},
  { "SUBJECT",		SYM(SUBJECT_SYM),0,0},
  { "SUPER",		SYM(SUPER_SYM),0,0},
  { "TABLE",		SYM(TABLE_SYM),0,0},
  { "TABLES",		SYM(TABLES),0,0},
  { "TABLESPACE",	SYM(TABLESPACE),0,0},
  { "TEMPORARY",	SYM(TEMPORARY),0,0},
  { "TERMINATED",	SYM(TERMINATED),0,0},
  { "TEXT",		SYM(TEXT_SYM),0,0},
  { "THEN",		SYM(THEN_SYM),0,0},
  { "TIME",		SYM(TIME_SYM),0,0},
  { "TIMESTAMP",	SYM(TIMESTAMP),0,0},
  { "TINYBLOB",		SYM(TINYBLOB),0,0},
  { "TINYTEXT",		SYM(TINYTEXT),0,0},
  { "TINYINT",		SYM(TINYINT),0,0},
  { "TRAILING",		SYM(TRAILING),0,0},
  { "TRANSACTION",	SYM(TRANSACTION_SYM),0,0},
  { "TRUNCATE",		SYM(TRUNCATE_SYM),0,0},
  { "TRUE",		SYM(TRUE_SYM),0,0},
  { "TO",		SYM(TO_SYM),0,0},
  { "TYPE",		SYM(TYPE_SYM),0,0},
  { "TYPES",		SYM(TYPES_SYM),0,0},
  { "UNCOMMITTED",	SYM(UNCOMMITTED_SYM),0,0},
  { "UNICODE",	        SYM(UNICODE_SYM),0,0},
  { "UNION",	        SYM(UNION_SYM),0,0},
  { "UNIQUE",		SYM(UNIQUE_SYM),0,0},
  { "UNLOCK",		SYM(UNLOCK_SYM),0,0},
  { "UNSIGNED",		SYM(UNSIGNED),0,0},
  { "USE",		SYM(USE_SYM),0,0},
  { "USE_FRM",		SYM(USE_FRM),0,0},
  { "USER",		SYM(USER),0,0},
  { "UNTIL",		SYM(UNTIL_SYM),0,0},
  { "USING",		SYM(USING),0,0},
  { "UTC_DATE",         SYM(UTC_DATE_SYM),0,0},
  { "UTC_TIME",         SYM(UTC_TIME_SYM),0,0},
  { "UTC_TIMESTAMP",    SYM(UTC_TIMESTAMP_SYM),0,0},
  { "UPDATE",		SYM(UPDATE_SYM),0,0},
  { "USAGE",		SYM(USAGE),0,0},
  { "VALUE",		SYM(VALUE_SYM),0,0},
  { "VALUES",		SYM(VALUES),0,0},
  { "VARCHAR",		SYM(VARCHAR),0,0},
  { "VARCHARACTER",	SYM(VARCHAR),0,0},
  { "VARIABLES",	SYM(VARIABLES),0,0},
  { "VARYING",		SYM(VARYING),0,0},
  { "VARBINARY",	SYM(VARBINARY),0,0},
  { "WARNINGS",		SYM(WARNINGS),0,0},
  { "WITH",		SYM(WITH),0,0},
  { "WORK",		SYM(WORK_SYM),0,0},
  { "WRITE",		SYM(WRITE_SYM),0,0},
  { "WHEN",		SYM(WHEN_SYM),0,0},
  { "WHERE",		SYM(WHERE),0,0},
  { "NO_WRITE_TO_BINLOG",  SYM(NO_WRITE_TO_BINLOG),0,0},
  { "XOR",		SYM(XOR),0,0},
  { "X509",		SYM(X509_SYM),0,0},
  { "YEAR",		SYM(YEAR_SYM),0,0},
  { "YEAR_MONTH",	SYM(YEAR_MONTH_SYM),0,0},
  { "ZEROFILL",		SYM(ZEROFILL),0,0},
  { "||",		SYM(OR_OR_CONCAT),0,0}
};


static SYMBOL sql_functions[] = {
  { "ABS",		SYM(FUNC_ARG1),0,CREATE_FUNC(create_func_abs)},
  { "ACOS",		SYM(FUNC_ARG1),0,CREATE_FUNC(create_func_acos)},
  { "ADDDATE",		SYM(ADDDATE_SYM),0,0},
  { "ADDTIME",		SYM(FUNC_ARG2),0,CREATE_FUNC(create_func_addtime)},
  { "AES_ENCRYPT",      SYM(FUNC_ARG2),0,CREATE_FUNC(create_func_aes_encrypt)},
  { "AES_DECRYPT",      SYM(FUNC_ARG2),0,CREATE_FUNC(create_func_aes_decrypt)},
  { "AREA",		SYM(FUNC_ARG1),0,CREATE_FUNC(create_func_area)},
  { "ASIN",		SYM(FUNC_ARG1),0,CREATE_FUNC(create_func_asin)},
  { "ASBINARY",		SYM(FUNC_ARG1),0,CREATE_FUNC(create_func_as_wkb)},
  { "ASTEXT",		SYM(FUNC_ARG1),0,CREATE_FUNC(create_func_as_wkt)},
  { "ASWKB",		SYM(FUNC_ARG1),0,CREATE_FUNC(create_func_as_wkb)},
  { "ASWKT",		SYM(FUNC_ARG1),0,CREATE_FUNC(create_func_as_wkt)},
  { "ATAN",		SYM(ATAN),0,0},
  { "ATAN2",		SYM(ATAN),0,0},
  { "BENCHMARK",	SYM(BENCHMARK_SYM),0,0},
  { "BIN",		SYM(FUNC_ARG1),0,CREATE_FUNC(create_func_bin)},
  { "BIT_COUNT",	SYM(FUNC_ARG1),0,CREATE_FUNC(create_func_bit_count)},
  { "BIT_OR",		SYM(BIT_OR),0,0},
  { "BIT_AND",		SYM(BIT_AND),0,0},
  { "CAST",		SYM(CAST_SYM),0,0},
  { "CEIL",		SYM(FUNC_ARG1),0,CREATE_FUNC(create_func_ceiling)},
  { "CEILING",		SYM(FUNC_ARG1),0,CREATE_FUNC(create_func_ceiling)},
  { "CURRENT_USER",	SYM(FUNC_ARG0),0,CREATE_FUNC(create_func_current_user)},
  { "BIT_LENGTH",	SYM(FUNC_ARG1),0,CREATE_FUNC(create_func_bit_length)},
  { "CENTROID",		SYM(FUNC_ARG1),0,CREATE_FUNC(create_func_centroid)},
  { "CHAR_LENGTH",	SYM(FUNC_ARG1),0,CREATE_FUNC(create_func_char_length)},
  { "CHARACTER_LENGTH", SYM(FUNC_ARG1),0,CREATE_FUNC(create_func_char_length)},
  { "COALESCE",		SYM(COALESCE),0,0},
  { "COERCIBILITY",	SYM(FUNC_ARG1),0,CREATE_FUNC(create_func_coercibility)},
  { "COMPRESS",		SYM(FUNC_ARG1),0,CREATE_FUNC(create_func_compress)},
  { "CONCAT",		SYM(CONCAT),0,0},
  { "CONCAT_WS",	SYM(CONCAT_WS),0,0},
  { "CONNECTION_ID",	SYM(FUNC_ARG0),0,CREATE_FUNC(create_func_connection_id)},
  { "CONTAINS",		SYM(FUNC_ARG2),0,CREATE_FUNC(create_func_contains)},
  { "CONV",		SYM(FUNC_ARG3),0,CREATE_FUNC(create_func_conv)},
  { "CONVERT",		SYM(CONVERT_SYM),0,0},
  { "COUNT",		SYM(COUNT_SYM),0,0},
  { "COS",		SYM(FUNC_ARG1),0,CREATE_FUNC(create_func_cos)},
  { "COT",		SYM(FUNC_ARG1),0,CREATE_FUNC(create_func_cot)},
  { "CRC32",		SYM(FUNC_ARG1),0,CREATE_FUNC(create_func_crc32)},
  { "CROSSES",		SYM(FUNC_ARG2),0,CREATE_FUNC(create_func_crosses)},
  { "CURDATE",		SYM(CURDATE),0,0},
  { "CURTIME",		SYM(CURTIME),0,0},
  { "DATE_ADD",		SYM(DATE_ADD_INTERVAL),0,0},
  { "DATEDIFF",		SYM(FUNC_ARG2),0,CREATE_FUNC(create_func_datediff)},
  { "DATE_FORMAT",	SYM(FUNC_ARG2),0,CREATE_FUNC(create_func_date_format)},
  { "DATE_SUB",		SYM(DATE_SUB_INTERVAL),0,0},
  { "DAYNAME",		SYM(FUNC_ARG1),0,CREATE_FUNC(create_func_dayname)},
  { "DAYOFMONTH",	SYM(FUNC_ARG1),0,CREATE_FUNC(create_func_dayofmonth)},
  { "DAYOFWEEK",	SYM(FUNC_ARG1),0,CREATE_FUNC(create_func_dayofweek)},
  { "DAYOFYEAR",	SYM(FUNC_ARG1),0,CREATE_FUNC(create_func_dayofyear)},
  { "DECODE",		SYM(DECODE_SYM),0,0},
  { "DEGREES",		SYM(FUNC_ARG1),0,CREATE_FUNC(create_func_degrees)},
  { "DES_ENCRYPT",	SYM(DES_ENCRYPT_SYM),0,0},
  { "DES_DECRYPT",	SYM(DES_DECRYPT_SYM),0,0},
  { "DIMENSION",	SYM(FUNC_ARG1),0,CREATE_FUNC(create_func_dimension)},
  { "DISJOINT",		SYM(FUNC_ARG2),0,CREATE_FUNC(create_func_disjoint)},
  { "ELT",		SYM(ELT_FUNC),0,0},
  { "ENCODE",		SYM(ENCODE_SYM),0,0},
  { "ENCRYPT",		SYM(ENCRYPT),0,0},
  { "ENDPOINT",		SYM(FUNC_ARG1),0,CREATE_FUNC(create_func_endpoint)},
  { "ENVELOPE",		SYM(FUNC_ARG1),0,CREATE_FUNC(create_func_envelope)},
  { "EQUALS",		SYM(FUNC_ARG2),0,CREATE_FUNC(create_func_equals)},
  { "EXTERIORRING",	SYM(FUNC_ARG1),0,CREATE_FUNC(create_func_exteriorring)},
  { "EXTRACT",		SYM(EXTRACT_SYM),0,0},
  { "EXP",		SYM(FUNC_ARG1),0,CREATE_FUNC(create_func_exp)},
  { "EXPORT_SET",	SYM(EXPORT_SET),0,0},
  { "FIELD",		SYM(FIELD_FUNC),0,0},	/* For compability */
  { "FIND_IN_SET",	SYM(FUNC_ARG2),0,CREATE_FUNC(create_func_find_in_set)},
  { "FLOOR",		SYM(FUNC_ARG1),0,CREATE_FUNC(create_func_floor)},
  { "FORMAT",		SYM(FORMAT_SYM),0,0},
  { "FOUND_ROWS",	SYM(FUNC_ARG0),0,CREATE_FUNC(create_func_found_rows)},
  { "FROM_DAYS",	SYM(FUNC_ARG1),0,CREATE_FUNC(create_func_from_days)},
  { "FROM_UNIXTIME",	SYM(FROM_UNIXTIME),0,0},
  { "GET_LOCK",		SYM(FUNC_ARG2),0,CREATE_FUNC(create_func_get_lock)},
  { "GEOMETRYN",	SYM(FUNC_ARG2),0,CREATE_FUNC(create_func_geometryn)},
  { "GEOMETRYTYPE",	SYM(FUNC_ARG1),0,CREATE_FUNC(create_func_geometry_type)},
  { "GEOMCOLLFROMTEXT",	SYM(GEOMCOLLFROMTEXT),0,0},
  { "GEOMCOLLFROMWKB",	SYM(GEOMFROMWKB),0,0},
  { "GEOMETRYCOLLECTIONFROMTEXT",SYM(GEOMCOLLFROMTEXT),0,0},
  { "GEOMETRYCOLLECTIONFROMWKB",SYM(GEOMFROMWKB),0,0},
  { "GEOMETRYFROMTEXT", SYM(GEOMFROMTEXT),0,0},
  { "GEOMETRYFROMWKB",	SYM(GEOMFROMWKB),0,0},
  { "GEOMFROMTEXT",	SYM(GEOMFROMTEXT),0,0},
  { "GEOMFROMWKB",	SYM(GEOMFROMWKB),0,0},
    { "GLENGTH",		SYM(FUNC_ARG1),0,CREATE_FUNC(create_func_glength)},
  { "GREATEST",		SYM(GREATEST_SYM),0,0},
  { "GROUP_CONCAT",	SYM(GROUP_CONCAT_SYM),0,0},
  { "GROUP_UNIQUE_USERS",	SYM(GROUP_UNIQUE_USERS),0,0},
  { "HEX",		SYM(FUNC_ARG1),0,CREATE_FUNC(create_func_hex)},
  { "IFNULL",		SYM(FUNC_ARG2),0,CREATE_FUNC(create_func_ifnull)},
  { "INET_ATON",	SYM(FUNC_ARG1),0,CREATE_FUNC(create_func_inet_aton)},
  { "INET_NTOA",	SYM(FUNC_ARG1),0,CREATE_FUNC(create_func_inet_ntoa)},
  { "INSTR",		SYM(FUNC_ARG2),0,CREATE_FUNC(create_func_instr)},
  { "INTERIORRINGN",	SYM(FUNC_ARG2),0,CREATE_FUNC(create_func_interiorringn)},
  { "INTERSECTS",	SYM(FUNC_ARG2),0,CREATE_FUNC(create_func_intersects)},
  { "ISCLOSED",		SYM(FUNC_ARG1),0,CREATE_FUNC(create_func_isclosed)},
  { "ISEMPTY",		SYM(FUNC_ARG1),0,CREATE_FUNC(create_func_isempty)},
  { "ISNULL",		SYM(FUNC_ARG1),0,CREATE_FUNC(create_func_isnull)},
  { "IS_FREE_LOCK",	SYM(FUNC_ARG1),0,CREATE_FUNC(create_func_is_free_lock)},
  { "IS_USED_LOCK",	SYM(FUNC_ARG1),0,CREATE_FUNC(create_func_is_used_lock)},
  { "LAST_INSERT_ID",	SYM(LAST_INSERT_ID),0,0},
  { "ISSIMPLE",		SYM(FUNC_ARG1),0,CREATE_FUNC(create_func_issimple)},
  { "LCASE",		SYM(FUNC_ARG1),0,CREATE_FUNC(create_func_lcase)},
  { "LEAST",		SYM(LEAST_SYM),0,0},
  { "LENGTH",		SYM(FUNC_ARG1),0,CREATE_FUNC(create_func_length)},
  { "LN",		SYM(FUNC_ARG1),0,CREATE_FUNC(create_func_ln)},
  { "LINEFROMTEXT",	SYM(LINEFROMTEXT),0,0},
  { "LINEFROMWKB",	SYM(GEOMFROMWKB),0,0},
  { "LINESTRINGFROMTEXT",SYM(LINEFROMTEXT),0,0},
  { "LINESTRINGFROMWKB",SYM(GEOMFROMWKB),0,0},
  { "LOAD_FILE",	SYM(FUNC_ARG1),0,CREATE_FUNC(create_load_file)},
  { "LOCATE",		SYM(LOCATE),0,0},
  { "LOG",		SYM(LOG_SYM),0,0},
  { "LOG2",		SYM(FUNC_ARG1),0,CREATE_FUNC(create_func_log2)},
  { "LOG10",		SYM(FUNC_ARG1),0,CREATE_FUNC(create_func_log10)},
  { "LOWER",		SYM(FUNC_ARG1),0,CREATE_FUNC(create_func_lcase)},
  { "LPAD",		SYM(FUNC_ARG3),0,CREATE_FUNC(create_func_lpad)},
  { "LTRIM",		SYM(FUNC_ARG1),0,CREATE_FUNC(create_func_ltrim)},
  { "MAKE_SET",		SYM(MAKE_SET_SYM),0,0},
  { "MAKEDATE",		SYM(FUNC_ARG2),0,CREATE_FUNC(create_func_makedate)},
  { "MAKETIME",		SYM(FUNC_ARG3),0,CREATE_FUNC(create_func_maketime)},
  { "MASTER_POS_WAIT",	SYM(MASTER_POS_WAIT),0,0},
  { "MAX",		SYM(MAX_SYM),0,0},
  { "MBRCONTAINS",	SYM(FUNC_ARG2),0,CREATE_FUNC(create_func_contains)},
  { "MBRDISJOINT",	SYM(FUNC_ARG2),0,CREATE_FUNC(create_func_disjoint)},
  { "MBREQUAL",		SYM(FUNC_ARG2),0,CREATE_FUNC(create_func_equals)},
  { "MBRINTERSECTS",	SYM(FUNC_ARG2),0,CREATE_FUNC(create_func_intersects)},
  { "MBROVERLAPS",	SYM(FUNC_ARG2),0,CREATE_FUNC(create_func_overlaps)},
  { "MBRTOUCHES",	SYM(FUNC_ARG2),0,CREATE_FUNC(create_func_touches)},
  { "MBRWITHIN",	SYM(FUNC_ARG2),0,CREATE_FUNC(create_func_within)},
  { "MD5",              SYM(FUNC_ARG1),0,CREATE_FUNC(create_func_md5)},
  { "MID",		SYM(SUBSTRING),0,0},	/* unireg function */
  { "MIN",		SYM(MIN_SYM),0,0},
  { "MLINEFROMTEXT",	SYM(MLINEFROMTEXT),0,0},
  { "MLINEFROMWKB",	SYM(GEOMFROMWKB),0,0},
  { "MPOINTFROMTEXT",	SYM(MPOINTFROMTEXT),0,0},
  { "MPOINTFROMWKB",	SYM(GEOMFROMWKB),0,0},
  { "MPOLYFROMTEXT",	SYM(MPOLYFROMTEXT),0,0},
  { "MPOLYFROMWKB",	SYM(GEOMFROMWKB),0,0},
  { "MONTHNAME",	SYM(FUNC_ARG1),0,CREATE_FUNC(create_func_monthname)},
  { "MULTILINESTRINGFROMTEXT",SYM(MLINEFROMTEXT),0,0},
  { "MULTILINESTRINGFROMWKB",SYM(GEOMFROMWKB),0,0},
  { "MULTIPOINTFROMTEXT",SYM(MPOINTFROMTEXT),0,0},
  { "MULTIPOINTFROMWKB",SYM(GEOMFROMWKB),0,0},
  { "MULTIPOLYGONFROMTEXT",SYM(MPOLYFROMTEXT),0,0},
  { "MULTIPOLYGONFROMWKB",SYM(GEOMFROMWKB),0,0},
  { "NOW",		SYM(NOW_SYM),0,0},
  { "NULLIF",		SYM(FUNC_ARG2),0,CREATE_FUNC(create_func_nullif)},
  { "NUMGEOMETRIES",	SYM(FUNC_ARG1),0,CREATE_FUNC(create_func_numgeometries)},
  { "NUMINTERIORRINGS",	SYM(FUNC_ARG1),0,CREATE_FUNC(create_func_numinteriorring)},
  { "NUMPOINTS",	SYM(FUNC_ARG1),0,CREATE_FUNC(create_func_numpoints)},
  { "OCTET_LENGTH",	SYM(FUNC_ARG1),0,CREATE_FUNC(create_func_length)},
  { "OCT",		SYM(FUNC_ARG1),0,CREATE_FUNC(create_func_oct)},
  { "ORD",              SYM(FUNC_ARG1),0,CREATE_FUNC(create_func_ord)},
  { "OVERLAPS",		SYM(FUNC_ARG2),0,CREATE_FUNC(create_func_overlaps)},
  { "PERIOD_ADD",	SYM(FUNC_ARG2),0,CREATE_FUNC(create_func_period_add)},
  { "PERIOD_DIFF",	SYM(FUNC_ARG2),0,CREATE_FUNC(create_func_period_diff)},
  { "PI",		SYM(FUNC_ARG0),0,CREATE_FUNC(create_func_pi)},
  { "POINTFROMTEXT",	SYM(POINTFROMTEXT),0,0},
  { "POINTFROMWKB",	SYM(GEOMFROMWKB),0,0},
  { "POINTN",		SYM(FUNC_ARG2),0,CREATE_FUNC(create_func_pointn)},
  { "POLYFROMTEXT",	SYM(POLYFROMTEXT),0,0},
  { "POLYFROMWKB",	SYM(GEOMFROMWKB),0,0},
  { "POLYGONFROMTEXT",	SYM(POLYFROMTEXT),0,0},
  { "POLYGONFROMWKB",	SYM(GEOMFROMWKB),0,0},
  { "POSITION",		SYM(POSITION_SYM),0,0},
  { "POW",		SYM(FUNC_ARG2),0,CREATE_FUNC(create_func_pow)},
  { "POWER",		SYM(FUNC_ARG2),0,CREATE_FUNC(create_func_pow)},
  { "QUARTER",		SYM(FUNC_ARG1),0,CREATE_FUNC(create_func_quarter)},
  { "QUOTE",		SYM(FUNC_ARG1),0,CREATE_FUNC(create_func_quote)},
  { "RADIANS",		SYM(FUNC_ARG1),0,CREATE_FUNC(create_func_radians)},
  { "RAND",		SYM(RAND),0,0},
  { "RELEASE_LOCK",	SYM(FUNC_ARG1),0,CREATE_FUNC(create_func_release_lock)},
  { "REPEAT",		SYM(FUNC_ARG2),0,CREATE_FUNC(create_func_repeat)},
  { "REVERSE",		SYM(FUNC_ARG1),0,CREATE_FUNC(create_func_reverse)},
  { "ROUND",		SYM(ROUND),0,0},
  { "RPAD",		SYM(FUNC_ARG3),0,CREATE_FUNC(create_func_rpad)},
  { "RTRIM",		SYM(FUNC_ARG1),0,CREATE_FUNC(create_func_rtrim)},
  { "SEC_TO_TIME",	SYM(FUNC_ARG1),0,CREATE_FUNC(create_func_sec_to_time)},
  { "SESSION_USER",	SYM(USER),0,0},
  { "SUBDATE",		SYM(SUBDATE_SYM),0,0},
  { "SIGN",		SYM(FUNC_ARG1),0,CREATE_FUNC(create_func_sign)},
  { "SIN",		SYM(FUNC_ARG1),0,CREATE_FUNC(create_func_sin)},
  { "SHA",              SYM(FUNC_ARG1),0,CREATE_FUNC(create_func_sha)},
  { "SHA1",             SYM(FUNC_ARG1),0,CREATE_FUNC(create_func_sha)},
  { "SOUNDEX",		SYM(FUNC_ARG1),0,CREATE_FUNC(create_func_soundex)},
  { "SPACE",		SYM(FUNC_ARG1),0,CREATE_FUNC(create_func_space)},
  { "SQRT",		SYM(FUNC_ARG1),0,CREATE_FUNC(create_func_sqrt)},
  { "SRID",		SYM(FUNC_ARG1),0,CREATE_FUNC(create_func_srid)},
  { "STARTPOINT",	SYM(FUNC_ARG1),0,CREATE_FUNC(create_func_startpoint)},
  { "STD",		SYM(STD_SYM),0,0},
  { "STDDEV",		SYM(STD_SYM),0,0},
  { "STRCMP",		SYM(FUNC_ARG2),0,CREATE_FUNC(create_func_strcmp)},
  { "SUBSTR",	  	SYM(SUBSTRING),0,0},
  { "SUBSTRING",	SYM(SUBSTRING),0,0},
  { "SUBSTRING_INDEX",	SYM(SUBSTRING_INDEX),0,0},
  { "SUBTIME",          SYM(FUNC_ARG2),0,CREATE_FUNC(create_func_subtime)},
  { "SUM",		SYM(SUM_SYM),0,0},
  { "SYSDATE",		SYM(NOW_SYM),0,0},
  { "SYSTEM_USER",	SYM(USER),0,0},
  { "TAN",		SYM(FUNC_ARG1),0,CREATE_FUNC(create_func_tan)},
  { "TIME_FORMAT",	SYM(FUNC_ARG2),0,CREATE_FUNC(create_func_time_format)},
  { "TIME_TO_SEC",	SYM(FUNC_ARG1),0,CREATE_FUNC(create_func_time_to_sec)},
  { "TIMEDIFF",         SYM(FUNC_ARG2),0,CREATE_FUNC(create_func_timediff)},
  { "TO_DAYS",		SYM(FUNC_ARG1),0,CREATE_FUNC(create_func_to_days)},
  { "TOUCHES",		SYM(FUNC_ARG2),0,CREATE_FUNC(create_func_touches)},
  { "TRIM",		SYM(TRIM),0,0},
  { "UCASE",		SYM(FUNC_ARG1),0,CREATE_FUNC(create_func_ucase)},
  { "UNCOMPRESS",	SYM(FUNC_ARG1),0,CREATE_FUNC(create_func_uncompress)},
  { "UNCOMPRESSED_LENGTH", SYM(FUNC_ARG1),0,CREATE_FUNC(create_func_uncompressed_length)},
  { "UNIQUE_USERS",	SYM(UNIQUE_USERS),0,0},
  { "UNIX_TIMESTAMP",	SYM(UNIX_TIMESTAMP),0,0},
  { "UPPER",		SYM(FUNC_ARG1),0,CREATE_FUNC(create_func_ucase)},
  { "VARIANCE",		SYM(VARIANCE_SYM),0,0},
  { "VERSION",		SYM(FUNC_ARG0),0,CREATE_FUNC(create_func_version)},
  { "WEEK",		SYM(WEEK_SYM),0,0},
  { "WEEKDAY",		SYM(FUNC_ARG1),0,CREATE_FUNC(create_func_weekday)},
  { "WEEKOFYEAR",	SYM(FUNC_ARG1),0,CREATE_FUNC(create_func_weekofyear)},
  { "WITHIN",		SYM(FUNC_ARG2),0,CREATE_FUNC(create_func_within)},
  { "X",		SYM(FUNC_ARG1),0,CREATE_FUNC(create_func_x)},
  { "Y",		SYM(FUNC_ARG1),0,CREATE_FUNC(create_func_y)},
  { "YEARWEEK",		SYM(YEARWEEK),0,0}
};
