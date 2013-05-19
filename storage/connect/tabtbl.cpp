/************* TabTbl C++ Program Source Code File (.CPP) **************/
/* PROGRAM NAME: TABTBL                                                */
/* -------------                                                       */
/*  Version 1.5                                                        */
/*                                                                     */
/* COPYRIGHT:                                                          */
/* ----------                                                          */
/*  (C) Copyright to PlugDB Software Development          2008-2013    */
/*  Author: Olivier BERTRAND                                           */
/*                                                                     */
/* WHAT THIS PROGRAM DOES:                                             */
/* -----------------------                                             */
/*  This program are the TDBTBL class DB routines.                     */
/*                                                                     */
/* WHAT YOU NEED TO COMPILE THIS PROGRAM:                              */
/* --------------------------------------                              */
/*                                                                     */
/*  REQUIRED FILES:                                                    */
/*  ---------------                                                    */
/*    TABTBL.CPP     - Source code                                     */
/*    PLGDBSEM.H     - DB application declaration file                 */
/*    TABDOS.H       - TABDOS classes declaration file                 */
/*    TABTBL.H       - TABTBL classes declaration file                 */
/*    GLOBAL.H       - Global declaration file                         */
/*                                                                     */
/*  REQUIRED LIBRARIES:                                                */
/*  -------------------                                                */
/*    Large model C library                                            */
/*                                                                     */
/*  REQUIRED PROGRAMS:                                                 */
/*  ------------------                                                 */
/*    IBM, Borland, GNU or Microsoft C++ Compiler and Linker           */
/*                                                                     */
/***********************************************************************/

/***********************************************************************/
/*  Include relevant section of system dependant header files.         */
/***********************************************************************/
//#include "sql_base.h"
#include "my_global.h"
#if defined(WIN32)
#include <stdlib.h>
#include <stdio.h>
#if defined(__BORLANDC__)
#define __MFC_COMPAT__                   // To define min/max as macro
#endif
//#include <windows.h>
#else
#if defined(UNIX)
#include <fnmatch.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "osutil.h"
#else
//#include <io.h>
#endif
//#include <fcntl.h>
#endif

/***********************************************************************/
/*  Include application header files:                                  */
/***********************************************************************/
#include "table.h"       // MySQL table definitions
#include "global.h"      // global declarations
#include "plgdbsem.h"    // DB application declarations
#include "reldef.h"      // DB definition declares
//#include "filter.h"      // FILTER classes dcls
#include "filamtxt.h"
#include "tabcol.h"
#include "tabdos.h"      // TDBDOS and DOSCOL class dcls
#include "tabtbl.h"
#if defined(MYSQL_SUPPORT)
#include "tabmysql.h"
#endif   // MYSQL_SUPPORT
#include "ha_connect.h"
#include "mycat.h"       // For GetHandler

extern "C" int trace;

/* ---------------------------- Class TBLDEF ---------------------------- */

/**************************************************************************/
/*  Constructor.                                                          */
/**************************************************************************/
TBLDEF::TBLDEF(void)
  {
//To_Tables = NULL;
  Ntables = 0;
  Pseudo = 3;
  } // end of TBLDEF constructor

/**************************************************************************/
/*  DefineAM: define specific AM block values from XDB file.              */
/**************************************************************************/
bool TBLDEF::DefineAM(PGLOBAL g, LPCSTR am, int poff)
  {
  char   *tablist, *dbname;

  Desc = "Table list table";
  tablist = Cat->GetStringCatInfo(g, "Tablist", "");
  dbname = Cat->GetStringCatInfo(g, "Dbname", "*");
  Ntables = 0;

  if (*tablist) {
    char  *p, *pn, *pdb;
    PTABLE tbl;

    for (pdb = tablist; ;) {
      if ((p = strchr(pdb, ',')))
        *p = 0;

      // Analyze the table name, it may have the format:
      // [dbname.]tabname
      if ((pn = strchr(pdb, '.'))) {
        *pn++ = 0;
      } else {
        pn = pdb;
        pdb = dbname;
      } // endif p

      // Allocate the TBLIST block for that table
      tbl = new(g) XTAB(pn);
      tbl->SetQualifier(pdb);
      
      if (trace)
        htrc("TBL: Name=%s db=%s\n", tbl->GetName(), tbl->GetQualifier());

      // Link the blocks
      if (Tablep)
        Tablep->Link(tbl);
      else
        Tablep = tbl;

      Ntables++;

      if (p)
        pdb = pn + strlen(pn) + 1;
      else
        break;

      } // endfor pdb

    Maxerr = Cat->GetIntCatInfo("Maxerr", 0);
    Accept = (Cat->GetBoolCatInfo("Accept", 0) != 0);
    } // endif fsec || tablist

  return FALSE;
  } // end of DefineAM

/***********************************************************************/
/*  GetTable: makes a new Table Description Block.                     */
/***********************************************************************/
PTDB TBLDEF::GetTable(PGLOBAL g, MODE m)
  {
  if (Catfunc == FNC_COL)
    return new(g) TDBTBC(this);
  else
    return new(g) TDBTBL(this);

  } // end of GetTable

/* ------------------------- Class TDBTBL ---------------------------- */

/***********************************************************************/
/*  TDBTBL constructors.                                               */
/***********************************************************************/
TDBTBL::TDBTBL(PTBLDEF tdp) : TDBPRX(tdp)
  {
  Tablist = NULL;
  CurTable = NULL;
//Tdbp = NULL;
  Accept = tdp->Accept;
  Maxerr = tdp->Maxerr;
  Nbf = 0;
  Rows = 0;
  Crp = 0;
//  NTables = 0;
//  iTable = 0;
  } // end of TDBTBL standard constructor

/***********************************************************************/
/*  Allocate TBL column description block.                             */
/***********************************************************************/
PCOL TDBTBL::MakeCol(PGLOBAL g, PCOLDEF cdp, PCOL cprec, int n)
  {
  return new(g) PRXCOL(cdp, this, cprec, n);
  } // end of MakeCol

/***********************************************************************/
/*  InsertSpecialColumn: Put a special column ahead of the column list.*/
/***********************************************************************/
PCOL TDBTBL::InsertSpecialColumn(PGLOBAL g, PCOL scp)
  {
  PCOL colp;

  if (!scp->IsSpecial())
    return NULL;

  if (scp->GetAmType() == TYPE_AM_TABID)
    // This special column is handled locally
    colp = new((TIDBLK*)scp) TBTBLK(scp->GetValue());
  else  // Other special columns are treated normally
    colp = scp;

  colp->SetNext(Columns);
  Columns = colp;
  return colp;
  } // end of InsertSpecialColumn

/***********************************************************************/
/*  Initializes the table table list.                                  */
/***********************************************************************/
bool TDBTBL::InitTableList(PGLOBAL g)
  {
  int     n;
  PTABLE  tp, tabp;
  PTDBASE tdbp;
  PCOL    colp;
  PTBLDEF tdp = (PTBLDEF)To_Def;

//  PlugSetPath(filename, Tdbp->GetFile(g), Tdbp->GetPath());

  for (n = 0, tp = tdp->Tablep; tp; tp = tp->GetNext()) {
    if (TestFil(g, To_Filter, tp)) {
      tabp = new(g) XTAB(tp);

      // Get the table description block of this table
      if (!(tdbp = GetSubTable(g, tabp))) {
        if (++Nbf > Maxerr)
          return TRUE;               // Error return
        else
          continue;                  // Skip this table

      } else
        RemoveNext(tabp);            // To avoid looping

      // We must allocate subtable columns before GetMaxSize is called
      // because some (PLG, ODBC?) need to have their columns attached.
      // Real initialization will be done later.
      for (colp = Columns; colp; colp = colp->GetNext())
        if (!colp->IsSpecial())
          if (((PPRXCOL)colp)->Init(g) && !Accept)
            return TRUE;

      if (Tablist)
        Tablist->Link(tabp);
      else
        Tablist = tabp;

      n++;
      } // endif filp

    } // endfor tp

//NumTables = n;
  To_Filter = NULL;        // To avoid doing it several times
  return FALSE;
  } // end of InitTableList

/***********************************************************************/
/*  Test the tablename against the pseudo "local" filter.              */
/***********************************************************************/
bool TDBTBL::TestFil(PGLOBAL g, PFIL filp, PTABLE tabp)
  {
  char *fil, op[8], tn[NAME_LEN];
  bool  neg;

  if (!filp)
    return TRUE;
  else if (strstr(filp, " OR ") || strstr(filp, " AND "))
    return TRUE;               // Not handled yet
  else
    fil = filp + (*filp == '(' ? 1 : 0);

  if (sscanf(fil, "TABID %s", op) != 1)
    return TRUE;               // ignore invalid filter

  if ((neg = !strcmp(op, "NOT")))
    strcpy(op, "IN");

  if (!strcmp(op, "=")) {
    // Temporarily, filter must be "TABID = 'value'" only
    if (sscanf(fil, "TABID = '%[^']'", tn) != 1)
      return TRUE;             // ignore invalid filter

    return !stricmp(tn, tabp->GetName());
  } else if (!strcmp(op, "IN")) {
    char *p, *tnl = (char*)PlugSubAlloc(g, NULL, strlen(fil) - 10);
    int   n;

    if (neg)
      n = sscanf(fil, "TABID NOT IN (%[^)])", tnl);
    else
      n = sscanf(fil, "TABID IN (%[^)])", tnl);

    if (n != 1)
      return TRUE;             // ignore invalid filter

    while (tnl) {
      if ((p = strchr(tnl, ',')))
        *p++ = 0;

      if (sscanf(tnl, "'%[^']'", tn) != 1)
        return TRUE;           // ignore invalid filter
      else if (!stricmp(tn, tabp->GetName()))
        return !neg;           // Found

      tnl = p;
      } // endwhile

    return neg;                // Not found
  } // endif op

  return TRUE;                 // invalid operator
  } // end of TestFil

/***********************************************************************/
/*  Sum up the sizes of all sub-tables.                                */
/***********************************************************************/
int TDBTBL::GetMaxSize(PGLOBAL g)
  {
  if (MaxSize < 0) {
    int mxsz;

    if (!Tablist && InitTableList(g))
      return 0;               // Cannot be calculated at this stage

    MaxSize = 0;

    for (PTABLE tabp = Tablist; tabp; tabp = tabp->GetNext()) {
      if ((mxsz = tabp->GetTo_Tdb()->GetMaxSize(g)) < 0) {
        MaxSize = -1;
        return mxsz;
        } // endif mxsz

      MaxSize += mxsz;
      } // endfor i

    } // endif MaxSize

  return MaxSize;
  } // end of GetMaxSize

/***********************************************************************/
/*  Reset read/write position values.                                  */
/***********************************************************************/
void TDBTBL::ResetDB(void)
  {
  for (PCOL colp = Columns; colp; colp = colp->GetNext())
    if (colp->GetAmType() == TYPE_AM_TABID)
      colp->COLBLK::Reset();

  for (PTABLE tabp = Tablist; tabp; tabp = tabp->GetNext())
    ((PTDBASE)tabp->GetTo_Tdb())->ResetDB();

  Tdbp = (PTDBASE)Tablist->GetTo_Tdb();
  Crp = 0;
  } // end of ResetDB

/***********************************************************************/
/*  Returns RowId if b is false or Rownum if b is true.                */
/***********************************************************************/
int TDBTBL::RowNumber(PGLOBAL g, bool b)
  {
  return Tdbp->RowNumber(g) + ((b) ? 0 : Rows);
  } // end of RowNumber

/***********************************************************************/
/*  TBL Access Method opening routine.                                 */
/*  Open first file, other will be opened sequencially when reading.   */
/***********************************************************************/
bool TDBTBL::OpenDB(PGLOBAL g)
  {
  if (trace)
    htrc("TBL OpenDB: tdbp=%p tdb=R%d use=%d key=%p mode=%d\n",
                      this, Tdb_No, Use, To_Key_Col, Mode);

  if (Use == USE_OPEN) {
    /*******************************************************************/
    /*  Table already open, replace it at its beginning.               */
    /*******************************************************************/
    ResetDB();
    return Tdbp->OpenDB(g);  // Re-open fist table
    } // endif use

  /*********************************************************************/
  /*  When GetMaxsize was called, To_Filter was not set yet.           */
  /*********************************************************************/
  if (To_Filter && Tablist) {
    Tablist = NULL;
    Nbf = 0;
    } // endif To_Filter

  /*********************************************************************/
  /*  Open the first table of the list.                                */
  /*********************************************************************/
  if (!Tablist && InitTableList(g))     //  done in GetMaxSize
    return TRUE;

  if ((CurTable = Tablist)) {
    Tdbp = (PTDBASE)CurTable->GetTo_Tdb();
    Tdbp->SetMode(Mode);
//  Tdbp->ResetDB();
//  Tdbp->ResetSize();

    // Check and initialize the subtable columns
    for (PCOL cp = Columns; cp; cp = cp->GetNext())
      if (cp->GetAmType() == TYPE_AM_TABID)
        cp->COLBLK::Reset();
      else if (((PPRXCOL)cp)->Init(g) && !Accept)
        return TRUE;
        
    if (trace)
      htrc("Opening subtable %s\n", Tdbp->GetName());

    // Now we can safely open the table
    if (Tdbp->OpenDB(g))
      return TRUE;

    } // endif *Tablist

  Use = USE_OPEN;
  return FALSE;
  } // end of OpenDB

/***********************************************************************/
/*  ReadDB: Data Base read routine for MUL access method.              */
/***********************************************************************/
int TDBTBL::ReadDB(PGLOBAL g)
  {
  int rc;

  if (!CurTable)
    return RC_EF;
  else if (To_Kindex) {
    /*******************************************************************/
    /*  Reading is by an index table.                                  */
    /*******************************************************************/
    strcpy(g->Message, MSG(NO_INDEX_READ));
    rc = RC_FX;
  } else {
    /*******************************************************************/
    /*  Now start the reading process.                                 */
    /*******************************************************************/
   retry:
    rc = Tdbp->ReadDB(g);

    if (rc == RC_EF) {
      // Total number of rows met so far
      Rows += Tdbp->RowNumber(g) - 1;
      Crp += Tdbp->GetProgMax(g);

      if ((CurTable = CurTable->GetNext())) {
        /***************************************************************/
        /*  Continue reading from next table file.                     */
        /***************************************************************/
        Tdbp->CloseDB(g);
        Tdbp = (PTDBASE)CurTable->GetTo_Tdb();

        // Check and initialize the subtable columns
        for (PCOL cp = Columns; cp; cp = cp->GetNext())
          if (cp->GetAmType() == TYPE_AM_TABID)
            cp->COLBLK::Reset();
          else if (((PPRXCOL)cp)->Init(g) && !Accept)
            return RC_FX;

        if (trace)
          htrc("Opening subtable %s\n", Tdbp->GetName());

        // Now we can safely open the table
        if (Tdbp->OpenDB(g))     // Open next table
          return RC_FX;

        goto retry;
        } // endif iFile

    } else if (rc == RC_FX)
      strcat(strcat(strcat(g->Message, " ("), Tdbp->GetName()), ")");

  } // endif To_Kindex

  return rc;
  } // end of ReadDB

/* ---------------------------- TBTBLK ------------------------------- */

/***********************************************************************/
/*  ReadColumn:                                                        */
/***********************************************************************/
void TBTBLK::ReadColumn(PGLOBAL g)
  {
  if (trace)
    htrc("TBT ReadColumn: name=%s\n", Name);

  Value->SetValue_psz((char*)((PTDBTBL)To_Tdb)->Tdbp->GetName());

  } // end of ReadColumn

/* ------------------------------------------------------------------- */
