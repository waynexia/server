/***********************************************************************/
/*  ODBC catalog function prototypes.                                  */
/***********************************************************************/
#if defined(PROMPT_OK)
char   *ODBCCheckConnection(PGLOBAL g, char *dsn, int cop);
#endif   // PROMPT_OK
PQRYRES ODBCDataSources(PGLOBAL g, bool info);
PQRYRES ODBCColumns(PGLOBAL g, char *dsn, char *table,
                                          char *colpat, bool info);
PQRYRES ODBCSrcCols(PGLOBAL g, char *dsn, char *src);
PQRYRES ODBCTables(PGLOBAL g, char *dsn, char *tabpat, bool info);
PQRYRES ODBCDrivers(PGLOBAL g, bool info);
