/************* TabSys C++ Program Source Code File (.CPP) **************/
/* PROGRAM NAME: TABSYS                                                */
/* -------------                                                       */
/*  Version 2.2                                                        */
/*                                                                     */
/*  Author Olivier BERTRAND                           2004-2013        */
/*                                                                     */
/*  This program are the INI/CFG tables classes.                       */
/***********************************************************************/

/***********************************************************************/
/*  Include relevant sections of the System header files.              */
/***********************************************************************/
#include "my_global.h"
#if defined(WIN32)
#if defined(__BORLANDC__)
#define __MFC_COMPAT__                   // To define min/max as macro
#endif   // __BORLANDC__
//#include <windows.h>
#else   // !WIN32
#if defined(UNIX)
#include <errno.h>
#include <unistd.h>
#else   // !UNIX
#include <io.h>
#endif  // !UNIX
#include <fcntl.h>
#endif  // !WIN32

/***********************************************************************/
/*  Include application header files:                                  */
/*  global.h    is header containing all global declarations.          */
/*  plgdbsem.h  is header containing the DB application declarations.  */
/*  tabdos.h    is header containing the TABDOS class declarations.    */
/***********************************************************************/
#include "global.h"
#include "plgdbsem.h"
#include "reldef.h"
#if !defined(WIN32)
#include "osutil.h"
#endif   // !WIN32
#include "filamtxt.h"
#include "tabdos.h"
#include "tabsys.h"
#include "tabmul.h"
#include "inihandl.h"

#define CSZ      36                       // Column section name length
#define CDZ      256                      // Column definition length

#if !defined(WIN32)
#define GetPrivateProfileSectionNames(S,L,I)  \
        GetPrivateProfileString(NULL,NULL,"",S,L,I)
#endif   // !WIN32

extern "C" int  trace;

/* -------------- Implementation of the INI classes ------------------ */

/***********************************************************************/
/*  Constructor.                                                       */
/***********************************************************************/
INIDEF::INIDEF(void)
  {
  Pseudo = 3;
  Fn = NULL;
  Xname = NULL;
  Layout = '?';
  Ln = 0;
  } // end of INIDEF constructor

/***********************************************************************/
/*  DefineAM: define specific AM block values from XDB file.           */
/***********************************************************************/
bool INIDEF::DefineAM(PGLOBAL g, LPCSTR am, int poff)
  {
  char   buf[8];

  Fn = Cat->GetStringCatInfo(g, "Filename", NULL);
  Cat->GetCharCatInfo("Layout", "C", buf, sizeof(buf));
  Layout = toupper(*buf);

  if (Fn) {
    char   *p = (char*)PlugSubAlloc(g, NULL, _MAX_PATH);

    PlugSetPath(p, Fn, GetPath());
    Fn = p;
  } else {
    strcpy(g->Message, MSG(MISSING_FNAME));
    return true;
  } // endif Fn

  Ln = Cat->GetSizeCatInfo("Secsize", "8K");
  Desc = Fn;
  return false;
  } // end of DefineAM

/***********************************************************************/
/*  GetTable: makes a new TDB of the proper type.                      */
/***********************************************************************/
PTDB INIDEF::GetTable(PGLOBAL g, MODE m)
  {
  PTDBASE tdbp;

  if (Layout == 'C')
    tdbp = new(g) TDBINI(this);
  else
    tdbp = new(g) TDBXIN(this);

  if (Multiple)
    tdbp = new(g) TDBMUL(tdbp);         // No block optimization yet

  return tdbp;
  } // end of GetTable

/***********************************************************************/
/*  DeleteTableFile: Delete INI table files using platform API.        */
/***********************************************************************/
bool INIDEF::DeleteTableFile(PGLOBAL g)
  {
  char    filename[_MAX_PATH];
  bool    rc;

  // Delete the INI table file if not protected
  if (!IsReadOnly()) {
    PlugSetPath(filename, Fn, GetPath());
#if defined(WIN32)
    rc = !DeleteFile(filename);
#else    // UNIX
    rc = remove(filename);
#endif   // UNIX
  } else
    rc =true;

  return rc;                                  // Return true if error
  } // end of DeleteTableFile

/* ------------------------------------------------------------------- */

/***********************************************************************/
/*  Implementation of the TDBINI class.                                */
/***********************************************************************/
TDBINI::TDBINI(PINIDEF tdp) : TDBASE(tdp)
  {
  Ifile = tdp->Fn;
  Seclist = NULL;
  Section = NULL;
  Seclen = tdp->Ln;
  N = 0;
  } // end of TDBINI constructor

TDBINI::TDBINI(PTDBINI tdbp) : TDBASE(tdbp)
  {
  Ifile = tdbp->Ifile;
  Seclist = tdbp->Seclist;
  Section = tdbp->Section;
  Seclen = tdbp->Seclen;
  N = tdbp->N;
  } // end of TDBINI copy constructor

// Is this really useful ???
PTDB TDBINI::CopyOne(PTABS t)
  {
  PTDB    tp;
  PINICOL cp1, cp2;
  PGLOBAL g = t->G;

  tp = new(g) TDBINI(this);

  for (cp1 = (PINICOL)Columns; cp1; cp1 = (PINICOL)cp1->GetNext()) {
    cp2 = new(g) INICOL(cp1, tp);  // Make a copy
    NewPointer(t, cp1, cp2);
    } // endfor cp1

  return tp;
  } // end of CopyOne

/***********************************************************************/
/*  Get the section list from the INI file.                            */
/***********************************************************************/
char *TDBINI::GetSeclist(PGLOBAL g)
  {
  if (trace)
    htrc("GetSeclist: Seclist=%p\n", Seclist);
    
  if (!Seclist) {
    // Result will be retrieved from the INI file
    Seclist = (char*)PlugSubAlloc(g, NULL, Seclen);
    GetPrivateProfileSectionNames(Seclist, Seclen, Ifile);
    } // endif Seclist

  return Seclist;
  } // end of GetSeclist

/***********************************************************************/
/*  Allocate INI column description block.                             */
/***********************************************************************/
PCOL TDBINI::MakeCol(PGLOBAL g, PCOLDEF cdp, PCOL cprec, int n)
  {
  return new(g) INICOL(cdp, this, cprec, n);
  } // end of MakeCol

/***********************************************************************/
/*  INI GetMaxSize: returns the number of sections in the INI file.    */
/***********************************************************************/
int TDBINI::GetMaxSize(PGLOBAL g)
  {
  if (MaxSize < 0 && GetSeclist(g)) {
    // Count the number of sections from the section list
    char *p;

    for (MaxSize = 0, p = Seclist; *p; p += (strlen(p) + 1))
      MaxSize++;

    } // endif MaxSize

  return MaxSize;
  } // end of GetMaxSize

/***********************************************************************/
/*  INI Access Method opening routine.                                 */
/***********************************************************************/
bool TDBINI::OpenDB(PGLOBAL g)
  {
  PINICOL colp;

  if (Use == USE_OPEN) {
#if 0
    if (To_Kindex)
      /*****************************************************************/
      /*  Table is to be accessed through a sorted index table.        */
      /*****************************************************************/
      To_Kindex->Reset();
#endif // 0
    Section = NULL;
    N = 0;
    return false;
    } // endif use

  /*********************************************************************/
  /*  OpenDB: initialize the INI file processing.                      */
  /*********************************************************************/
  GetSeclist(g);
  Use = USE_OPEN;       // Do it now in case we are recursively called

  /*********************************************************************/
  /*  Allocate the buffers that will contain key values.               */
  /*********************************************************************/
  for (colp = (PINICOL)Columns; colp; colp = (PINICOL)colp->GetNext())
    if (!colp->IsSpecial())            // Not a pseudo column
      colp->AllocBuf(g);

  if (trace)
    htrc("INI OpenDB: seclist=%s seclen=%d ifile=%s\n", 
          Seclist, Seclen, Ifile);

  return false;
  } // end of OpenDB

/***********************************************************************/
/*  Data Base read routine for INI access method.                      */
/***********************************************************************/
int TDBINI::ReadDB(PGLOBAL g)
  {
  /*********************************************************************/
  /*  Now start the pseudo reading process.                            */
  /*********************************************************************/
#if 0                // INI tables are not indexable
  if (To_Kindex) {
    /*******************************************************************/
    /*  Reading is by an index table.                                  */
    /*******************************************************************/
    int recpos = To_Kindex->Fetch(g);

    switch (recpos) {
      case -1:           // End of file reached
        return RC_EF;
      case -2:           // No match for join
        return RC_NF;
      case -3:           // Same record as last non null one
        return RC_OK;
      default:
        Section = (char*)recpos;    // No good on 64 bit machines
      } // endswitch recpos

  } else {
#endif // 0
    if (!Section)
      Section = Seclist;
    else
      Section += (strlen(Section) + 1);

    if (trace > 1)
      htrc("INI ReadDB: section=%s N=%d\n", Section, N);

    N++;
//} // endif To_Kindex

  return (*Section) ? RC_OK : RC_EF;
  } // end of ReadDB

/***********************************************************************/
/*  WriteDB: Data Base write routine for INI access methods.           */
/***********************************************************************/
int TDBINI::WriteDB(PGLOBAL g)
  {
  // This is to check that section name was given when inserting
  if (Mode == MODE_INSERT)
    Section = NULL;

  // Nothing else to do because all was done in WriteColumn
  return RC_OK;
  } // end of WriteDB

/***********************************************************************/
/*  Data Base delete line routine for INI access methods.              */
/***********************************************************************/
int TDBINI::DeleteDB(PGLOBAL g, int irc)
  {
  switch (irc) {
    case RC_EF:
      break;
    case RC_FX:
      while (ReadDB(g) == RC_OK)
        if (!WritePrivateProfileString(Section, NULL, NULL, Ifile)) {
          sprintf(g->Message, "Error %d accessing %s", 
                              GetLastError(), Ifile);
          return RC_FX;
          } // endif

      break;
    default:
      if (!Section) {
        strcpy(g->Message, MSG(NO_SECTION_NAME));
        return RC_FX;
      } else
        if (!WritePrivateProfileString(Section, NULL, NULL, Ifile)) {
          sprintf(g->Message, "Error %d accessing %s", 
                              GetLastError(), Ifile);
          return RC_FX;
          } // endif rc

    } // endswitch irc

  return RC_OK;
  } // end of DeleteDB

/***********************************************************************/
/*  Data Base close routine for INI access methods.                    */
/***********************************************************************/
void TDBINI::CloseDB(PGLOBAL g)
  {
#if !defined(WIN32)
  PROFILE_Close(Ifile);
#endif   // !WIN32
  } // end of CloseDB

// ------------------------ INICOL functions ----------------------------

/***********************************************************************/
/*  INICOL public constructor.                                         */
/***********************************************************************/
INICOL::INICOL(PCOLDEF cdp, PTDB tdbp, PCOL cprec, int i, PSZ am)
  : COLBLK(cdp, tdbp, i)
  {
  if (cprec) {
    Next = cprec->GetNext();
    cprec->SetNext(this);
  } else {
    Next = tdbp->GetColumns();
    tdbp->SetColumns(this);
  } // endif cprec

  // Set additional INI access method information for column.
  Valbuf = NULL;
  Flag = cdp->GetOffset();
  Long = cdp->GetLong();
  To_Val = NULL;
  } // end of INICOL constructor

/***********************************************************************/
/*  INICOL constructor used for copying columns.                       */
/*  tdbp is the pointer to the new table descriptor.                   */
/***********************************************************************/
INICOL::INICOL(INICOL *col1, PTDB tdbp) : COLBLK(col1, tdbp)
  {
  Valbuf = col1->Valbuf;
  Flag = col1->Flag;
  Long = col1->Long;
  To_Val = col1->To_Val;
  } // end of INICOL copy constructor

/***********************************************************************/
/*  Allocate a buffer of the proper size.                              */
/***********************************************************************/
void INICOL::AllocBuf(PGLOBAL g)
  {
  if (!Valbuf)
    Valbuf = (char*)PlugSubAlloc(g, NULL, Long + 1);

  } // end of AllocBuf

/***********************************************************************/
/*  SetBuffer: prepare a column block for write operation.             */
/***********************************************************************/
bool INICOL::SetBuffer(PGLOBAL g, PVAL value, bool ok, bool check)
  {
  if (!(To_Val = value)) {
    sprintf(g->Message, MSG(VALUE_ERROR), Name);
    return true;
  } else if (Buf_Type == value->GetType()) {
    // Values are of the (good) column type
    if (Buf_Type == TYPE_DATE) {
      // If any of the date values is formatted
      // output format must be set for the receiving table
      if (GetDomain() || ((DTVAL *)value)->IsFormatted())
        goto newval;          // This will make a new value;

    } else if (Buf_Type == TYPE_FLOAT)
      // Float values must be written with the correct (column) precision
      // Note: maybe this should be forced by ShowValue instead of this ?
      value->SetPrec(GetPrecision());

    Value = value;            // Directly access the external value
  } else {
    // Values are not of the (good) column type
    if (check) {
      sprintf(g->Message, MSG(TYPE_VALUE_ERR), Name,
              GetTypeName(Buf_Type), GetTypeName(value->GetType()));
      return true;
      } // endif check

 newval:
    if (InitValue(g))         // Allocate the matching value block
      return true;

  } // endif's Value, Buf_Type

  // Allocate the internal value buffer
  AllocBuf(g);

  // Because Colblk's have been made from a copy of the original TDB in
  // case of Update, we must reset them to point to the original one.
  if (To_Tdb->GetOrig())
    To_Tdb = (PTDB)To_Tdb->GetOrig();

  // Set the Column
  Status = (ok) ? BUF_EMPTY : BUF_NO;
  return false;
  } // end of SetBuffer

/***********************************************************************/
/*  ReadColumn: what this routine does is to access the key buffer set */
/*  from the corresponding section, extract from it the key value      */
/*  corresponding to this column name and convert it to buffer type.   */
/***********************************************************************/
void INICOL::ReadColumn(PGLOBAL g)
  {
  PTDBINI tdbp = (PTDBINI)To_Tdb;

  if (trace > 1)
    htrc("INI ReadColumn: col %s R%d flag=%d\n",
          Name, tdbp->GetTdb_No(), Flag);

  /*********************************************************************/
  /*  Get the key value from the INI file.                             */
  /*********************************************************************/
  switch (Flag) {
    case 1:
      strncpy(Valbuf, tdbp->Section, Long);              // Section name
      Valbuf[Long] = '\0';
      break;
    default:
      GetPrivateProfileString(tdbp->Section, Name, "\b",
                                        Valbuf, Long + 1, tdbp->Ifile);
      break;
    } // endswitch Flag

  // Missing keys are interpreted as null values
  if (!strcmp(Valbuf, "\b")) {
    if (Nullable)
      Value->SetNull(true);

    Value->Reset();              // Null value
  } else
    Value->SetValue_psz(Valbuf);

  } // end of ReadColumn

/***********************************************************************/
/*  WriteColumn: what this routine does is to access the last line     */
/*  read from the corresponding table, and rewrite the field           */
/*  corresponding to this column from the column buffer and type.      */
/***********************************************************************/
void INICOL::WriteColumn(PGLOBAL g)
  {
  char   *p;
  bool    rc;
  PTDBINI tdbp = (PTDBINI)To_Tdb;

  if (trace > 1)
    htrc("INI WriteColumn: col %s R%d coluse=%.4X status=%.4X\n",
          Name, tdbp->GetTdb_No(), ColUse, Status);

  /*********************************************************************/
  /*  Get the string representation of Value according to column type. */
  /*********************************************************************/
  if (Value != To_Val)
    Value->SetValue_pval(To_Val, false);    // Convert the updated value

  // Null key are missing keys
  if (Value->IsNull())
    return;

  p = Value->GetCharString(Valbuf);

  if (strlen(p) > (unsigned)Long) {
    sprintf(g->Message, MSG(VALUE_TOO_LONG), p, Name, Long);
    longjmp(g->jumper[g->jump_level], 31);
  } else if (Flag == 1) {
    if (tdbp->Mode == MODE_UPDATE) {
      strcpy(g->Message, MSG(NO_SEC_UPDATE));
      longjmp(g->jumper[g->jump_level], 31);
    } else if (*p) {
      tdbp->Section = p;
    } else
      tdbp->Section = NULL;

    return;
  } else if (!tdbp->Section) {
    strcpy(g->Message, MSG(SEC_NAME_FIRST));
    longjmp(g->jumper[g->jump_level], 31);
  } // endif's

  /*********************************************************************/
  /*  Updating must be done only when not in checking pass.            */
  /*********************************************************************/
  if (Status) {
    rc = WritePrivateProfileString(tdbp->Section, Name, p, tdbp->Ifile);
    
    if (!rc) {
      sprintf(g->Message, "Error %d writing to %s", 
                          GetLastError(), tdbp->Ifile);
      longjmp(g->jumper[g->jump_level], 31);
      } // endif rc

    } // endif Status

  } // end of WriteColumn

/* ------------------------------------------------------------------- */

/***********************************************************************/
/*  Implementation of the TDBXIN class.                                */
/***********************************************************************/
TDBXIN::TDBXIN(PINIDEF tdp) : TDBINI(tdp)
  {
  Keylist = NULL;
  Keycur = NULL;
  Keylen = Seclen;
  Oldsec = -1;
  } // end of TDBXIN constructor

TDBXIN::TDBXIN(PTDBXIN tdbp) : TDBINI(tdbp)
  {
  Keylist = tdbp->Keylist;
  Keycur = tdbp->Keycur;
  Keylen = tdbp->Keylen;
  Oldsec = tdbp->Oldsec;
  } // end of TDBXIN copy constructor

// Is this really useful ???
PTDB TDBXIN::CopyOne(PTABS t)
  {
  PTDB    tp;
  PXINCOL cp1, cp2;
  PGLOBAL g = t->G;

  tp = new(g) TDBXIN(this);

  for (cp1 = (PXINCOL)Columns; cp1; cp1 = (PXINCOL)cp1->GetNext()) {
    cp2 = new(g) XINCOL(cp1, tp);  // Make a copy
    NewPointer(t, cp1, cp2);
    } // endfor cp1

  return tp;
  } // end of CopyOne

/***********************************************************************/
/*  Get the key list from the INI file.                                */
/***********************************************************************/
char *TDBXIN::GetKeylist(PGLOBAL g, char *sec)
  {
  if (!Keylist)
    Keylist = (char*)PlugSubAlloc(g, NULL, Keylen);

  GetPrivateProfileString(sec, NULL, "", Keylist, Keylen, Ifile);
  return Keylist;
  } // end of GetKeylist

/***********************************************************************/
/*  Allocate XIN column description block.                             */
/***********************************************************************/
PCOL TDBXIN::MakeCol(PGLOBAL g, PCOLDEF cdp, PCOL cprec, int n)
  {
  return new(g) XINCOL(cdp, this, cprec, n);
  } // end of MakeCol

/***********************************************************************/
/*  XIN GetMaxSize: returns the number of sections in the XIN file.    */
/***********************************************************************/
int TDBXIN::GetMaxSize(PGLOBAL g)
  {
  if (MaxSize < 0 && GetSeclist(g)) {
    // Count the number of keys from the section list
    char *p, *k;

    for (MaxSize = 0, p = Seclist; *p; p += (strlen(p) + 1))
      for (k = GetKeylist(g, p); *k; k += (strlen(k) + 1))
        MaxSize++;

    } // endif MaxSize

  return MaxSize;
  } // end of GetMaxSize

/***********************************************************************/
/*  Record position is Section+Key.                                    */
/***********************************************************************/
int TDBXIN::GetRecpos(void)
  {
  union {
    short X[2];                              // Section and Key offsets
    int  Xpos;                              // File position
    }; // end of union

  X[0] = (short)(Section - Seclist);
  X[1] = (short)(Keycur - Keylist);
  return Xpos;
  } // end of GetRecpos

/***********************************************************************/
/*  Record position is Section+Key.                                    */
/***********************************************************************/
bool TDBXIN::SetRecpos(PGLOBAL g, int recpos)
  {
  union {
    short X[2];                              // Section and Key offsets
    int  Xpos;                              // File position
    }; // end of union

  Xpos = recpos;

  if (X[0] != Oldsec) {
    Section = Seclist + X[0];
    Keycur = GetKeylist(g, Section) + X[1];
    Oldsec = X[0];
  } else
    Keycur = Keylist + X[1];

  return false;
  } // end of SetRecpos

/***********************************************************************/
/*  XIN Access Method opening routine.                                 */
/***********************************************************************/
bool TDBXIN::OpenDB(PGLOBAL g)
  {
  Oldsec = -1;       // To replace the table at its beginning
  return TDBINI::OpenDB(g);
  } // end of OpenDB

/***********************************************************************/
/*  Data Base read routine for XIN access method.                      */
/***********************************************************************/
int TDBXIN::ReadDB(PGLOBAL g)
  {
  /*********************************************************************/
  /*  Now start the pseudo reading process.                            */
  /*********************************************************************/
#if 0               // XIN tables are not indexable
  if (To_Kindex) {
    /*******************************************************************/
    /*  Reading is by an index table.                                  */
    /*******************************************************************/
    int recpos = To_Kindex->Fetch(g);

    switch (recpos) {
      case -1:           // End of file reached
        return RC_EF;
      case -2:           // No match for join
        return RC_NF;
      case -3:           // Same record as last non null one
        return RC_OK;
      default:
        SetRecpos(g, recpos);
      } // endswitch recpos

  } else {
#endif // 0
    do {
      if (!Keycur || !*Keycur) {
        if (!Section)
          Section = Seclist;
        else
          Section += (strlen(Section) + 1);

        if (*Section)
          Keycur = GetKeylist(g, Section);
        else
          return RC_EF;

      } else
        Keycur += (strlen(Keycur) + 1);

      } while (!*Keycur);

    N++;
//} // endif To_Kindex

  return RC_OK;
  } // end of ReadDB

/***********************************************************************/
/*  WriteDB: Data Base write routine for XIN access methods.           */
/***********************************************************************/
int TDBXIN::WriteDB(PGLOBAL g)
  {
  // To check that section and key names were given when inserting
  if (Mode == MODE_INSERT) {
    Section = NULL;
    Keycur = NULL;
    } // endif Mode

  // Nothing else to do because all was done in WriteColumn
  return RC_OK;
  } // end of WriteDB

/***********************************************************************/
/*  Data Base delete line routine for XIN access methods.              */
/***********************************************************************/
int TDBXIN::DeleteDB(PGLOBAL g, int irc)
  {
  if (irc == RC_EF) {
  } else if (irc == RC_FX) {
    for (Section = Seclist; *Section; Section += (strlen(Section) + 1))
      if (!WritePrivateProfileString(Section, NULL, NULL, Ifile)) {
        sprintf(g->Message, "Error %d accessing %s", 
                            GetLastError(), Ifile);
        return RC_FX;
        } // endif

  } else if (!Section) {
    strcpy(g->Message, MSG(NO_SECTION_NAME));
    return RC_FX;
  } else
    if (!WritePrivateProfileString(Section, Keycur, NULL, Ifile)) {
      sprintf(g->Message, "Error %d accessing %s", 
                          GetLastError(), Ifile);
      return RC_FX;
      } // endif

  return RC_OK;
  } // end of DeleteDB

// ------------------------ XINCOL functions ----------------------------

/***********************************************************************/
/*  XINCOL public constructor.                                         */
/***********************************************************************/
XINCOL::XINCOL(PCOLDEF cdp, PTDB tdbp, PCOL cprec, int i, PSZ am)
      : INICOL(cdp, tdbp, cprec, i, am)
  {
  } // end of XINCOL constructor

/***********************************************************************/
/*  XINCOL constructor used for copying columns.                       */
/*  tdbp is the pointer to the new table descriptor.                   */
/***********************************************************************/
XINCOL::XINCOL(XINCOL *col1, PTDB tdbp) : INICOL(col1, tdbp)
  {
  } // end of XINCOL copy constructor

/***********************************************************************/
/*  ReadColumn: what this routine does is to access the key buffer set */
/*  from the corresponding section, extract from it the key value      */
/*  corresponding to this column name and convert it to buffer type.   */
/***********************************************************************/
void XINCOL::ReadColumn(PGLOBAL g)
  {
  PTDBXIN tdbp = (PTDBXIN)To_Tdb;

  /*********************************************************************/
  /*  Get the key value from the XIN file.                             */
  /*********************************************************************/
  switch (Flag) {
    case 1:
      strncpy(Valbuf, tdbp->Section, Long);              // Section name
      Valbuf[Long] = '\0';
      break;
    case 2:
      strncpy(Valbuf, tdbp->Keycur, Long);               // Key name
      Valbuf[Long] = '\0';
      break;
    default:
      GetPrivateProfileString(tdbp->Section, tdbp->Keycur, "",
                                        Valbuf, Long + 1, tdbp->Ifile);
      break;
    } // endswitch Flag

  Value->SetValue_psz(Valbuf);
  } // end of ReadColumn

/***********************************************************************/
/*  WriteColumn: what this routine does is to access the last line     */
/*  read from the corresponding table, and rewrite the field           */
/*  corresponding to this column from the column buffer and type.      */
/***********************************************************************/
void XINCOL::WriteColumn(PGLOBAL g)
  {
  char   *p;
  bool    rc;
  PTDBXIN tdbp = (PTDBXIN)To_Tdb;

  if (trace > 1)
    htrc("XIN WriteColumn: col %s R%d coluse=%.4X status=%.4X\n",
          Name, tdbp->GetTdb_No(), ColUse, Status);

  /*********************************************************************/
  /*  Get the string representation of Value according to column type. */
  /*********************************************************************/
  if (Value != To_Val)
    Value->SetValue_pval(To_Val, false);    // Convert the updated value

  p = Value->GetCharString(Valbuf);

  if (strlen(p) > (unsigned)Long) {
    sprintf(g->Message, MSG(VALUE_TOO_LONG), p, Name, Long);
    longjmp(g->jumper[g->jump_level], 31);
  } else if (Flag == 1) {
    if (tdbp->Mode == MODE_UPDATE) {
      strcpy(g->Message, MSG(NO_SEC_UPDATE));
      longjmp(g->jumper[g->jump_level], 31);
    } else if (*p) {
      tdbp->Section = p;
    } else
      tdbp->Section = NULL;

    return;
  } else if (Flag == 2) {
    if (tdbp->Mode == MODE_UPDATE) {
      strcpy(g->Message, MSG(NO_KEY_UPDATE));
      longjmp(g->jumper[g->jump_level], 31);
    } else if (*p) {
      tdbp->Keycur = p;
    } else
      tdbp->Keycur = NULL;

    return;
  } else if (!tdbp->Section || !tdbp->Keycur) {
    strcpy(g->Message, MSG(SEC_KEY_FIRST));
    longjmp(g->jumper[g->jump_level], 31);
  } // endif's

  /*********************************************************************/
  /*  Updating must be done only when not in checking pass.            */
  /*********************************************************************/
  if (Status) {
    rc = WritePrivateProfileString(tdbp->Section, tdbp->Keycur, p, tdbp->Ifile);
    
    if (!rc) {
      sprintf(g->Message, "Error %d writing to %s", 
                          GetLastError(), tdbp->Ifile);
      longjmp(g->jumper[g->jump_level], 31);
      } // endif rc

    } // endif Status

  } // end of WriteColumn

/* ------------------------ End of System ---------------------------- */


