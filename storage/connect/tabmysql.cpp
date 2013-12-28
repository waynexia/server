/************* TabMySQL C++ Program Source Code File (.CPP) *************/
/* PROGRAM NAME: TABMYSQL                                               */
/* -------------                                                        */
/*  Version 1.7                                                         */
/*                                                                      */
/* AUTHOR:                                                              */
/* -------                                                              */
/*  Olivier BERTRAND                                      2007-2013     */
/*                                                                      */
/* WHAT THIS PROGRAM DOES:                                              */
/* -----------------------                                              */
/*  Implements a table type that are MySQL tables.                      */
/*  It can optionally use the embedded MySQL library.                   */
/*                                                                      */
/* WHAT YOU NEED TO COMPILE THIS PROGRAM:                               */
/* --------------------------------------                               */
/*                                                                      */
/*  REQUIRED FILES:                                                     */
/*  ---------------                                                     */
/*    TABMYSQL.CPP   - Source code                                      */
/*    PLGDBSEM.H     - DB application declaration file                  */
/*    TABMYSQL.H     - TABODBC classes declaration file                 */
/*    GLOBAL.H       - Global declaration file                          */
/*                                                                      */
/*  REQUIRED LIBRARIES:                                                 */
/*  -------------------                                                 */
/*    Large model C library                                             */
/*                                                                      */
/*  REQUIRED PROGRAMS:                                                  */
/*  ------------------                                                  */
/*    IBM, Borland, GNU or Microsoft C++ Compiler and Linker            */
/*                                                                      */
/************************************************************************/
#define MYSQL_SERVER 1
#include "my_global.h"
#include "sql_class.h"
#include "sql_servers.h"
#if defined(WIN32)
//#include <windows.h>
#else   // !WIN32
//#include <fnmatch.h>
//#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "osutil.h"
//#include <io.h>
//#include <fcntl.h>
#endif  // !WIN32

/***********************************************************************/
/*  Include application header files:                                  */
/***********************************************************************/
#include "global.h"
#include "plgdbsem.h"
#include "xtable.h"
#include "tabcol.h"
#include "colblk.h"
#include "mycat.h"
#include "reldef.h"
#include "tabmysql.h"
#include "valblk.h"
#include "tabutil.h"

#if defined(_CONSOLE)
void PrintResult(PGLOBAL, PSEM, PQRYRES);
#endif   // _CONSOLE

extern "C" int   trace;

/* -------------- Implementation of the MYSQLDEF class --------------- */

/***********************************************************************/
/*  Constructor.                                                       */
/***********************************************************************/
MYSQLDEF::MYSQLDEF(void)
  {
  Pseudo = 2;                            // SERVID is Ok but not ROWID
  Hostname = NULL;
  Database = NULL;
  Tabname = NULL;
  Srcdef = NULL;
  Username = NULL;
  Password = NULL;
  Portnumber = 0;
  Isview = FALSE;
  Bind = FALSE;
  Delayed = FALSE;
  Xsrc = FALSE;
  } // end of MYSQLDEF constructor

/***********************************************************************/
/*  Get connection info from the declared server.                      */
/***********************************************************************/
bool MYSQLDEF::GetServerInfo(PGLOBAL g, const char *server_name)
{
  THD      *thd= current_thd;
  MEM_ROOT *mem= thd->mem_root;
  FOREIGN_SERVER *server, server_buffer;
  DBUG_ENTER("GetServerInfo");
  DBUG_PRINT("info", ("server_name %s", server_name));

  if (!server_name || !strlen(server_name)) {
    DBUG_PRINT("info", ("server_name not defined!"));
    strcpy(g->Message, "server_name not defined!");
    DBUG_RETURN(true);
    } // endif server_name

  // get_server_by_name() clones the server if exists and allocates
	// copies of strings in the supplied mem_root
  if (!(server= get_server_by_name(mem, server_name, &server_buffer))) {
    DBUG_PRINT("info", ("get_server_by_name returned > 0 error condition!"));
    /* need to come up with error handling */
    strcpy(g->Message, "get_server_by_name returned > 0 error condition!");
    DBUG_RETURN(true);
    } // endif server

  DBUG_PRINT("info", ("get_server_by_name returned server at %lx",
                      (long unsigned int) server));

  // TODO: We need to examine which of these can really be NULL
  Hostname = PlugDup(g, server->host);
  Database = PlugDup(g, server->db);
  Username = PlugDup(g, server->username);
  Password = PlugDup(g, server->password);
  Portnumber = (server->port) ? server->port : GetDefaultPort();

  DBUG_RETURN(false);
} // end of GetServerInfo

/***********************************************************************/
/* Parse connection string                                             */
/*                                                                     */
/* SYNOPSIS                                                            */
/*   ParseURL()                                                        */
/*   url                 The connection string to parse                */
/*                                                                     */
/* DESCRIPTION                                                         */
/*   Populates the table with information about the connection         */
/*   to the foreign database that will serve as the data source.       */
/*   This string must be specified (currently) in the "CONNECTION"     */
/*   field, listed in the CREATE TABLE statement.                      */
/*                                                                     */
/*   This string MUST be in the format of any of these:                */
/*                                                                     */
/*   CONNECTION="scheme://user:pwd@host:port/database/table"           */
/*   CONNECTION="scheme://user@host/database/table"                    */
/*   CONNECTION="scheme://user@host:port/database/table"               */
/*   CONNECTION="scheme://user:pwd@host/database/table"                */
/*                                                                     */
/*   _OR_                                                              */
/*                                                                     */
/*   CONNECTION="connection name" (NIY)                                */
/*                                                                     */
/* An Example:                                                         */
/*                                                                     */
/* CREATE TABLE t1 (id int(32))                                        */
/*   ENGINE="CONNECT" TABLE_TYPE="MYSQL"                               */
/*   CONNECTION="mysql://joe:pwd@192.168.1.111:9308/dbname/tabname";   */
/*                                                                     */
/* CREATE TABLE t2 (                                                   */
/*   id int(4) NOT NULL auto_increment,                                */
/*   name varchar(32) NOT NULL,                                        */
/*   PRIMARY KEY(id)                                                   */
/*   ) ENGINE="CONNECT" TABLE_TYPE="MYSQL"                             */
/*   CONNECTION="my_conn";    (NIY)                                    */
/*                                                                     */
/*  'password' and 'port' are both optional.                           */
/*                                                                     */
/* RETURN VALUE                                                        */
/*   false       success                                               */
/*   true        error                                                 */
/*                                                                     */
/***********************************************************************/
bool MYSQLDEF::ParseURL(PGLOBAL g, char *url, bool b)
  {
  if ((!strstr(url, "://") && (!strchr(url, '@')))) {
    // No :// or @ in connection string. Must be a straight
    // connection name of either "server" or "server/table"
    // ok, so we do a little parsing, but not completely!
    if ((Tabname= strchr(url, '/'))) {
      // If there is a single '/' in the connection string, 
      // this means the user is specifying a table name
      *Tabname++= '\0';

      // there better not be any more '/'s !
      if (strchr(Tabname, '/'))
        return true;

    } else
      // Otherwise, straight server name, 
      // use tablename of federatedx table as remote table name
      Tabname= Name;

    if (trace)
      htrc("server: %s  Tabname: %s", url, Tabname);

    Server = url;
    return GetServerInfo(g, url);
  } else {
    // URL, parse it
    char *sport, *scheme = url;

    if (!(Username = strstr(url, "://"))) {
      strcpy(g->Message, "Connection is not an URL");
      return true;
      } // endif User

    scheme[Username - scheme] = 0;

    if (stricmp(scheme, "mysql")) {
      strcpy(g->Message, "scheme must be mysql");
      return true;
      } // endif scheme

    Username += 3;

    if (!(Hostname = strchr(Username, '@'))) {
      strcpy(g->Message, "No host specified in URL");
      return true;
    } else {
      *Hostname++ = 0;                   // End Username
      Server = Hostname;
    } // endif Hostname

    if ((Password = strchr(Username, ':'))) {
      *Password++ = 0;                   // End username

      // Make sure there isn't an extra / or @
      if ((strchr(Password, '/') || strchr(Hostname, '@'))) {
        strcpy(g->Message, "Syntax error in URL");
        return true;
        } // endif

      // Found that if the string is:
      // user:@hostname:port/db/table
      // Then password is a null string, so set to NULL
      if ((Password[0] == 0))
        Password = NULL;

      } // endif password

    // Make sure there isn't an extra / or @ */
    if ((strchr(Username, '/')) || (strchr(Hostname, '@'))) {
      strcpy(g->Message, "Syntax error in URL");
      return true;
      } // endif

    if ((Database = strchr(Hostname, '/'))) {
      *Database++ = 0;

      if ((Tabname = strchr(Database, '/'))) {
        *Tabname++ = 0;

        // Make sure there's not an extra /
        if ((strchr(Tabname, '/'))) {
          strcpy(g->Message, "Syntax error in URL");
          return true;
          } // endif /

        } // endif Tabname
        
      } // endif database

    if ((sport = strchr(Hostname, ':')))
      *sport++ = 0;

    // For unspecified values, get the values of old style options
    // but only if called from MYSQLDEF, else set them to NULL
    Portnumber = (sport && sport[0]) ? atoi(sport) 
               : (b) ? Cat->GetIntCatInfo("Port", GetDefaultPort()) : 0;

    if (Username[0] == 0)
      Username = (b) ? Cat->GetStringCatInfo(g, "User", "*") : NULL;

    if (Hostname[0] == 0)
      Hostname = (b) ? Cat->GetStringCatInfo(g, "Host", "localhost") : NULL;

    if (!Database || !*Database)
      Database = (b) ? Cat->GetStringCatInfo(g, "Database", "*") : NULL;

    if (!Tabname || !*Tabname)
      Tabname = (b) ? Cat->GetStringCatInfo(g, "Tabname", Name) : NULL;

    if (!Password)
      Password = (b) ? Cat->GetStringCatInfo(g, "Password", NULL) : NULL;
    } // endif URL

#if 0
  if (!share->port)
    if (!share->hostname || strcmp(share->hostname, my_localhost) == 0)
      share->socket= (char *) MYSQL_UNIX_ADDR;
    else
      share->port= MYSQL_PORT;
#endif // 0

  return false;
  } // end of ParseURL

/***********************************************************************/
/*  DefineAM: define specific AM block values from XCV file.           */
/***********************************************************************/
bool MYSQLDEF::DefineAM(PGLOBAL g, LPCSTR am, int poff)
  {
  char *url;

  Desc = "MySQL Table";

  if (stricmp(am, "MYPRX")) {
    // Normal case of specific MYSQL table
    url = Cat->GetStringCatInfo(g, "Connect", NULL);

    if (!url || !*url) { 
      // Not using the connection URL
      Hostname = Cat->GetStringCatInfo(g, "Host", "localhost");
      Database = Cat->GetStringCatInfo(g, "Database", "*");
      Tabname = Cat->GetStringCatInfo(g, "Name", Name); // Deprecated
      Tabname = Cat->GetStringCatInfo(g, "Tabname", Tabname);
      Username = Cat->GetStringCatInfo(g, "User", "*");
      Password = Cat->GetStringCatInfo(g, "Password", NULL);
      Portnumber = Cat->GetIntCatInfo("Port", GetDefaultPort());
      Server = Hostname;
    } else if (ParseURL(g, url))
      return true;

    Bind = !!Cat->GetIntCatInfo("Bind", 0);
    Delayed = !!Cat->GetIntCatInfo("Delayed", 0);
  } else {
    // MYSQL access from a PROXY table 
    Database = Cat->GetStringCatInfo(g, "Database", "*");
    Isview = Cat->GetBoolCatInfo("View", FALSE);

    // We must get other connection parms from the calling table
    Remove_tshp(Cat);
    url = Cat->GetStringCatInfo(g, "Connect", NULL);

    if (!url || !*url) { 
      Hostname = Cat->GetStringCatInfo(g, "Host", "localhost");
      Username = Cat->GetStringCatInfo(g, "User", "*");
      Password = Cat->GetStringCatInfo(g, "Password", NULL);
      Portnumber = Cat->GetIntCatInfo("Port", GetDefaultPort());
      Server = Hostname;
    } else {
      char *locdb = Database;

      if (ParseURL(g, url))
        return true;

      Database = locdb;
    } // endif url

    Tabname = Name;
  } // endif am

  if ((Srcdef = Cat->GetStringCatInfo(g, "Srcdef", NULL)))
    Isview = true;

  // Used for Update and Delete
  Qrystr = Cat->GetStringCatInfo(g, "Query_String", "?");
  Quoted = Cat->GetIntCatInfo("Quoted", 0);

  // Specific for command executing tables
  Xsrc = Cat->GetBoolCatInfo("Execsrc", false);
  Mxr = Cat->GetIntCatInfo("Maxerr", 0);
  return FALSE;
  } // end of DefineAM

/***********************************************************************/
/*  GetTable: makes a new TDB of the proper type.                      */
/***********************************************************************/
PTDB MYSQLDEF::GetTable(PGLOBAL g, MODE m)
  {
  if (Xsrc)
    return new(g) TDBMYEXC(this);
  else if (Catfunc == FNC_COL)
    return new(g) TDBMCL(this);
  else
    return new(g) TDBMYSQL(this);

  } // end of GetTable

/* ------------------------------------------------------------------- */

/***********************************************************************/
/*  Implementation of the TDBMYSQL class.                              */
/***********************************************************************/
TDBMYSQL::TDBMYSQL(PMYDEF tdp) : TDBASE(tdp)
  {
  if (tdp) {
    Host = tdp->Hostname;
    Database = tdp->Database;
    Tabname = tdp->Tabname;
    Srcdef = tdp->Srcdef;
    User = tdp->Username;
    Pwd = tdp->Password;
    Server = tdp->Server;
    Qrystr = tdp->Qrystr;
    Quoted = max(0, tdp->Quoted);
    Port = tdp->Portnumber;
    Isview = tdp->Isview;
    Prep = tdp->Bind;
    Delayed = tdp->Delayed;
  } else {
    Host = NULL;
    Database = NULL;
    Tabname = NULL;
    Srcdef = NULL;
    User = NULL;
    Pwd = NULL;
    Server = NULL;
    Qrystr = NULL;
    Quoted = 0;
    Port = 0;
    Isview = FALSE;
    Prep = FALSE;
    Delayed = FALSE;
  } // endif tdp

  Bind = NULL;
  Query = NULL;
  Qbuf = NULL;
  Fetched = FALSE;
  m_Rc = RC_FX;
  AftRows = 0;
  N = -1;
  Nparm = 0;
  } // end of TDBMYSQL constructor

TDBMYSQL::TDBMYSQL(PGLOBAL g, PTDBMY tdbp) : TDBASE(tdbp)
  {
  Host = tdbp->Host;
  Database = tdbp->Database;
  Tabname = tdbp->Tabname;
  Srcdef = tdbp->Srcdef;
  User = tdbp->User;
  Pwd =  tdbp->Pwd; 
  Qrystr = tdbp->Qrystr;
  Quoted = tdbp->Quoted;
  Port = tdbp->Port;
  Isview = tdbp->Isview;
  Prep = tdbp->Prep;
  Delayed = tdbp->Delayed;
  Bind = NULL;
  Query = tdbp->Query;
  Qbuf = NULL;
  Fetched = tdbp->Fetched;
  m_Rc = tdbp->m_Rc;
  AftRows = tdbp->AftRows;
  N = tdbp->N;
  Nparm = tdbp->Nparm;
  } // end of TDBMYSQL copy constructor

// Is this really useful ???
PTDB TDBMYSQL::CopyOne(PTABS t)
  {
  PTDB    tp;
  PCOL    cp1, cp2;
  PGLOBAL g = t->G;

  tp = new(g) TDBMYSQL(g, this);

  for (cp1 = Columns; cp1; cp1 = cp1->GetNext()) {
    cp2 = new(g) MYSQLCOL((PMYCOL)cp1, tp);

    NewPointer(t, cp1, cp2);
    } // endfor cp1

  return tp;
  } // end of CopyOne

/***********************************************************************/
/*  Allocate MYSQL column description block.                           */
/***********************************************************************/
PCOL TDBMYSQL::MakeCol(PGLOBAL g, PCOLDEF cdp, PCOL cprec, int n)
  {
  return new(g) MYSQLCOL(cdp, this, cprec, n);
  } // end of MakeCol

/***********************************************************************/
/*  MakeSelect: make the Select statement use with MySQL connection.   */
/*  Note: when implementing EOM filtering, column only used in local   */
/*  filter should be removed from column list.                         */
/***********************************************************************/
bool TDBMYSQL::MakeSelect(PGLOBAL g)
  {
  char   *tk = "`";
  int     rank = 0;
  bool    b = FALSE;
  PCOL    colp;
//PDBUSER dup = PlgGetUser(g);

  if (Query)
    return FALSE;        // already done

  if (Srcdef) {
    Query = Srcdef;
    return false;
    } // endif Srcdef

  //Find the address of the suballocated query
  Query = (char*)PlugSubAlloc(g, NULL, 0);
  strcpy(Query, "SELECT ");

  if (Columns) {
    for (colp = Columns; colp; colp = colp->GetNext())
      if (!colp->IsSpecial()) {
//      if (colp->IsSpecial()) {
//        strcpy(g->Message, MSG(NO_SPEC_COL));
//        return TRUE;
//      } else {
        if (b)
          strcat(Query, ", ");
        else
          b = TRUE;

        strcat(strcat(strcat(Query, tk), colp->GetName()), tk);
        ((PMYCOL)colp)->Rank = rank++;
      } // endif colp

  } else {
    // ncol == 0 can occur for views or queries such as
    // Query count(*) from... for which we will count the rows from
    // Query '*' from...
    // (the use of a char constant minimize the result storage)
    strcat(Query, (Isview) ? "*" : "'*'");
  } // endif ncol

  strcat(strcat(strcat(strcat(Query, " FROM "), tk), Tabname), tk);

  if (To_Filter)
    strcat(strcat(Query, " WHERE "), To_Filter->Body);

  if (trace)
    htrc("Query=%s\n", Query);

  // Now we know how much to suballocate
  PlugSubAlloc(g, NULL, strlen(Query) + 1);
  return FALSE;
  } // end of MakeSelect

/***********************************************************************/
/*  MakeInsert: make the Insert statement used with MySQL connection.  */
/***********************************************************************/
bool TDBMYSQL::MakeInsert(PGLOBAL g)
  {
  char *colist, *valist = NULL;
  char *tk = "`";
  int   len = 0, qlen = 0;
  bool  b = FALSE;
  PCOL  colp;

  if (Query)
    return FALSE;        // already done

  for (colp = Columns; colp; colp = colp->GetNext())
    if (!colp->IsSpecial()) {
//    if (colp->IsSpecial()) {
//      strcpy(g->Message, MSG(NO_SPEC_COL));
//      return TRUE;
//    } else {
      len += (strlen(colp->GetName()) + 4);
      ((PMYCOL)colp)->Rank = Nparm++;
    } // endif colp

  colist = (char*)PlugSubAlloc(g, NULL, len);
  *colist = '\0';

  if (Prep) {
#if defined(MYSQL_PREPARED_STATEMENTS)
    valist = (char*)PlugSubAlloc(g, NULL, 2 * Nparm);
    *valist = '\0';
#else   // !MYSQL_PREPARED_STATEMENTS
    strcpy(g->Message, "Prepared statements not used (not supported)");
    PushWarning(g, this);
    Prep = FALSE;
#endif  // !MYSQL_PREPARED_STATEMENTS 
    } // endif Prep

  for (colp = Columns; colp; colp = colp->GetNext()) {
    if (b) {
      strcat(colist, ", ");
      if (Prep) strcat(valist, ",");
    } else
      b = TRUE;

    strcat(strcat(strcat(colist, tk), colp->GetName()), tk);

    // Parameter marker
    if (!Prep) {
      if (colp->GetResultType() == TYPE_DATE)
        qlen += 20;
      else
        qlen += colp->GetLength();

    } // endif Prep

    if (Prep)
      strcat(valist, "?");

    } // endfor colp

  // Below 40 is enough to contain the fixed part of the query
  len = (strlen(Tabname) + strlen(colist)
                         + ((Prep) ? strlen(valist) : 0) + 40);
  Query = (char*)PlugSubAlloc(g, NULL, len);

  if (Delayed)
    strcpy(Query, "INSERT DELAYED INTO ");
  else
    strcpy(Query, "INSERT INTO ");

  strcat(strcat(strcat(Query, tk), Tabname), tk);
  strcat(strcat(strcat(Query, " ("), colist), ") VALUES (");

  if (Prep)
    strcat(strcat(Query, valist), ")");
  else {
    qlen += (strlen(Query) + Nparm);
    Qbuf = (char *)PlugSubAlloc(g, NULL, qlen);
    } // endelse Prep

  return FALSE;
  } // end of MakeInsert

/***********************************************************************/
/*  MakeCommand: make the Update or Delete statement to send to the    */
/*  MySQL server. Limited to remote values and filtering.              */
/***********************************************************************/
int TDBMYSQL::MakeCommand(PGLOBAL g)
  {
  Query = (char*)PlugSubAlloc(g, NULL, strlen(Qrystr) + 64);

  if (Quoted > 0 || stricmp(Name, Tabname)) {
    char *p, *qrystr, name[68];
    bool  qtd = Quoted > 0;


    // Make a lower case copy of the originale query
    qrystr = (char*)PlugSubAlloc(g, NULL, strlen(Qrystr) + 1);
    strlwr(strcpy(qrystr, Qrystr));

    // Check whether the table name is equal to a keyword
    // If so, it must be quoted in the original query
    strlwr(strcat(strcat(strcpy(name, "`"), Name), "`"));

    if (!strstr("`update`delete`low_priority`ignore`quick`from`", name))
      strlwr(strcpy(name, Name));     // Not a keyword

    if ((p = strstr(qrystr, name))) {
      memcpy(Query, Qrystr, p - qrystr);
      Query[p - qrystr] = 0;

      if (qtd && *(p-1) == ' ')
        strcat(strcat(strcat(Query, "`"), Tabname), "`");
      else
        strcat(Query, Tabname);

      strcat(Query, Qrystr + (p - qrystr) + strlen(name));
    } else {
      sprintf(g->Message, "Cannot use this %s command",
                   (Mode == MODE_UPDATE) ? "UPDATE" : "DELETE");
      return RC_FX;
    } // endif p

  } else
    strcpy(Query, Qrystr);

  return RC_OK;
  } // end of MakeCommand

#if 0
/***********************************************************************/
/*  MakeUpdate: make the Update statement use with MySQL connection.   */
/*  Limited to remote values and filtering.                            */
/***********************************************************************/
int TDBMYSQL::MakeUpdate(PGLOBAL g)
  {
  char *qc, cmd[8], tab[96], end[1024];

  Query = (char*)PlugSubAlloc(g, NULL, strlen(Qrystr) + 64);
  memset(end, 0, sizeof(end));

  if (sscanf(Qrystr, "%s `%[^`]`%1023c", cmd, tab, end) > 2 ||
      sscanf(Qrystr, "%s \"%[^\"]\"%1023c", cmd, tab, end) > 2)
    qc = "`";
  else if (sscanf(Qrystr, "%s %s%1023c", cmd, tab, end) > 2
                  && !stricmp(tab, Name))
    qc = (Quoted) ? "`" : "";
  else {
    strcpy(g->Message, "Cannot use this UPDATE command");
    return RC_FX;
  } // endif sscanf

  assert(!stricmp(cmd, "update"));
  strcat(strcat(strcat(strcpy(Query, "UPDATE "), qc), Tabname), qc);
  strcat(Query, end);
  return RC_OK;
  } // end of MakeUpdate

/***********************************************************************/
/*  MakeDelete: make the Delete statement used with MySQL connection.  */
/*  Limited to remote filtering.                                       */
/***********************************************************************/
int TDBMYSQL::MakeDelete(PGLOBAL g)
  {
  char *qc, cmd[8], from[8], tab[96], end[512];

  Query = (char*)PlugSubAlloc(g, NULL, strlen(Qrystr) + 64);
  memset(end, 0, sizeof(end));

  if (sscanf(Qrystr, "%s %s `%[^`]`%511c", cmd, from, tab, end) > 2 ||
      sscanf(Qrystr, "%s %s \"%[^\"]\"%511c", cmd, from, tab, end) > 2)
    qc = "`";
  else if (sscanf(Qrystr, "%s %s %s%511c", cmd, from, tab, end) > 2)
    qc = (Quoted) ? "`" : "";
  else {
    strcpy(g->Message, "Cannot use this DELETE command");
    return RC_FX;
  } // endif sscanf

  assert(!stricmp(cmd, "delete") && !stricmp(from, "from"));
  strcat(strcat(strcat(strcpy(Query, "DELETE FROM "), qc), Tabname), qc);

  if (*end)
    strcat(Query, end);

  return RC_OK;
  } // end of MakeDelete
#endif // 0

/***********************************************************************/
/*  XCV GetMaxSize: returns the maximum number of rows in the table.   */
/***********************************************************************/
int TDBMYSQL::GetMaxSize(PGLOBAL g)
  {
  if (MaxSize < 0) {
#if 0
    if (MakeSelect(g))
      return -2;

    if (!Myc.Connected()) {
      if (Myc.Open(g, Host, Database, User, Pwd, Port))
        return -1;

      } // endif connected

    if ((MaxSize = Myc.GetResultSize(g, Query)) < 0) {
      Myc.Close();
      return -3;
      } // endif MaxSize

    // FIXME: Columns should be known when Info calls GetMaxSize
    if (!Columns)
      Query = NULL;     // Must be remade when columns are known
#endif // 0

    // Return 0 in mode DELETE in case of delete all.
    MaxSize = (Mode == MODE_DELETE) ? 0 : 10;   // To make MySQL happy
    } // endif MaxSize

  return MaxSize;
  } // end of GetMaxSize

/***********************************************************************/
/*  This a fake routine as ROWID does not exist in MySQL.              */
/***********************************************************************/
int TDBMYSQL::RowNumber(PGLOBAL g, bool b)
  {
  return N;
  } // end of RowNumber

/***********************************************************************/
/*  Return 0 in mode UPDATE to tell that the update is done.           */
/***********************************************************************/
int TDBMYSQL::GetProgMax(PGLOBAL g)
  {
  return (Mode == MODE_UPDATE) ? 0 : GetMaxSize(g);
  } // end of GetProgMax

/***********************************************************************/
/*  MySQL Bind Parameter function.                                     */
/***********************************************************************/
int TDBMYSQL::BindColumns(PGLOBAL g)
  {
#if defined(MYSQL_PREPARED_STATEMENTS)
  if (Prep) {
    Bind = (MYSQL_BIND*)PlugSubAlloc(g, NULL, Nparm * sizeof(MYSQL_BIND));

    for (PMYCOL colp = (PMYCOL)Columns; colp; colp = (PMYCOL)colp->Next)
      colp->InitBind(g);

    return Myc.BindParams(g, Bind);
    } // endif prep
#endif   // MYSQL_PREPARED_STATEMENTS

  return RC_OK;
  } // end of BindColumns

/***********************************************************************/
/*  MySQL Access Method opening routine.                               */
/***********************************************************************/
bool TDBMYSQL::OpenDB(PGLOBAL g)
  {
  if (Use == USE_OPEN) {
    /*******************************************************************/
    /*  Table already open, just replace it at its beginning.          */
    /*******************************************************************/
    Myc.Rewind();
    return false;
    } // endif use

  /*********************************************************************/
  /*  Open a MySQL connection for this table.                          */
  /*  Note: this may not be the proper way to do. Perhaps it is better */
  /*  to test whether a connection is already open for this server     */
  /*  and if so to allocate just a new result set. But this only for   */
  /*  servers allowing concurency in getting results ???               */
  /*********************************************************************/
  if (!Myc.Connected()) {
    if (Myc.Open(g, Host, Database, User, Pwd, Port))
      return true;

    } // endif Connected

  /*********************************************************************/
  /*  Take care of DATE columns.                                       */
  /*********************************************************************/
  for (PMYCOL colp = (PMYCOL)Columns; colp; colp = (PMYCOL)colp->Next)
    if (colp->Buf_Type == TYPE_DATE)
      // Format must match DATETIME MySQL type
      ((DTVAL*)colp->GetValue())->SetFormat(g, "YYYY-MM-DD hh:mm:ss", 19);

  /*********************************************************************/
  /*  Allocate whatever is used for getting results.                   */
  /*********************************************************************/
  if (Mode == MODE_READ) {
    if (!MakeSelect(g))
      m_Rc = Myc.ExecSQL(g, Query);

#if 0
    if (!Myc.m_Res || !Myc.m_Fields) {
      sprintf(g->Message, "%s result", (Myc.m_Res) ? "Void" : "No");
      Myc.Close();
      return true;
      } // endif m_Res
#endif // 0

    if (!m_Rc && Srcdef)
      if (SetColumnRanks(g))
        return true;

  } else if (Mode == MODE_INSERT) {
    if (Srcdef) {
      strcpy(g->Message, "No insert into anonym views");
      return true;
      } // endif Srcdef

    if (!MakeInsert(g)) {
#if defined(MYSQL_PREPARED_STATEMENTS)
      int n = (Prep) ? Myc.PrepareSQL(g, Query) : Nparm;

      if (Nparm != n) {
        if (n >= 0)          // Other errors return negative values
          strcpy(g->Message, MSG(BAD_PARM_COUNT));

      } else
#endif   // MYSQL_PREPARED_STATEMENTS
        m_Rc = BindColumns(g);

      } // endif MakeInsert

    if (m_Rc != RC_FX) {
      char cmd[64];
      int  w;

      sprintf(cmd, "ALTER TABLE `%s` DISABLE KEYS", Tabname);
      m_Rc = Myc.ExecSQL(g, cmd, &w);
      } // endif m_Rc

  } else
//  m_Rc = (Mode == MODE_DELETE) ? MakeDelete(g) : MakeUpdate(g);
    m_Rc = MakeCommand(g);

  if (m_Rc == RC_FX) {
    Myc.Close();
    return true;
    } // endif rc

  Use = USE_OPEN;
  return false;
  } // end of OpenDB

/***********************************************************************/
/*  Set the rank of columns in the result set.                         */
/***********************************************************************/
bool TDBMYSQL::SetColumnRanks(PGLOBAL g)
  {
  for (PCOL colp = Columns; colp; colp = colp->GetNext())
    if (((PMYCOL)colp)->FindRank(g))
      return TRUE;

  return FALSE;
  } // end of SetColumnRanks

/***********************************************************************/
/*  Called by Parent table to make the columns of a View.              */
/***********************************************************************/
PCOL TDBMYSQL::MakeFieldColumn(PGLOBAL g, char *name)
  {
  int          n;
  MYSQL_FIELD *fld;
  PCOL         cp, colp = NULL;

  for (n = 0; n < Myc.m_Fields; n++) {
    fld = &Myc.m_Res->fields[n];

    if (!stricmp(name, fld->name)) {
      colp = new(g) MYSQLCOL(fld, this, n);

      if (colp->InitValue(g))
        return NULL;

      if (!Columns)
        Columns = colp;
      else for (cp = Columns; cp; cp = cp->GetNext())
        if (!cp->GetNext()) {
          cp->SetNext(colp);
          break;
          } // endif Next

      break;
      } // endif name

    } // endfor n

  if (!colp)
    sprintf(g->Message, "Column %s is not in view", name);

  return colp;
  } // end of MakeFieldColumn

/***********************************************************************/
/*  Called by Pivot tables to find default column names in a View      */
/*  as the name of last field not equal to the passed name.            */
/***********************************************************************/
char *TDBMYSQL::FindFieldColumn(char *name)
  {
  int          n;
  MYSQL_FIELD *fld;
  char        *cp = NULL;

  for (n = Myc.m_Fields - 1; n >= 0; n--) {
    fld = &Myc.m_Res->fields[n];

    if (!name || stricmp(name, fld->name)) {
      cp = fld->name;
      break;
      } // endif name

    } // endfor n

  return cp;
  } // end of FindFieldColumn

/***********************************************************************/
/*  Send an UPDATE or DELETE command to the remote server.             */
/***********************************************************************/
int TDBMYSQL::SendCommand(PGLOBAL g)
  {
  int w;

  if (Myc.ExecSQLcmd(g, Query, &w) == RC_NF) {
    AftRows = Myc.m_Afrw;
    sprintf(g->Message, "%s: %d affected rows", Tabname, AftRows);
    PushWarning(g, this, 0);    // 0 means a Note

    if (trace)
      htrc("%s\n", g->Message);

    if (w && Myc.ExecSQL(g, "SHOW WARNINGS") == RC_OK) {
      // We got warnings from the remote server
      while (Myc.Fetch(g, -1) == RC_OK) {
        sprintf(g->Message, "%s: (%s) %s", Tabname,
                Myc.GetCharField(1), Myc.GetCharField(2));
        PushWarning(g, this);
        } // endwhile Fetch

      Myc.FreeResult();
      } // endif w

    return RC_EF;               // Nothing else to do
  } else
    return RC_FX;               // Error

  } // end of SendCommand

/***********************************************************************/
/*  Data Base read routine for MYSQL access method.                    */
/***********************************************************************/
int TDBMYSQL::ReadDB(PGLOBAL g)
  {
  int rc;

  if (trace > 1)
    htrc("MySQL ReadDB: R%d Mode=%d key=%p link=%p Kindex=%p\n",
          GetTdb_No(), Mode, To_Key_Col, To_Link, To_Kindex);

  if (Mode == MODE_UPDATE || Mode == MODE_DELETE)
    return SendCommand(g);

  /*********************************************************************/
  /*  Now start the reading process.                                   */
  /*  Here is the place to fetch the line.                             */
  /*********************************************************************/
  N++;
  Fetched = ((rc = Myc.Fetch(g, -1)) == RC_OK);

  if (trace > 1)
    htrc(" Read: rc=%d\n", rc);

  return rc;
  } // end of ReadDB

/***********************************************************************/
/*  WriteDB: Data Base write routine for MYSQL access methods.         */
/***********************************************************************/
int TDBMYSQL::WriteDB(PGLOBAL g)
  {
#if defined(MYSQL_PREPARED_STATEMENTS)
  if (Prep)
    return Myc.ExecStmt(g);
#endif   // MYSQL_PREPARED_STATEMENTS

  // Statement was not prepared, we must construct and execute
  // an insert query for each line to insert
  int  rc;
  char buf[32];

  strcpy(Qbuf, Query);

  // Make the Insert command value list
  for (PCOL colp = Columns; colp; colp = colp->GetNext()) {
    if (!colp->GetValue()->IsNull()) {
      if (colp->GetResultType() == TYPE_STRING || 
          colp->GetResultType() == TYPE_DATE)
        strcat(Qbuf, "'");

      strcat(Qbuf, colp->GetValue()->GetCharString(buf));

      if (colp->GetResultType() == TYPE_STRING || 
          colp->GetResultType() == TYPE_DATE)
        strcat(Qbuf, "'");

    } else
      strcat(Qbuf, "NULL");

    strcat(Qbuf, (colp->GetNext()) ? "," : ")");
    } // endfor colp

  Myc.m_Rows = -1;      // To execute the query
  rc = Myc.ExecSQL(g, Qbuf);
  return (rc == RC_NF) ? RC_OK : rc;      // RC_NF is Ok
  } // end of WriteDB

/***********************************************************************/
/*  Data Base delete all routine for MYSQL access methods.             */
/***********************************************************************/
int TDBMYSQL::DeleteDB(PGLOBAL g, int irc)
  {
  if (irc == RC_FX)
    // Send the DELETE (all) command to the remote table
    return (SendCommand(g) == RC_FX) ? RC_FX : RC_OK;              
  else
    return RC_OK;                 // Ignore

  } // end of DeleteDB

/***********************************************************************/
/*  Data Base close routine for MySQL access method.                   */
/***********************************************************************/
void TDBMYSQL::CloseDB(PGLOBAL g)
  {
  if (Myc.Connected()) {
    if (Mode == MODE_INSERT) {
      char cmd[64];
      int  w;
      PDBUSER dup = PlgGetUser(g);

      dup->Step = "Enabling indexes";
      sprintf(cmd, "ALTER TABLE `%s` ENABLE KEYS", Tabname);
      Myc.m_Rows = -1;      // To execute the query
      m_Rc = Myc.ExecSQL(g, cmd, &w);
      } // endif m_Rc

    Myc.Close();
    } // endif Myc

  if (trace)
    htrc("MySQL CloseDB: closing %s rc=%d\n", Name, m_Rc);

  } // end of CloseDB

// ------------------------ MYSQLCOL functions --------------------------

/***********************************************************************/
/*  MYSQLCOL public constructor.                                       */
/***********************************************************************/
MYSQLCOL::MYSQLCOL(PCOLDEF cdp, PTDB tdbp, PCOL cprec, int i, PSZ am)
        : COLBLK(cdp, tdbp, i)
  {
  if (cprec) {
    Next = cprec->GetNext();
    cprec->SetNext(this);
  } else {
    Next = tdbp->GetColumns();
    tdbp->SetColumns(this);
  } // endif cprec

  // Set additional MySQL access method information for column.
  Precision = Long = cdp->GetLong();
  Bind = NULL;
  To_Val = NULL;
  Slen = 0;
  Rank = -1;            // Not known yet

  if (trace)
    htrc(" making new %sCOL C%d %s at %p\n", am, Index, Name, this);

  } // end of MYSQLCOL constructor

/***********************************************************************/
/*  MYSQLCOL public constructor.                                       */
/***********************************************************************/
MYSQLCOL::MYSQLCOL(MYSQL_FIELD *fld, PTDB tdbp, int i, PSZ am)
        : COLBLK(NULL, tdbp, i)
  {
  Name = fld->name;
  Opt = 0;
  Precision = Long = fld->length;
  Buf_Type = MYSQLtoPLG(fld->type);
  strcpy(Format.Type, GetFormatType(Buf_Type));
  Format.Length = Long;
  Format.Prec = fld->decimals;
  ColUse = U_P;
  Nullable = !IS_NOT_NULL(fld->flags);

  if (Buf_Type == TYPE_DECIM)
    Precision = ((Field_new_decimal*)fld)->precision;

  // Set additional MySQL access method information for column.
  Bind = NULL;
  To_Val = NULL;
  Slen = 0;
  Rank = i;

  if (trace)
    htrc(" making new %sCOL C%d %s at %p\n", am, Index, Name, this);

  } // end of MYSQLCOL constructor

/***********************************************************************/
/*  MYSQLCOL constructor used for copying columns.                     */
/*  tdbp is the pointer to the new table descriptor.                   */
/***********************************************************************/
MYSQLCOL::MYSQLCOL(MYSQLCOL *col1, PTDB tdbp) : COLBLK(col1, tdbp)
  {
  Long = col1->Long;
  Bind = NULL;
  To_Val = NULL;
  Slen = col1->Slen;
  Rank = col1->Rank;
  } // end of MYSQLCOL copy constructor

/***********************************************************************/
/*  FindRank: Find the rank of this column in the result set.          */
/***********************************************************************/
bool MYSQLCOL::FindRank(PGLOBAL g)
{
  int    n;
  MYSQLC myc = ((PTDBMY)To_Tdb)->Myc;

  for (n = 0; n < myc.m_Fields; n++)
    if (!stricmp(Name, myc.m_Res->fields[n].name)) {
      Rank = n;
      return false;
      } // endif Name

  sprintf(g->Message, "Column %s not in result set", Name);
  return true;
} // end of FindRank

/***********************************************************************/
/*  SetBuffer: prepare a column block for write operation.             */
/***********************************************************************/
bool MYSQLCOL::SetBuffer(PGLOBAL g, PVAL value, bool ok, bool check)
  {
  if (!(To_Val = value)) {
    sprintf(g->Message, MSG(VALUE_ERROR), Name);
    return TRUE;
  } else if (Buf_Type == value->GetType()) {
    // Values are of the (good) column type
    if (Buf_Type == TYPE_DATE) {
      // If any of the date values is formatted
      // output format must be set for the receiving table
      if (GetDomain() || ((DTVAL *)value)->IsFormatted())
        goto newval;          // This will make a new value;

    } else if (Buf_Type == TYPE_DOUBLE)
      // Float values must be written with the correct (column) precision
      // Note: maybe this should be forced by ShowValue instead of this ?
      value->SetPrec(GetScale());

    Value = value;            // Directly access the external value
  } else {
    // Values are not of the (good) column type
    if (check) {
      sprintf(g->Message, MSG(TYPE_VALUE_ERR), Name,
              GetTypeName(Buf_Type), GetTypeName(value->GetType()));
      return TRUE;
      } // endif check

 newval:
    if (InitValue(g))         // Allocate the matching value block
      return TRUE;

  } // endif's Value, Buf_Type

  // Because Colblk's have been made from a copy of the original TDB in
  // case of Update, we must reset them to point to the original one.
  if (To_Tdb->GetOrig())
    To_Tdb = (PTDB)To_Tdb->GetOrig();

  // Set the Column
  Status = (ok) ? BUF_EMPTY : BUF_NO;
  return FALSE;
  } // end of SetBuffer

/***********************************************************************/
/*  InitBind: Initialize the bind structure according to type.         */
/***********************************************************************/
void MYSQLCOL::InitBind(PGLOBAL g)
  {
  PTDBMY tdbp = (PTDBMY)To_Tdb;

  assert(tdbp->Bind && Rank < tdbp->Nparm);

  Bind = &tdbp->Bind[Rank];
  memset(Bind, 0, sizeof(MYSQL_BIND));

  if (Buf_Type == TYPE_DATE) {
    Bind->buffer_type = PLGtoMYSQL(TYPE_STRING, false);
    Bind->buffer = (char *)PlugSubAlloc(g,NULL, 20);
    Bind->buffer_length = 20;
    Bind->length = &Slen;
  } else {
    Bind->buffer_type = PLGtoMYSQL(Buf_Type, false);
    Bind->buffer = (char *)Value->GetTo_Val();
    Bind->buffer_length = Value->GetClen();
    Bind->length = (IsTypeChar(Buf_Type)) ? &Slen : NULL;
  } // endif Buf_Type

  } // end of InitBind

/***********************************************************************/
/*  ReadColumn:                                                        */
/***********************************************************************/
void MYSQLCOL::ReadColumn(PGLOBAL g)
  {
  char  *p, *buf, tim[20];
  int    rc;
  PTDBMY tdbp = (PTDBMY)To_Tdb;

  /*********************************************************************/
  /*  If physical fetching of the line was deferred, do it now.        */
  /*********************************************************************/
  if (!tdbp->Fetched)
    if ((rc = tdbp->Myc.Fetch(g, tdbp->N)) != RC_OK) {
      if (rc == RC_EF)
        sprintf(g->Message, MSG(INV_DEF_READ), rc);

      longjmp(g->jumper[g->jump_level], 11);
    } else
      tdbp->Fetched = TRUE;

  if ((buf = ((PTDBMY)To_Tdb)->Myc.GetCharField(Rank))) {
    if (trace)
      htrc("MySQL ReadColumn: name=%s buf=%s\n", Name, buf);

    // TODO: have a true way to differenciate temporal values
    if (Buf_Type == TYPE_DATE && strlen(buf) == 8)
      // This is a TIME value
      p = strcat(strcpy(tim, "1970-01-01 "), buf);
    else
      p = buf;

    if (Value->SetValue_char(p, strlen(p))) {
      sprintf(g->Message, "Out of range value for column %s at row %d",
              Name, tdbp->RowNumber(g));
      PushWarning(g, tdbp);
      } // endif SetValue_char

  } else {
    if (Nullable)
      Value->SetNull(true);

    Value->Reset();              // Null value
  } // endif buf

  } // end of ReadColumn

/***********************************************************************/
/*  WriteColumn: make sure the bind buffer is updated.                 */
/***********************************************************************/
void MYSQLCOL::WriteColumn(PGLOBAL g)
  {
  /*********************************************************************/
  /*  Do convert the column value if necessary.                        */
  /*********************************************************************/
  if (Value != To_Val)
    Value->SetValue_pval(To_Val, FALSE);   // Convert the inserted value

#if defined(MYSQL_PREPARED_STATEMENTS)
  if (((PTDBMY)To_Tdb)->Prep) {
    if (Buf_Type == TYPE_DATE) {
      Value->ShowValue((char *)Bind->buffer, (int)Bind->buffer_length);
      Slen = strlen((char *)Bind->buffer);
    } else if (IsTypeChar(Buf_Type))
      Slen = strlen(Value->GetCharValue());

    } // endif Prep
#endif   // MYSQL_PREPARED_STATEMENTS

  } // end of WriteColumn

/* ------------------------------------------------------------------- */

/***********************************************************************/
/*  Implementation of the TDBMYEXC class.                              */
/***********************************************************************/
TDBMYEXC::TDBMYEXC(PMYDEF tdp) : TDBMYSQL(tdp) 
{
  Cmdlist = NULL;
  Cmdcol = NULL;
  Shw = false;
  Havew = false;
  Isw = false;
  Warnings = 0;
  Mxr = tdp->Mxr;
  Nerr = 0;
} // end of TDBMYEXC constructor

TDBMYEXC::TDBMYEXC(PGLOBAL g, PTDBMYX tdbp) : TDBMYSQL(g, tdbp)
{
  Cmdlist = tdbp->Cmdlist;
  Cmdcol = tdbp->Cmdcol;
  Shw = tdbp->Shw;
  Havew = tdbp->Havew;
  Isw = tdbp->Isw;
  Mxr = tdbp->Mxr;
  Nerr = tdbp->Nerr;
} // end of TDBMYEXC copy constructor

// Is this really useful ???
PTDB TDBMYEXC::CopyOne(PTABS t)
  {
  PTDB    tp;
  PCOL    cp1, cp2;
  PGLOBAL g = t->G;

  tp = new(g) TDBMYEXC(g, this);

  for (cp1 = Columns; cp1; cp1 = cp1->GetNext()) {
    cp2 = new(g) MYXCOL((PMYXCOL)cp1, tp);

    NewPointer(t, cp1, cp2);
    } // endfor cp1

  return tp;
  } // end of CopyOne

/***********************************************************************/
/*  Allocate MYSQL column description block.                           */
/***********************************************************************/
PCOL TDBMYEXC::MakeCol(PGLOBAL g, PCOLDEF cdp, PCOL cprec, int n)
  {
  PMYXCOL colp = new(g) MYXCOL(cdp, this, cprec, n);

  if (!colp->Flag)
    Cmdcol = colp->GetName();

  return colp;
  } // end of MakeCol

/***********************************************************************/
/*  MakeCMD: make the SQL statement to send to MYSQL connection.       */
/***********************************************************************/
PCMD TDBMYEXC::MakeCMD(PGLOBAL g)
  {
  PCMD xcmd = NULL;

  if (To_Filter) {
    if (Cmdcol) {
      if (!stricmp(Cmdcol, To_Filter->Body) &&
          (To_Filter->Op == OP_EQ || To_Filter->Op == OP_IN)) {
        xcmd = To_Filter->Cmds;
      } else
        strcpy(g->Message, "Invalid command specification filter");

    } else
      strcpy(g->Message, "No command column in select list");

  } else if (!Srcdef)
    strcpy(g->Message, "No Srcdef default command");
  else
    xcmd = new(g) CMD(g, Srcdef);

  return xcmd;
  } // end of MakeCMD

/***********************************************************************/
/*  EXC GetMaxSize: returns the maximum number of rows in the table.   */
/***********************************************************************/
int TDBMYEXC::GetMaxSize(PGLOBAL g)
  {
  if (MaxSize < 0) {
    MaxSize = 10;                 // a guess
    } // endif MaxSize

  return MaxSize;
  } // end of GetMaxSize

/***********************************************************************/
/*  MySQL Exec Access Method opening routine.                          */
/***********************************************************************/
bool TDBMYEXC::OpenDB(PGLOBAL g)
  {
  if (Use == USE_OPEN) {
    strcpy(g->Message, "Multiple execution is not allowed");
    return true;
    } // endif use

  /*********************************************************************/
  /*  Open a MySQL connection for this table.                          */
  /*  Note: this may not be the proper way to do. Perhaps it is better */
  /*  to test whether a connection is already open for this server     */
  /*  and if so to allocate just a new result set. But this only for   */
  /*  servers allowing concurency in getting results ???               */
  /*********************************************************************/
  if (!Myc.Connected())
    if (Myc.Open(g, Host, Database, User, Pwd, Port))
      return true;

  Use = USE_OPEN;       // Do it now in case we are recursively called

  if (Mode != MODE_READ) {
    strcpy(g->Message, "No INSERT/DELETE/UPDATE of MYSQL EXEC tables");
    return true;
    } // endif Mode

  /*********************************************************************/
  /*  Get the command to execute.                                      */
  /*********************************************************************/
  if (!(Cmdlist = MakeCMD(g))) {
    Myc.Close();
    return true;
    } // endif Query

  return false;
  } // end of OpenDB

/***********************************************************************/
/*  Data Base read routine for MYSQL access method.                    */
/***********************************************************************/
int TDBMYEXC::ReadDB(PGLOBAL g)
  {
  if (Havew) {
    // Process result set from SHOW WARNINGS
    if (Myc.Fetch(g, -1) != RC_OK) {
      Myc.FreeResult();
      Havew = Isw = false;
    } else {
      N++;
      Isw = true;
      return RC_OK;
    } // endif Fetch

    } // endif m_Res

  if (Cmdlist) {
    // Process query to send
    int rc;
  
    do {
      Query = Cmdlist->Cmd;

      switch (rc = Myc.ExecSQLcmd(g, Query, &Warnings)) {
        case RC_NF:
          AftRows = Myc.m_Afrw;
          strcpy(g->Message, "Affected rows");
          break;
        case RC_OK:
          AftRows = Myc.m_Fields;
          strcpy(g->Message, "Result set columns");
          break;
        case RC_FX:
          AftRows = Myc.m_Afrw;
          Nerr++;
          break;
        case RC_INFO:
          Shw = true;
        } // endswitch rc
  
      Cmdlist = (Nerr > Mxr) ? NULL : Cmdlist->Next;
      } while (rc == RC_INFO);

    if (Shw && Warnings)
      Havew = (Myc.ExecSQL(g, "SHOW WARNINGS") == RC_OK);

    ++N;
    return RC_OK;
  } else
    return RC_EF;

  } // end of ReadDB

/***********************************************************************/
/*  WriteDB: Data Base write routine for Exec MYSQL access methods.    */
/***********************************************************************/
int TDBMYEXC::WriteDB(PGLOBAL g)
  {
  strcpy(g->Message, "EXEC MYSQL tables are read only");
  return RC_FX;
  } // end of WriteDB

// ------------------------- MYXCOL functions ---------------------------

/***********************************************************************/
/*  MYXCOL public constructor.                                         */
/***********************************************************************/
MYXCOL::MYXCOL(PCOLDEF cdp, PTDB tdbp, PCOL cprec, int i, PSZ am)
      : MYSQLCOL(cdp, tdbp, cprec, i, am)
  {
  // Set additional EXEC MYSQL access method information for column.
  Flag = cdp->GetOffset();
  } // end of MYSQLCOL constructor

/***********************************************************************/
/*  MYSQLCOL public constructor.                                       */
/***********************************************************************/
MYXCOL::MYXCOL(MYSQL_FIELD *fld, PTDB tdbp, int i, PSZ am)
      : MYSQLCOL(fld, tdbp, i, am)
  {
  if (trace)
    htrc(" making new %sCOL C%d %s at %p\n", am, Index, Name, this);

  } // end of MYSQLCOL constructor

/***********************************************************************/
/*  MYXCOL constructor used for copying columns.                       */
/*  tdbp is the pointer to the new table descriptor.                   */
/***********************************************************************/
MYXCOL::MYXCOL(MYXCOL *col1, PTDB tdbp) : MYSQLCOL(col1, tdbp)
  {
  Flag = col1->Flag;
  } // end of MYXCOL copy constructor

/***********************************************************************/
/*  ReadColumn:                                                        */
/***********************************************************************/
void MYXCOL::ReadColumn(PGLOBAL g)
  {
  PTDBMYX tdbp = (PTDBMYX)To_Tdb;

  if (tdbp->Isw) {
    char *buf = NULL;

    if (Flag < 3) {
      buf = tdbp->Myc.GetCharField(Flag);
      Value->SetValue_psz(buf);
    } else
      Value->Reset();

  } else
    switch (Flag) {
      case  0: Value->SetValue_psz(tdbp->Query);    break;
      case  1: Value->SetValue(tdbp->AftRows);      break;
      case  2: Value->SetValue_psz(g->Message);     break;
      case  3: Value->SetValue(tdbp->Warnings);     break;
      default: Value->SetValue_psz("Invalid Flag"); break;
      } // endswitch Flag

  } // end of ReadColumn

/***********************************************************************/
/*  WriteColumn: should never be called.                               */
/***********************************************************************/
void MYXCOL::WriteColumn(PGLOBAL g)
  {
  assert(false);
  } // end of WriteColumn

/* ---------------------------TDBMCL class --------------------------- */

/***********************************************************************/
/*  TDBMCL class constructor.                                          */
/***********************************************************************/
TDBMCL::TDBMCL(PMYDEF tdp) : TDBCAT(tdp)
  {
  Host = tdp->Hostname;  
  Db   = tdp->Database;    
  Tab  = tdp->Tabname;    
  User = tdp->Username;  
  Pwd  = tdp->Password;   
  Port = tdp->Portnumber;
  } // end of TDBMCL constructor

/***********************************************************************/
/*  GetResult: Get the list the MYSQL table columns.                   */
/***********************************************************************/
PQRYRES TDBMCL::GetResult(PGLOBAL g)
  {
  return MyColumns(g, Host, Db, User, Pwd, Tab, NULL, Port, false);
	} // end of GetResult
