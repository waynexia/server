/************* TabXcl CPP Declares Source Code File (.CPP) *************/
/*  Name: TABXCL.CPP   Version 1.0                                     */
/*                                                                     */
/*  (C) Copyright to the author Olivier BERTRAND          2013         */
/*                                                                     */
/*  XCOL: Table having one column containing several values            */
/*  comma separated. When creating the table, the name of the X        */
/*  column is given by the Name option.       					               */
/*  This first version has one limitation:                             */
/*  - The X column has the same length than in the physical file.      */
/*  This tables produces as many rows for a physical row than the      */
/*  number of items in the X column (eventually 0).                    */
/***********************************************************************/

/***********************************************************************/
/*  Include relevant section of system dependant header files.         */
/***********************************************************************/
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
#include "global.h"
#include "plgdbsem.h"
#include "plgcnx.h"                       // For DB types
#include "resource.h"
#include "reldef.h"
#include "filamtxt.h"
#include "tabdos.h"
#include "tabcol.h"
#include "tabxcl.h"
#include "xtable.h"
#if defined(MYSQL_SUPPORT)
#include "tabmysql.h"
#endif   // MYSQL_SUPPORT
#include "ha_connect.h"
#include "mycat.h"

extern "C" int trace;

/* -------------- Implementation of the XCOL classes	---------------- */

/***********************************************************************/
/*  XCLDEF constructor.                                                */
/***********************************************************************/
XCLDEF::XCLDEF(void)
  {
  Pseudo = 3;
  Xcol = NULL;
  Sep = ',';
  Mult = 10;
} // end of XCLDEF constructor

/***********************************************************************/
/*  DefineAM: define specific AM block values from XCOL file.          */
/***********************************************************************/
bool XCLDEF::DefineAM(PGLOBAL g, LPCSTR am, int poff)
  {
  char buf[8];

  Xcol = Cat->GetStringCatInfo(g, "Colname", "");
  Cat->GetCharCatInfo("Separator", ",", buf, sizeof(buf));
  Sep = (strlen(buf) == 2 && buf[0] == '\\' && buf[1] == 't') ? '\t' : *buf;
  Mult = Cat->GetIntCatInfo("Mult", 10);
  return PRXDEF::DefineAM(g, am, poff);
  } // end of DefineAM

/***********************************************************************/
/*  GetTable: makes a new TDB of the proper type.                      */
/***********************************************************************/
PTDB XCLDEF::GetTable(PGLOBAL g, MODE mode)
  {
  if (Catfunc == FNC_COL)
    return new(g) TDBTBC(this);
  else
    return new(g) TDBXCL(this);

  } // end of GetTable

/* ------------------------------------------------------------------- */

/***********************************************************************/
/*  Implementation of the TDBXCL class.                                */
/***********************************************************************/
TDBXCL::TDBXCL(PXCLDEF tdp) : TDBPRX(tdp)
  {
	Xcolumn = tdp->Xcol;						// CSV column name     
	Xcolp = NULL;										// To the XCVCOL column
	Mult = tdp->Mult;								// Multiplication factor
	N = 0;													// The current table index
	M = 0;                          // The occurence rank
	RowFlag = 0;    								// 0: Ok, 1: Same, 2: Skip
	New = TRUE;						          // TRUE for new line
	Sep = tdp->Sep;                 // The Xcol separator
  } // end of TDBXCL constructor

/***********************************************************************/
/*  Initializes the table.                                             */
/***********************************************************************/
bool TDBXCL::InitTable(PGLOBAL g)
  {
  if (!Tdbp) {
    PCOLDEF cdp;

    // Get the table description block of this table
    if (!(Tdbp = (PTDBASE)GetSubTable(g, ((PXCLDEF)To_Def)->Tablep)))
      return TRUE;

    for (cdp = Tdbp->GetDef()->GetCols(); cdp; cdp = cdp->GetNext())
      if (!stricmp(cdp->GetName(), Xcolumn))
        break;

    if (!cdp) {
      sprintf(g->Message, "%s is not a %s column",
                          Xcolumn, Tdbp->GetName());
      return TRUE;
      } // endif cdp

    } // endif Tdbp

  return FALSE;
  } // end of InitTable

/***********************************************************************/
/*  Allocate XCL column description block.                             */
/***********************************************************************/
PCOL TDBXCL::MakeCol(PGLOBAL g, PCOLDEF cdp, PCOL cprec, int n)
  {
  PCOL colp;
  
  if (!stricmp(cdp->GetName(), Xcolumn)) {
		Xcolp = new(g) XCLCOL(g, cdp, this, cprec, n);
    colp = Xcolp;
  } else
    colp = new(g) PRXCOL(cdp, this, cprec, n);

  return colp;
  } // end of MakeCol

/***********************************************************************/
/*  XCL GetMaxSize: returns the maximum number of rows in the table.   */
/***********************************************************************/
int TDBXCL::GetMaxSize(PGLOBAL g)
  {
  if (MaxSize < 0) {
    if (InitTable(g))
      return NULL;
  
  	MaxSize = Mult * Tdbp->GetMaxSize(g);
    } // endif MaxSize

  return MaxSize;
  } // end of GetMaxSize

/***********************************************************************/
/*  In this sample, ROWID will be the (virtual) row number,            */
/*  while ROWNUM will be the occurence rank in the multiple column.    */
/***********************************************************************/
int TDBXCL::RowNumber(PGLOBAL g, bool b)
	{
	return (b) ? M : N;
	} // end of RowNumber
 
/***********************************************************************/
/*  XCV Access Method opening routine.                                 */
/***********************************************************************/
bool TDBXCL::OpenDB(PGLOBAL g)
  {
  if (Use == USE_OPEN) {
    /*******************************************************************/
    /*  Table already open, just replace it at its beginning.          */
    /*******************************************************************/
		M = N = 0;
		RowFlag = 0;
    New = TRUE;
		return Tdbp->OpenDB(g);
    } // endif use

  if (Mode != MODE_READ) {
    /*******************************************************************/
    /* Currently XCOL tables cannot be modified.                       */
    /*******************************************************************/
    strcpy(g->Message, "XCOL tables are read only");
    return TRUE;
    } // endif Mode

  if (InitTable(g))
    return NULL;
  
  /*********************************************************************/
  /*  Check and initialize the subtable columns.                       */
  /*********************************************************************/
  for (PCOL cp = Columns; cp; cp = cp->GetNext())
    if (((PXCLCOL)cp)->Init(g))
      return TRUE;

  /*********************************************************************/
  /*  Physically open the object table.                                */
  /*********************************************************************/
	if (Tdbp->OpenDB(g))
		return TRUE;

	return FALSE;
  } // end of OpenDB

/***********************************************************************/
/*  Data Base read routine for XCV access method.                      */
/***********************************************************************/
int TDBXCL::ReadDB(PGLOBAL g)
  {
	int rc = RC_OK;

  /*********************************************************************/
  /*  Now start the multi reading process.                             */
  /*********************************************************************/
	do {
		if (RowFlag != 1) {
			if ((rc = Tdbp->ReadDB(g)) != RC_OK)
				break;

      New = TRUE;
			M = 1;
    } else {
      New = FALSE;
			M++;
    } // endif RowFlag

    if (Xcolp) {
  		RowFlag = 0;
	  	Xcolp->ReadColumn(g);
      } // endif Xcolp

  	N++;
		} while (RowFlag == 2);

	return rc;
  } // end of ReadDB


// ------------------------ XCLCOL functions ----------------------------

/***********************************************************************/
/*  XCLCOL public constructor.                                         */
/***********************************************************************/
XCLCOL::XCLCOL(PGLOBAL g, PCOLDEF cdp, PTDB tdbp, PCOL cprec, int i)
			: PRXCOL(cdp, tdbp, cprec, i, "XCL")
  {
  // Set additional XXL access method information for column.
  Cbuf = (char*)PlugSubAlloc(g, NULL, Long + 1);
	Cp = NULL;						      // Pointer to current position in Cbuf
	Sep = ((PTDBXCL)tdbp)->Sep;
	AddStatus(BUF_READ);	      // Only evaluated from TDBXCL::ReadDB
  } // end of XCLCOL constructor

/***********************************************************************/
/*  What this routine does is to get the comma-separated string        */
/*  from the source table column, extract the single values and        */
/*  set the flag for the table ReadDB function.                        */
/***********************************************************************/
void XCLCOL::ReadColumn(PGLOBAL g)
  {
	if (((PTDBXCL)To_Tdb)->New) {
		Colp->ReadColumn(g);
		strcpy(Cbuf, To_Val->GetCharValue());
		Cp = Cbuf;
		} // endif New

	if (*Cp) {
		PSZ p;

    // Trim left
    for (p = Cp; *p == ' '; p++) ;

		if ((Cp = strchr(Cp, Sep)))
			// Separator is found
			*Cp++ = '\0';

		Value->SetValue_psz(p);
  } else if (Nullable) {
    Value->Reset();
    Value->SetNull(true);
  } else
    // Skip that row
		((PTDBXCL)To_Tdb)->RowFlag = 2;
	
	if (Cp && *Cp)
		// More to come from the same row
		((PTDBXCL)To_Tdb)->RowFlag = 1;

  } // end of ReadColumn
