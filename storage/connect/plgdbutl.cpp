/********** PlgDBUtl Fpe C++ Program Source Code File (.CPP) ***********/
/* PROGRAM NAME: PLGDBUTL                                              */
/* -------------                                                       */
/*  Version 3.7                                                        */
/*                                                                     */
/* COPYRIGHT:                                                          */
/* ----------                                                          */
/*  (C) Copyright to the author Olivier BERTRAND          1998-2012    */
/*                                                                     */
/* WHAT THIS PROGRAM DOES:                                             */
/* -----------------------                                             */
/*  Utility functions used by DB semantic routines.                    */
/*                                                                     */
/* WHAT YOU NEED TO COMPILE THIS PROGRAM:                              */
/* --------------------------------------                              */
/*                                                                     */
/*  REQUIRED FILES:                                                    */
/*  ---------------                                                    */
/*  See Readme.C for a list and description of required SYSTEM files.  */
/*                                                                     */
/*    PLGDBUTL.C     - Source code                                     */
/*    GLOBAL.H       - Global declaration file                         */
/*    PLGDBSEM.H     - DB application declaration file                 */
/*                                                                     */
/*  REQUIRED LIBRARIES:                                                */
/*  -------------------                                                */
/*    OS2.LIB        - OS2 libray                                      */
/*    LLIBCE.LIB     - Protect mode/standard combined large model C    */
/*                     library                                         */
/*                                                                     */
/*  REQUIRED PROGRAMS:                                                 */
/*  ------------------                                                 */
/*    IBM, MS, Borland or GNU C++ Compiler                             */
/*    IBM, MS, Borland or GNU Linker                                   */
/***********************************************************************/

/***********************************************************************/
/*  Include relevant MariaDB header file.                              */
/***********************************************************************/
#include "my_global.h"
#if defined(WIN32)
#include <io.h>
#include <fcntl.h>
#include <errno.h>
#define BIGMEM         1048576            // 1 Megabyte
#else     // !WIN32
#include <unistd.h>
#include <fcntl.h>
#if defined(THREAD)
#include <pthread.h>
#endif   // THREAD
#include <stdarg.h>
#define BIGMEM      2147483647            // Max int value
#endif    // !WIN32
#include <locale.h>

/***********************************************************************/
/*  Include application header files                                   */
/***********************************************************************/
#include "global.h"    // header containing all global declarations.
#include "plgdbsem.h"  // header containing the DB applic. declarations.
#include "preparse.h"  // For DATPAR
#include "osutil.h"
#include "maputil.h"
#include "catalog.h"
#include "colblk.h"
#include "xtable.h"    // header of TBX, TDB and TDBASE classes
#include "tabcol.h"    // header of XTAB and COLUMN classes

/***********************************************************************/
/*  Macro or external routine definition                               */
/***********************************************************************/
#if defined(THREAD)
#if defined(WIN32)
extern CRITICAL_SECTION parsec;      // Used calling the Flex parser
#else   // !WIN32
extern pthread_mutex_t parmut;
#endif  // !WIN32
#endif  //  THREAD

#define PLGINI      "plugdb.ini"       /* Configuration settings file  */
#define PLGXINI     "plgcnx.ini"       /* Configuration settings file  */

/***********************************************************************/
/*  DB static variables.                                               */
/***********************************************************************/
bool  Initdone = false;
bool  plugin = false;  // True when called by the XDB plugin handler 

extern "C" {
       char  plgxini[_MAX_PATH] = PLGXINI;
       char  plgini[_MAX_PATH] = PLGINI;
#if defined(WIN32)
       char  nmfile[_MAX_PATH] = ".\\Log\\plugdb.out";
       char  pdebug[_MAX_PATH] = ".\\Log\\plgthread.out";

       HINSTANCE s_hModule;           // Saved module handle
#else   // !WIN32
       char  nmfile[_MAX_PATH] = "./Log/plugdb.out";
       char  pdebug[_MAX_PATH] = "./Log/plgthread.out";
#endif  // !WIN32

#if defined(XMSG)
       char  msglang[16] = "ENGLISH";      // Default language
#endif
} // extern "C"

extern "C" int  trace;
extern "C" char version[];

// The debug trace used by the main thread
       FILE *pfile = NULL;

MBLOCK Nmblk = {NULL, false, 0, false, NULL};   // Used to init MBLOCK's

/***********************************************************************/
/*  Routines called externally and internally by utility routines.     */
/***********************************************************************/
bool PlugEvalLike(PGLOBAL, LPCSTR, LPCSTR, bool);
bool EvalLikePattern(LPCSTR, LPCSTR);
void PlugConvertConstant(PGLOBAL, void* &, short&);

#ifdef DOMDOC_SUPPORT
void CloseXMLFile(PGLOBAL, PFBLOCK, bool);
#endif   // DOMDOC_SUPPORT

#ifdef LIBXML2_SUPPORT
void CloseXML2File(PGLOBAL, PFBLOCK, bool);
#endif   // LIBXML2_SUPPORT


/***********************************************************************/
/* Routines for file IO with error reporting to g->Message             */
/***********************************************************************/
static void
global_open_error_msg(GLOBAL *g, int msgid, const char *path, const char *mode)
{
  int len;
  switch (msgid)
  {
    case MSGID_CANNOT_OPEN:
      len= snprintf(g->Message, sizeof(g->Message) - 1,
                    MSG(CANNOT_OPEN), // Cannot open %s
                    path);
      break;

    case MSGID_OPEN_MODE_ERROR:
      len= snprintf(g->Message, sizeof(g->Message) - 1,
                    MSG(OPEN_MODE_ERROR), // "Open(%s) error %d on %s"
                    mode, (int) errno, path);
      break;

    case MSGID_OPEN_MODE_STRERROR:
      len= snprintf(g->Message, sizeof(g->Message) - 1,
                    MSG(OPEN_MODE_ERROR) ": %s", // Open(%s) error %d on %s: %s
                    mode, (int) errno, path, strerror(errno));
      break;

    case MSGID_OPEN_STRERROR:
      len= snprintf(g->Message, sizeof(g->Message) - 1,
                    MSG(OPEN_STRERROR), // "open error: %s"
                    strerror(errno));
      break;

    case MSGID_OPEN_ERROR_AND_STRERROR:
      len= snprintf(g->Message, sizeof(g->Message) - 1,
                    //OPEN_ERROR does not work, as it wants mode %d (not %s)
                    //MSG(OPEN_ERROR) "%s",// "Open error %d in mode %d on %s: %s"
                    "Open error %d in mode %s on %s: %s",
                    errno, mode, path, strerror(errno));
      break;

    case MSGID_OPEN_EMPTY_FILE:
      len= snprintf(g->Message, sizeof(g->Message) - 1,
                    MSG(OPEN_EMPTY_FILE), // "Opening empty file %s: %s"
                    path, strerror(errno));
    default:
      DBUG_ASSERT(0);
      /* Fall through*/
    case 0:
      len= 0;
  }
  g->Message[len]= '\0';
}


FILE *global_fopen(GLOBAL *g, int msgid, const char *path, const char *mode)
{
  FILE *f;
  if (!(f= fopen(path, mode)))
    global_open_error_msg(g, msgid, path, mode);
  return f;
}


int global_open(GLOBAL *g, int msgid, const char *path, int flags)
{
  int h;
  if ((h= open(path, flags)) <= 0)
    global_open_error_msg(g, msgid, path, "");
  return h;
}


int global_open(GLOBAL *g, int msgid, const char *path, int flags, int mode)
{
  int h;
  if ((h= open(path, flags, mode)) <= 0)
  {
    char modestr[64];
    snprintf(modestr, sizeof(modestr), "%d", mode);
    global_open_error_msg(g, msgid, path, modestr);
  }
  return h;
}


/**************************************************************************/
/*  Utility for external callers (such as XDB)                            */
/**************************************************************************/
DllExport char *GetIni(int n = 0)
  {
  switch (n) {
    case 1: return plgxini; break;
    case 2: return nmfile;  break;
    case 3: return pdebug;  break;
    case 4: return version; break;
#if defined(XMSG)
    case 5: return msglang; break;
#endif   // XMSG
//  default: return plgini;
    } // endswitch GetIni

  return plgini;
  } // end of GetIni

DllExport void SetTrc(void)
  {
  // If tracing is on, debug must be initialized.
  debug = pfile;
  } // end of SetTrc

#if 0
/**************************************************************************/
/*  Tracing output function.                                              */
/**************************************************************************/
void ptrc(char const *fmt, ...)
  {
  va_list ap;
  va_start (ap, fmt);

//  if (trace == 0 || (trace == 1 && !pfile) || !fmt)
//    printf("In %s wrong trace=%d pfile=%p fmt=%p\n", 
//      __FILE__, trace, pfile, fmt);

  if (trace == 1)
    vfprintf(pfile, fmt, ap);
  else
    vprintf(fmt, ap);

  va_end (ap);
  } // end of ptrc
#endif // 0

/***********************************************************************/
/*  Allocate and initialize the new DB User Block.                     */
/***********************************************************************/
PDBUSER PlgMakeUser(PGLOBAL g)
  {
  PDBUSER dbuserp;

  if (!(dbuserp = (PDBUSER)PlugAllocMem(g, (uint)sizeof(DBUSERBLK)))) {
    sprintf(g->Message, MSG(MALLOC_ERROR), "PlgMakeUser");
    return NULL;
    } // endif dbuserp

  memset(dbuserp, 0, sizeof(DBUSERBLK));
//dbuserp->Act2 = g->Activityp;
//#if defined(UNIX)
//  dbuserp->LineLen = 160;
//#else
//  dbuserp->LineLen = 78;
//#endif
//dbuserp->Maxres = MAXRES;
//dbuserp->Maxlin = MAXLIN;
//dbuserp->Maxbmp = MAXBMP;
//dbuserp->AlgChoice = AMOD_AUTO;
  dbuserp->UseTemp = TMP_AUTO;
  dbuserp->Check = CHK_ALL;
  strcpy(dbuserp->Server, "CONNECT");
  return dbuserp;
  } // end of PlgMakeUser

/***********************************************************************/
/*  PlgGetUser: returns DBUSER block pointer.                          */
/***********************************************************************/
PDBUSER PlgGetUser(PGLOBAL g)
  {
  PDBUSER dup = (PDBUSER)((g->Activityp) ? g->Activityp->Aptr : NULL);

  if (!dup)
    strcpy(g->Message, MSG(APPL_NOT_INIT));

  return dup;
  } // end of PlgGetUser

/***********************************************************************/
/*  PlgGetCatalog: returns CATALOG class pointer.                      */
/***********************************************************************/
PCATLG PlgGetCatalog(PGLOBAL g, bool jump)
  {
  PDBUSER dbuserp = PlgGetUser(g);
  PCATLG  cat = (dbuserp) ? dbuserp->Catalog : NULL;

  if (!cat && jump) {
    // Raise exception so caller doesn't have to check return value
    strcpy(g->Message, MSG(NO_ACTIVE_DB));
    longjmp(g->jumper[g->jump_level], 1);
    } // endif cat

  return cat;
  } // end of PlgGetCatalog

/***********************************************************************/
/*  PlgGetCatalog: returns CATALOG class pointer.                      */
/***********************************************************************/
char *PlgGetDataPath(PGLOBAL g)
  {
  PCATLG cat = PlgGetCatalog(g, false);

  if (!cat)
    return GetIniString(g, NULL, "DataBase", "DataPath", "", plgini);

  return cat->GetDataPath();
  } // end of PlgGetDataPath

/***********************************************************************/
/*  PlgGetXdbPath: sets the fully qualified file name of a database    */
/*  description file in lgn and the new datapath in dp.                */
/*  New database description file is a Configuration Settings file     */
/*  that will be used and updated in case of DB modifications such     */
/*  as Insert into a VCT file. Look for it and use it if found.        */
/*  By default the configuration file is DataPath\name.xdb but the     */
/*  configuration file name may also be specified in Plugdb.ini.       */
/***********************************************************************/
bool PlgSetXdbPath(PGLOBAL g, PSZ dbname, PSZ dbpath,
                              char *lgn,  int lgsize,
                              char *path, int psize)
  {
  char *dp, datapath[_MAX_PATH], ft[_MAX_EXT] = ".xdb";
  int   n;

  if (path) {
    dp = path;
    n = psize;
  } else {
    dp = datapath;
    n = sizeof(datapath);
  } // endif path

  GetPrivateProfileString("DataBase", "DataPath", "", dp, n, plgini);

  if (trace)
    htrc("PlgSetXdbPath: path=%s\n", dp);

  if (dbpath) {
    char fn[_MAX_FNAME];

    strcpy(lgn, dbpath);
    _splitpath(lgn, NULL, NULL, fn, NULL);

    if (!*fn)       // Old style use command
      strcat(lgn, dbname);

    _splitpath(lgn, NULL, NULL, dbname, NULL);  // Extract DB name
  } else if (strcspn(dbname, ":/\\.") < strlen(dbname)) {
    // dbname name contains the path name of the XDB file
    strcpy(lgn, dbname);
    _splitpath(lgn, NULL, NULL, dbname, NULL);  // Extract DB name
  } else
    /*******************************************************************/
    /*  New database description file is a Configuration Settings file */
    /*  that will be used and updated in case of DB modifications such */
    /*  as Insert into a VCT file. Look for it and use it if found.    */
    /*  By default the configuration file is DataPath\name.xdb but the */
    /*  configuration file name may also be specified in Plugdb.ini.   */
    /*******************************************************************/
    GetPrivateProfileString("DBnames", dbname, "", lgn, lgsize, plgini);

  if (*lgn) {
#if !defined(UNIX)
    char drive[_MAX_DRIVE];
    char direc[_MAX_DIR];
#endif
    char fname[_MAX_FNAME];
    char ftype[_MAX_EXT];

    _splitpath(lgn, NULL, NULL, fname, ftype);

    if (!*ftype)
      strcat(lgn, ft);
    else if (!stricmp(ftype, ".var")) {
      strcpy(g->Message, MSG(NO_MORE_VAR));
      return true;
      } // endif ftype

    // Given DB description path may be relative to data path
    PlugSetPath(lgn, lgn, dp);

    // New data path is the path of the configuration setting file
#if !defined(UNIX)
    _splitpath(lgn, drive, direc, NULL, NULL);
    _makepath(dp, drive, direc, "", "");
#else
//#error This must be tested for trailing slash
    _splitpath(lgn, NULL, dp, NULL, NULL);
#endif
  } else {
    // Try dbname[.ext] in the current directory
    strcpy(lgn, dbname);

    if (!strchr(dbname, '.'))
      strcat(lgn, ft);

    PlugSetPath(lgn, lgn, dp);
  } // endif lgn

  if (trace)
    htrc("PlgSetXdbPath: new DB description file=%s\n", lgn);

  return false;
  } // end of PlgSetXdbPath

/***********************************************************************/
/*  Extract from a path name the required component.                   */
/*  This function assumes there is enough space in the buffer.         */
/***********************************************************************/
char *ExtractFromPath(PGLOBAL g, char *pBuff, char *FileName, OPVAL op)
  {
  char *drive = NULL, *direc = NULL, *fname = NULL, *ftype = NULL;

  switch (op) {           // Determine which part to extract
#if !defined(UNIX)
    case OP_FDISK: drive = pBuff; break;
#endif   // !UNIX
    case OP_FPATH: direc = pBuff; break;
    case OP_FNAME: fname = pBuff; break;
    case OP_FTYPE: ftype = pBuff; break;
    default:
      sprintf(g->Message, MSG(INVALID_OPER), op, "ExtractFromPath");
      return NULL;
    } // endswitch op

  // Now do the extraction
  _splitpath(FileName, drive, direc, fname, ftype);
  return pBuff;
  } // end of PlgExtractFromPath

/***********************************************************************/
/*  Check the occurence and matching of a pattern against a string.    */
/*  Because this function is only used for catalog name checking,      */
/*  it must be case insensitive.                                       */
/***********************************************************************/
bool PlugCheckPattern(PGLOBAL g, LPCSTR string, LPCSTR pat)
  {
  if (pat && strlen(pat)) {
    // This leaves 512 bytes (MAX_STR / 2) for each components
    LPSTR name = g->Message + MAX_STR / 2;

    strlwr(strcpy(name, string));
    strlwr(strcpy(g->Message, pat));         // Can be modified by Eval
    return EvalLikePattern(name, g->Message);
  } else
    return true;

  } // end of PlugCheckPattern

/***********************************************************************/
/*  PlugEvalLike: evaluates a LIKE clause.                             */
/*  Syntaxe: M like P escape C. strg->M, pat->P, C not implemented yet */
/***********************************************************************/
bool PlugEvalLike(PGLOBAL g, LPCSTR strg, LPCSTR pat, bool ci)
  {
  char *tp, *sp;
  bool  b;

  if (trace)
    htrc("LIKE: strg='%s' pattern='%s'\n", strg, pat);

  if (ci) {                        /* Case insensitive test             */
    if (strlen(pat) + strlen(strg) + 1 < MAX_STR)
      tp = g->Message;
    else if (!(tp = new char[strlen(pat) + strlen(strg) + 2])) {
      strcpy(g->Message, MSG(NEW_RETURN_NULL));
      longjmp(g->jumper[g->jump_level], OP_LIKE);
      } /* endif tp */
    
    sp = tp + strlen(pat) + 1;
    strlwr(strcpy(tp, pat));      /* Make a lower case copy of pat     */
    strlwr(strcpy(sp, strg));     /* Make a lower case copy of strg    */
  } else {                        /* Case sensitive test               */
    if (strlen(pat) < MAX_STR)    /* In most of the case for small pat */
      tp = g->Message;            /* Use this as temporary work space. */
    else if (!(tp = new char[strlen(pat) + 1])) {
      strcpy(g->Message, MSG(NEW_RETURN_NULL));
      longjmp(g->jumper[g->jump_level], OP_LIKE);
      } /* endif tp */
    
    strcpy(tp, pat);                  /* Make a copy to be worked into */
    sp = (char*)strg;
  } /* endif ci */

  b = EvalLikePattern(sp, tp);

  if (tp != g->Message)               /* If working space was obtained */
    delete [] tp;                     /* by the use of new, delete it. */

  return (b);
  } /* end of PlugEvalLike */

/***********************************************************************/
/*  M and P are variable length character string. If M and P are zero  */
/*  length strings then the Like predicate is true.                    */
/*                                                                     */
/*  The Like predicate is true if:                                     */
/*                                                                     */
/*  1- A subtring of M is a sequence of 0 or more contiguous <CR> of M */
/*     and each <CR> of M is part of exactly one substring.            */
/*                                                                     */
/*  2- If the i-th <subtring-specifyer> of P is an <arbitrary-char-    */
/*     specifier>, the i-th subtring of M is any single <CR>.          */
/*                                                                     */
/*  3- If the i-th <subtring-specifyer> of P is an <arbitrary-string-  */
/*     specifier>, then the i-th subtring of M is any sequence of zero */
/*     or more <CR>.                                                   */
/*                                                                     */
/*  4- If the i-th <subtring-specifyer> of P is neither an <arbitrary- */
/*     character-specifier> nor an <arbitrary-string-specifier>, then  */
/*     the i-th substring of M is equal to that <substring-specifier>  */
/*     according to the collating sequence of the <like-predicate>,    */
/*     without the appending of <space-character>, and has the same    */
/*     length as that <substring-specifier>.                           */
/*                                                                     */
/*  5- The number of substrings of M is equal to the number of         */
/*     <subtring-specifiers> of P.                                     */
/*                                                                     */
/*  Otherwise M like P is false.                                       */
/***********************************************************************/
bool EvalLikePattern(LPCSTR sp, LPCSTR tp)
  {
  LPSTR p;
  char  c;
  int   n;
  bool  b, t = false;

  if (trace)
    htrc("Eval Like: sp=%s tp=%s\n", 
         (sp) ? sp : "Null", (tp) ? tp : "Null");

  /********************************************************************/
  /*  If pattern is void, Like is true only if string is also void.   */
  /********************************************************************/
  if (!*tp)
    return (!*sp);

  /********************************************************************/
  /*  Analyse eventual arbitrary specifications ahead of pattern.     */
  /********************************************************************/
  for (p = (LPSTR)tp; p;)
    switch (*p) {                     /*   it can contain % and/or _   */
      case '%':                       /* An % has been found           */
        t = true;                     /* Note eventual character skip  */
        p++;
        break;
      case '_':                       /* An _ has been found           */
        if (*sp) {                    /* If more character in string   */
          sp++;                       /*   skip it                     */
          p++;
        } else
          return false;               /* Like condition is not met     */

        break;
      default:
        tp = p;                       /* Point to rest of template     */
        p = NULL;                     /* To stop For loop              */
        break;
      } /* endswitch */

  if ((p = (LPSTR)strpbrk(tp, "%_"))) /* Get position of next % or _   */
    n = p - tp;
  else
    n = strlen(tp);                   /* Get length of pattern head    */

  if (trace)
    htrc(" testing: t=%d sp=%s tp=%s p=%p\n", t, sp, tp, p);

  if (n > (signed)strlen(sp))         /* If head is longer than strg   */
    b = false;                        /* Like condition is not met     */
  else if (n == 0)                    /* If void <substring-specifier> */
    b = (t || !*sp);                  /*   true if %  or void strg.    */
  else if (!t) {
    /*******************************************************************/
    /*  No character to skip, check occurence of <subtring-specifier>  */
    /*  at the very beginning of remaining string.                     */
    /*******************************************************************/
    if (p) {
      if ((b = !strncmp(sp, tp, n)))
        b = EvalLikePattern(sp + n, p);

    } else
      b = !strcmp(sp, tp);            /*   strg and tmp heads match    */

  } else
    if (p)
      /*****************************************************************/
      /*  Here is the case explaining why we need a recursive routine. */
      /*  The test must be done not only against the first occurence   */
      /*  of the <substring-specifier> in the remaining string,        */
      /*  but also with all eventual succeeding ones.                  */
      /*****************************************************************/
      for (b = false, c = *p; !b && (signed)strlen(sp) >= n; sp++) {
        *p = '\0';                    /* Separate pattern header       */

        if ((sp = strstr(sp, tp))) {
          *p = c;
          b = EvalLikePattern(sp + n, p);
        } else {
          *p = c;
          b = false;
          break;
        } /* endif s */

        } /* endfor b, sp */

    else {
      sp += (strlen(sp) - n);
      b = !strcmp(sp, tp);
    } /* endif p */

  if (trace)
    htrc(" done: b=%d n=%d sp=%s tp=%s\n",
          b, n, (sp) ? sp : "Null", tp);

  return (b);
  } /* end of EvalLikePattern */

/***********************************************************************/
/*  PlugConvertConstant: convert a Plug constant to an Xobject.        */
/***********************************************************************/
void PlugConvertConstant(PGLOBAL g, void* & value, short& type)
  {
  if (trace)
    htrc("PlugConvertConstant: value=%p type=%hd\n", value, type);

  if (type != TYPE_XOBJECT) {
    value = new(g) CONSTANT(g, value, type);
    type = TYPE_XOBJECT;
    } // endif type

  } // end of PlugConvertConstant

/***********************************************************************/
/*  Call the Flex preparser to convert a date format to a sscanf input */
/*  format and a Strftime output format. Flag if not 0 indicates that  */
/*  non quoted blanks are not included in the output format.           */
/***********************************************************************/
PDTP MakeDateFormat(PGLOBAL g, PSZ dfmt, bool in, bool out, int flag)
  {
  PDTP pdp = (PDTP)PlugSubAlloc(g, NULL, sizeof(DATPAR));

  if (trace)
    htrc("MakeDateFormat: dfmt=%s\n", dfmt);

  memset(pdp, 0, sizeof(DATPAR));
  pdp->Format = pdp->Curp = dfmt;
  pdp->Outsize = 2 * strlen(dfmt) + 1;

  if (in)
    pdp->InFmt = (char*)PlugSubAlloc(g, NULL, pdp->Outsize);

  if (out)
    pdp->OutFmt = (char*)PlugSubAlloc(g, NULL, pdp->Outsize);

  pdp->Flag = flag;

  /*********************************************************************/
  /* Call the FLEX generated parser. In multi-threading mode the next  */
  /* instruction is included in an Enter/LeaveCriticalSection bracket. */
  /*********************************************************************/
#if defined(THREAD)
#if defined(WIN32)
  EnterCriticalSection((LPCRITICAL_SECTION)&parsec);
#else   // !WIN32
  pthread_mutex_lock(&parmut);
#endif  // !WIN32
#endif  //  THREAD
  /*int rc =*/ fmdflex(pdp);
#if defined(THREAD)
#if defined(WIN32)
  LeaveCriticalSection((LPCRITICAL_SECTION)&parsec);
#else   // !WIN32
  pthread_mutex_unlock(&parmut);
#endif  // !WIN32
#endif  //  THREAD

  if (trace)
    htrc("Done:  in=%s out=%s\n", SVP(pdp->InFmt), SVP(pdp->OutFmt));           
  return pdp;
  } // end of MakeDateFormat

/***********************************************************************/
/* Extract the date from a formatted string according to format.       */
/***********************************************************************/
int ExtractDate(char *dts, PDTP pdp, int defy, int val[6])
  {
  char *fmt, c, d, e, W[8][12];
  int   i, k, m, numval;
  int   n, y = 30;

  if (pdp)
    fmt = pdp->InFmt;
  else            // assume standard MySQL date format
    fmt = "%4d-%2d-%2d %2d:%2d:%2d";

  if (trace)
    htrc("ExtractDate: dts=%s fmt=%s defy=%d\n", dts, fmt, defy);

  // Set default values for time only use
  if (defy) {
    // This may be a default value for year
    y = defy;
    val[0] = y;
    y = (y < 100) ? y : 30;
  } else
    val[0] = 70;

  val[1] = 1;
  val[2] = 1;

  for (i = 3; i < 6; i++)
    val[i] = 0;

  numval = 0;

  // Get the date field parse it with derived input format
  m = sscanf(dts, fmt, W[0], W[1], W[2], W[3], W[4], W[5], W[6], W[7]);

  if (m > pdp->Num)
    m = pdp->Num;

  for (i = 0; i < m; i++) {
    n = *(int*)W[i];

    switch (k = pdp->Index[i]) {
      case 0:
        if (n < y)
          n += 100;

        val[0] = n;
        numval = max(numval, 1);
        break;
      case 1:
      case 2:
      case 3:
      case 4:
      case 5:
        val[k] = n;
        numval = max(numval, k + 1);
        break;
      case -1:
        c = toupper(W[i][0]);
        d = toupper(W[i][1]);
        e = toupper(W[i][2]);

        switch (c) {
          case 'J':
            n = (d == 'A') ? 1
              : (e == 'N') ? 6 : 7; break;
          case 'F': n =  2; break;
          case 'M':
            n = (e == 'R') ? 3 : 5; break;
          case 'A':
            n = (d == 'P') ? 4 : 8; break;
            break;
          case 'S': n =  9; break;
          case 'O': n = 10; break;
          case 'N': n = 11; break;
          case 'D': n = 12; break;
          } /* endswitch c */

        val[1] = n;
        numval = max(numval, 2);
        break;
      case -6:
        c = toupper(W[i][0]);
        n = val[3] % 12;

        if (c == 'P')
          n += 12;

        val[3] = n;
        break;
      } // endswitch Plugpar

    } // endfor i

  if (trace)
    htrc("numval=%d val=(%d,%d,%d,%d,%d,%d)\n",
          numval, val[0], val[1], val[2], val[3], val[4], val[5]); 

  return numval;
  } // end of ExtractDate

/***********************************************************************/
/*  Open file routine: the purpose of this routine is to make a list   */
/*  of all open file so they can be closed in SQLINIT on error jump.   */
/***********************************************************************/
FILE *PlugOpenFile(PGLOBAL g, LPCSTR fname, LPCSTR ftype)
  {
  FILE     *fop;
  PFBLOCK   fp;
  PDBUSER   dbuserp = (PDBUSER)g->Activityp->Aptr;

  if (trace) {
    htrc("PlugOpenFile: fname=%s ftype=%s\n", fname, ftype);
    htrc("dbuserp=%p\n", dbuserp);
    } // endif trace

  if ((fop= global_fopen(g, MSGID_OPEN_MODE_STRERROR, fname, ftype)) != NULL) {
    if (trace)
      htrc(" fop=%p\n", fop);

    fp = (PFBLOCK)PlugSubAlloc(g, NULL, sizeof(FBLOCK));

    if (trace)
      htrc(" fp=%p\n", fp);

    // fname may be in volatile memory such as stack
    fp->Fname = (char*)PlugSubAlloc(g, NULL, strlen(fname) + 1);
    strcpy((char*)fp->Fname, fname);
    fp->Count = 1;
    fp->Type = TYPE_FB_FILE;
    fp->File = fop;
    fp->Mode = MODE_ANY;                        // ???
    fp->Next = dbuserp->Openlist;
    dbuserp->Openlist = fp;
    } /* endif fop */

  if (trace)
    htrc(" returning fop=%p\n", fop);

  return (fop);
  } // end of PlugOpenFile

/***********************************************************************/
/*  Close file routine: the purpose of this routine is to avoid        */
/*  double closing that freeze the system on some Unix platforms.      */
/***********************************************************************/
int PlugCloseFile(PGLOBAL g, PFBLOCK fp, bool all)
  {
  int rc = 0;

  if (trace)
    htrc("PlugCloseFile: fp=%p count=%hd type=%hd\n",
          fp, ((fp) ? fp->Count : 0), ((fp) ? fp->Type : 0));

  if (!fp || !fp->Count)
    return rc;

  switch (fp->Type) {
    case TYPE_FB_FILE:
      if (fclose((FILE *)fp->File) == EOF)
        rc = errno;

      fp->File = NULL;
      fp->Mode = MODE_ANY;
      fp->Count = 0;
      break;
    case TYPE_FB_MAP:
      if ((fp->Count = (all) ? 0 : fp->Count - 1))
        break;

      if (CloseMemMap(fp->Memory, fp->Length))
        rc = (int)GetLastError();

      fp->Memory = NULL;
      fp->Mode = MODE_ANY;
      // Passthru
    case TYPE_FB_HANDLE:
      if (fp->Handle && fp->Handle != INVALID_HANDLE_VALUE)
        if (CloseFileHandle(fp->Handle))
          rc = (rc) ? rc : (int)GetLastError();

      fp->Handle = INVALID_HANDLE_VALUE;
      fp->Mode = MODE_ANY;
      fp->Count = 0;
      break;
#ifdef DOMDOC_SUPPORT
    case TYPE_FB_XML:
      CloseXMLFile(g, fp, all);
      break;
#endif   // DOMDOC_SUPPORT
#ifdef LIBXML2_SUPPORT
    case TYPE_FB_XML2:
      CloseXML2File(g, fp, all);
      break;
#endif   // LIBXML2_SUPPORT
    default:
      rc = RC_FX;
    } // endswitch Type

  return rc;
  } // end of PlugCloseFile

/***********************************************************************/
/*  PlugCleanup: Cleanup remaining items of a SQL query.               */
/***********************************************************************/
void PlugCleanup(PGLOBAL g, bool dofree)
  {
  PCATLG  cat;
  PDBUSER dbuserp = (PDBUSER)g->Activityp->Aptr;

  // The test on Catalog is to avoid a Windows bug that can make
  // LoadString in PlugGetMessage to fail in some case
  if (!dbuserp || !(cat = dbuserp->Catalog))
    return;

  /*********************************************************************/
  /*  Close eventually still open/mapped files.                        */
  /*********************************************************************/
  for (PFBLOCK fp = dbuserp->Openlist; fp; fp = fp->Next)
    PlugCloseFile(g, fp, true);

  dbuserp->Openlist = NULL;

  if (dofree) {
    /*******************************************************************/
    /*  Cleanup any non suballocated memory still not freed.           */
    /*******************************************************************/
    for (PMBLOCK mp = dbuserp->Memlist; mp; mp = mp->Next)
      PlgDBfree(*mp);

    dbuserp->Memlist = NULL;

    /*******************************************************************/
    /*  If not using permanent storage catalog, reset volatile values. */
    /*******************************************************************/
    cat->Reset();

    /*******************************************************************/
    /*  This is the place to reset the pointer on domains.             */
    /*******************************************************************/
    dbuserp->Subcor = false;
    dbuserp->Step = STEP(PARSING_QUERY);
    dbuserp->ProgMax = dbuserp->ProgCur = dbuserp->ProgSav = 0;
    } // endif dofree

  } // end of PlugCleanup

/***********************************************************************/
/*  That stupid Windows 98 does not provide this function.             */
/***********************************************************************/
bool WritePrivateProfileInt(LPCSTR sec, LPCSTR key, int n, LPCSTR ini)
  {
  char buf[12];

  sprintf(buf, "%d", n);
  return WritePrivateProfileString(sec, key, buf, ini);
  } // end of WritePrivateProfileInt

/***********************************************************************/
/*  Retrieve a size from an INI file with eventual K or M following.   */
/***********************************************************************/
int GetIniSize(char *section, char *key, char *def, char *ini)
  {
  char c, buff[32];
  int  i;
  int  n = 0;

  GetPrivateProfileString(section, key, def, buff, sizeof(buff), ini);

  if ((i = sscanf(buff, " %d %c ", &n, &c)) == 2)
    switch (toupper(c)) {
      case 'M':
        n *= 1024;
      case 'K':
        n *= 1024;
      } // endswitch c

  if (trace)
    htrc("GetIniSize: key=%s buff=%s i=%d n=%d\n", key, buff, i, n);

  return n;
  } // end of GetIniSize

/***********************************************************************/
/* Allocate a string retrieved from an INI file and return its address */
/***********************************************************************/
DllExport PSZ GetIniString(PGLOBAL g, void *mp, LPCSTR sec, LPCSTR key,
                                                LPCSTR def, LPCSTR ini)
  {
  char  buff[_MAX_PATH];
  PSZ   p;
  int   n, m = sizeof(buff);
  char *buf = buff;

#if defined(_DEBUG)
  assert (sec && key);
#endif

 again:
  n = GetPrivateProfileString(sec, key, def, buf, m, ini);

  if (n == m - 1) {
    // String may have been truncated, make sure to have all
    if (buf != buff)
      delete [] buf;

    m *= 2;
    buf = new char[m];
    goto again;
    } // endif n

  p = (PSZ)PlugSubAlloc(g, mp, n + 1);

  if (trace)
    htrc("GetIniString: sec=%s key=%s buf=%s\n", sec, key, buf);

  strcpy(p, buf);

  if (buf != buff)
    delete [] buf;

  return p;
  } // end of GetIniString

/***********************************************************************/
/*  GetAmName: return the name correponding to an AM code.             */
/***********************************************************************/
char *GetAmName(PGLOBAL g, AMT am, void *memp)
  {
  char *amn= (char*)PlugSubAlloc(g, memp, 16);

  switch (am) {
    case TYPE_AM_ERROR: strcpy(amn, "ERROR"); break;
    case TYPE_AM_ROWID: strcpy(amn, "ROWID"); break;
    case TYPE_AM_FILID: strcpy(amn, "FILID"); break;
    case TYPE_AM_VIEW:  strcpy(amn, "VIEW");  break;
    case TYPE_AM_COUNT: strcpy(amn, "COUNT"); break;
    case TYPE_AM_DCD:   strcpy(amn, "DCD");   break;
    case TYPE_AM_CMS:   strcpy(amn, "CMS");   break;
    case TYPE_AM_MAP:   strcpy(amn, "MAP");   break;
    case TYPE_AM_FMT:   strcpy(amn, "FMT");   break;
    case TYPE_AM_CSV:   strcpy(amn, "CSV");   break;
    case TYPE_AM_MCV:   strcpy(amn, "MCV");   break;
    case TYPE_AM_DOS:   strcpy(amn, "DOS");   break;
    case TYPE_AM_FIX:   strcpy(amn, "FIX");   break;
    case TYPE_AM_BIN:   strcpy(amn, "BIN");   break;
    case TYPE_AM_VCT:   strcpy(amn, "VEC");   break;
    case TYPE_AM_VMP:   strcpy(amn, "VMP");   break;
    case TYPE_AM_DBF:   strcpy(amn, "DBF");   break;
    case TYPE_AM_QRY:   strcpy(amn, "QRY");   break;
    case TYPE_AM_SQL:   strcpy(amn, "SQL");   break;
    case TYPE_AM_PLG:   strcpy(amn, "PLG");   break;
    case TYPE_AM_PLM:   strcpy(amn, "PLM");   break;
    case TYPE_AM_DOM:   strcpy(amn, "DOM");   break;
    case TYPE_AM_DIR:   strcpy(amn, "DIR");   break;
    case TYPE_AM_ODBC:  strcpy(amn, "ODBC");  break;
    case TYPE_AM_MAC:   strcpy(amn, "MAC");   break;
    case TYPE_AM_OEM:   strcpy(amn, "OEM");   break;
    case TYPE_AM_OUT:   strcpy(amn, "OUT");   break;
    default:           sprintf(amn, "OEM(%d)", am);
    } // endswitch am

  return amn;
  } // end of GetAmName

#if defined(WIN32) && !defined(NOCATCH)
/***********************************************************************/
/*  GetExceptionDesc: return the description of an exception code.     */
/***********************************************************************/
char *GetExceptionDesc(PGLOBAL g, unsigned int e)
  {
  char *p;

  switch (e) {
    case EXCEPTION_GUARD_PAGE:
      p = MSG(GUARD_PAGE);
      break;
    case EXCEPTION_DATATYPE_MISALIGNMENT:
      p = MSG(DATA_MISALIGN);
      break;
    case EXCEPTION_BREAKPOINT:
      p = MSG(BREAKPOINT);
      break;
    case EXCEPTION_SINGLE_STEP:
      p = MSG(SINGLE_STEP);
      break;
    case EXCEPTION_ACCESS_VIOLATION:
      p = MSG(ACCESS_VIOLATN);
      break;
    case EXCEPTION_IN_PAGE_ERROR:
      p = MSG(PAGE_ERROR);
      break;
    case EXCEPTION_INVALID_HANDLE:
      p = MSG(INVALID_HANDLE);
      break;
    case EXCEPTION_ILLEGAL_INSTRUCTION:
      p = MSG(ILLEGAL_INSTR);
      break;
    case EXCEPTION_NONCONTINUABLE_EXCEPTION:
      p = MSG(NONCONT_EXCEPT);
      break;
    case EXCEPTION_INVALID_DISPOSITION:
      p = MSG(INVALID_DISP);
      break;
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
      p = MSG(ARRAY_BNDS_EXCD);
      break;
    case EXCEPTION_FLT_DENORMAL_OPERAND:
      p = MSG(FLT_DENORMAL_OP);
      break;
    case EXCEPTION_FLT_DIVIDE_BY_ZERO:
      p = MSG(FLT_ZERO_DIVIDE);
      break;
    case EXCEPTION_FLT_INEXACT_RESULT:
      p = MSG(FLT_BAD_RESULT);
      break;
    case EXCEPTION_FLT_INVALID_OPERATION:
      p = MSG(FLT_INVALID_OP);
      break;
    case EXCEPTION_FLT_OVERFLOW:
      p = MSG(FLT_OVERFLOW);
      break;
    case EXCEPTION_FLT_STACK_CHECK:
      p = MSG(FLT_STACK_CHECK);
      break;
    case EXCEPTION_FLT_UNDERFLOW:
      p = MSG(FLT_UNDERFLOW);
      break;
    case EXCEPTION_INT_DIVIDE_BY_ZERO:
      p = MSG(INT_ZERO_DIVIDE);
      break;
    case EXCEPTION_INT_OVERFLOW:
      p = MSG(INT_OVERFLOW);
      break;
    case EXCEPTION_PRIV_INSTRUCTION:
      p = MSG(PRIV_INSTR);
      break;
    case EXCEPTION_STACK_OVERFLOW:
      p = MSG(STACK_OVERFLOW);
      break;
    case CONTROL_C_EXIT:
      p = MSG(CONTROL_C_EXIT);
      break;
    case STATUS_NO_MEMORY:
      p = MSG(NO_MEMORY);
      break;
    default:
      p = MSG(UNKNOWN_EXCPT);
      break;
    } // endswitch nSE

  return p;
  } // end of GetExceptionDesc
#endif   // WIN32 && !NOCATCH

/***********************************************************************/
/*  PlgDBalloc: allocates or suballocates memory conditionally.        */
/*  If mp.Sub is true at entry, this forces suballocation.             */
/*  If the memory is allocated, makes an entry in an allocation list   */
/*  so it can be freed at the normal or error query completion.        */
/***********************************************************************/
void *PlgDBalloc(PGLOBAL g, void *area, MBLOCK& mp)
  {
//bool        b;
  size_t      maxsub, minsub;
  void       *arp = (area) ? area : g->Sarea;
  PPOOLHEADER pph = (PPOOLHEADER)arp;

  if (mp.Memp) {
    // This is a reallocation. If this block is not suballocated, it
    // was already placed in the chain of memory blocks and we must
    // not do it again as it can trigger a loop when freeing them.
    // Note: this works if blocks can be reallocated only once.
    // Otherwise a new boolean must be added to the block that
    // indicate that it is chained, or a test on the whole chain be
    // done to check whether the block is already there.
//  b = mp.Sub;
    mp.Sub = false;    // Restrict suballocation to one quarter
    } // endif Memp

  // Suballoc when possible if mp.Sub is initially true, but leaving
  // a minimum amount of storage for future operations such as the
  // optimize recalculation after insert; otherwise
  // suballoc only if size is smaller than one quarter of free mem.
  minsub = (pph->FreeBlk + pph->To_Free + 524248) >> 2;
  maxsub = (pph->FreeBlk < minsub) ? 0 : pph->FreeBlk - minsub;
  mp.Sub = mp.Size <= ((mp.Sub) ? maxsub : (maxsub >> 2));

  if (trace)
    htrc("PlgDBalloc: in %p size=%d used=%d free=%d sub=%d\n",
          arp, mp.Size, pph->To_Free, pph->FreeBlk, mp.Sub);

  if (!mp.Sub) {
    // For allocations greater than one fourth of remaining storage
    // in the area, do allocate from virtual storage.
#if defined(WIN32)
    if (mp.Size >= BIGMEM)
      mp.Memp = VirtualAlloc(NULL, mp.Size, MEM_COMMIT, PAGE_READWRITE);
    else
#endif
      mp.Memp = malloc(mp.Size);

    if (!mp.Inlist && mp.Memp) {
      // New allocated block, put it in the memory block chain.
      PDBUSER dbuserp = (PDBUSER)g->Activityp->Aptr;

      mp.Next = dbuserp->Memlist;
      dbuserp->Memlist = &mp;
      mp.Inlist = true;
      } // endif mp

  } else
    // Suballocating is Ok.
    mp.Memp = PlugSubAlloc(g, area, mp.Size);

  return mp.Memp;
  } // end of PlgDBalloc

/***********************************************************************/
/*  PlgDBrealloc: reallocates memory conditionally.                    */
/*  Note that this routine can fail only when block size is increased  */
/*  because otherwise we keep the old storage on failure.              */
/***********************************************************************/
void *PlgDBrealloc(PGLOBAL g, void *area, MBLOCK& mp, size_t newsize)
  {
  MBLOCK m;

#if defined(_DEBUG)
//  assert (mp.Memp != NULL);
#endif

  if (trace)
    htrc("PlgDBrealloc: %p size=%d sub=%d\n", mp.Memp, mp.Size, mp.Sub);

  if (newsize == mp.Size)
    return mp.Memp;      // Nothing to do
  else
    m = mp;

  if (!mp.Sub && mp.Size < BIGMEM && newsize < BIGMEM) {
    // Allocation was done by malloc, try to use realloc but
    // suballoc if newsize is smaller than one quarter of free mem.
    size_t      maxsub;
    PPOOLHEADER pph = (PPOOLHEADER)((area) ? area : g->Sarea);

    maxsub = (pph->FreeBlk < 131072) ? 0 : pph->FreeBlk - 131072;

    if ((mp.Sub = (newsize <= (maxsub >> 2)))) {
      mp.Memp = PlugSubAlloc(g, area, newsize);
      memcpy(mp.Memp, m.Memp, min(m.Size, newsize));
      PlgDBfree(m);    // Free the old block
    } else if (!(mp.Memp = realloc(mp.Memp, newsize))) {
      mp = m;          // Possible only if newsize > Size
      return NULL;     // Failed
    } // endif's

    mp.Size = newsize;
  } else if (!mp.Sub || newsize > mp.Size) {
    // Was suballocated or Allocation was done by VirtualAlloc
    // Make a new allocation and copy the useful part
    // Note: DO NOT reset Memp and Sub so we know that this
    // is a reallocation in PlgDBalloc
    mp.Size = newsize;

    if (PlgDBalloc(g, area, mp)) {
      memcpy(mp.Memp, m.Memp, min(m.Size, newsize));
      PlgDBfree(m);    // Free the old block
    } else {
      mp = m;          // No space to realloc, do nothing

      if (newsize > m.Size)
        return NULL;   // Failed

    } // endif PlgDBalloc

  } // endif's

  if (trace)
    htrc(" newsize=%d newp=%p sub=%d\n", mp.Size, mp.Memp, mp.Sub);

  return mp.Memp;
  } // end of PlgDBrealloc

/***********************************************************************/
/*  PlgDBfree: free memory if not suballocated.                        */
/***********************************************************************/
void PlgDBfree(MBLOCK& mp)
  {
  if (trace)
    htrc("PlgDBfree: %p sub=%d size=%d\n", mp.Memp, mp.Sub, mp.Size);

  if (!mp.Sub && mp.Memp)
#if defined(WIN32)
    if (mp.Size >= BIGMEM)
      VirtualFree(mp.Memp, 0, MEM_RELEASE);
    else
#endif
      free(mp.Memp);

  // Do not reset Next to avoid cutting the Mblock chain
  mp.Memp = NULL;
  mp.Sub = false;
  mp.Size = 0;
  } // end of PlgDBfree

#if 0     // Not used yet
/***********************************************************************/
/*  Program for sub-allocating one item in a storage area.             */
/*  Note: This function is equivalent to PlugSubAlloc except that in   */
/*  case of insufficient memory, it returns NULL instead of doing a    */
/*  long jump. The caller must test the return value for error.        */
/***********************************************************************/
void *PlgDBSubAlloc(PGLOBAL g, void *memp, size_t size)
  {
  PPOOLHEADER pph;                           // Points on area header.

  if (!memp)
    /*******************************************************************/
    /*  Allocation is to be done in the Sarea.                         */
    /*******************************************************************/
    memp = g->Sarea;

  size = ((size + 3) / 4) * 4;       /* Round up size to multiple of 4 */
//size = ((size + 7) / 8) * 8;       /* Round up size to multiple of 8 */
  pph = (PPOOLHEADER)memp;

#if defined(DEBTRACE)
 htrc("PlgDBSubAlloc: memp=%p size=%d used=%d free=%d\n",
  memp, size, pph->To_Free, pph->FreeBlk);
#endif

  if ((uint)size > pph->FreeBlk) {   /* Not enough memory left in pool */
    char     *pname = NULL;
    PACTIVITY ap;

    if (memp == g->Sarea)
      pname = "Work";
    else if ((ap = g->Activityp)) {
      if      (memp == ap->LangRulep)
        pname = "Rule";
      else if (memp == ap->Nodep[0])
        pname = "Dictionary";
      else if (memp == ap->Nodep[1])
        pname = "Vartok";
      else if (memp == ap->Nodep[2])
        pname = "Lexicon";
      else if (memp == ap->User_Dictp)
        pname = "User dictionary";
      else if (ap->Aptr)
        pname = "Application";

    } // endif memp

    if (pname)
      sprintf(g->Message,
      "Not enough memory in %s area for request of %d (used=%d free=%d)",
                          pname, size, pph->To_Free, pph->FreeBlk);
    else
      sprintf(g->Message, MSG(SUBALLOC_ERROR),
                          memp, size, pph->To_Free, pph->FreeBlk);

#if defined(DEBTRACE)
 htrc("%s\n", g->Message);
#endif

    return NULL;
    } // endif size

  /*********************************************************************/
  /*  Do the suballocation the simplest way.                           */
  /*********************************************************************/
  memp = MakePtr(memp, pph->To_Free);   // Points to suballocated block
  pph->To_Free += size;                 // New offset of pool free block
  pph->FreeBlk -= size;                 // New size   of pool free block
#if defined(DEBTRACE)
 htrc("Done memp=%p used=%d free=%d\n",
  memp, pph->To_Free, pph->FreeBlk);
#endif
  return (memp);
  } // end of PlgDBSubAlloc
#endif // 0  Not used yet

/***********************************************************************/
/*  PUTOUT: Plug DB object typing routine.                             */
/***********************************************************************/
void PlugPutOut(PGLOBAL g, FILE *f, short t, void *v, uint n)
  {
  char  m[64];

  if (trace)
    htrc("PUTOUT: f=%p t=%d v=%p n=%d\n", f, t, v, n);

  if (!v)
    return;

  memset(m, ' ', n);                             /* Make margin string */
  m[n] = '\0';
  n += 2;                                        /* Increase margin    */

  switch (t) {
    case TYPE_ERROR:
      fprintf(f, "--> %s\n", (PSZ)v);
      break;

    case TYPE_STRING:
    case TYPE_PSZ:
      fprintf(f, "%s%s\n", m, (PSZ)v);
      break;

    case TYPE_FLOAT:
      fprintf(f, "%s%lf\n", m, *(double *)v);
      break;

    case TYPE_LIST:
    case TYPE_COLIST:
    case TYPE_COL:
     {PPARM p;

      if (t == TYPE_LIST)
        fprintf(f, "%s%s\n", m, MSG(LIST));
      else
        fprintf(f, "%s%s\n", m, "Colist:");

      for (p = (PPARM)v; p; p = p->Next)
        PlugPutOut(g, f, p->Type, p->Value, n);

      } break;

    case TYPE_INT:
      fprintf(f, "%s%d\n", m, *(int *)v);
      break;

    case TYPE_SHORT:
      fprintf(f, "%s%hd\n", m, *(short *)v);
      break;

    case TYPE_VOID:
      break;

    case TYPE_SQL:
    case TYPE_TABLE:
    case TYPE_TDB:
    case TYPE_XOBJECT:
      ((PBLOCK)v)->Print(g, f, n-2);
      break;

    default:
      fprintf(f, "%s%s %d\n", m, MSG(ANSWER_TYPE), t);
    } /* endswitch */

  return;
  } /* end of PlugPutOut */

/***********************************************************************/
/*  NewPointer: makes a table of pointer values to be changed later.   */
/***********************************************************************/
DllExport void NewPointer(PTABS t, void *oldv, void *newv)
  {
  PTABPTR tp;

  if (!oldv)                                       /* error ?????????? */
    return;

  if (!t->P1 || t->P1->Num == 50)
  {
    if (!(tp = new TABPTR)) {
      PGLOBAL g = t->G;

      sprintf(g->Message, "NewPointer: %s", MSG(MEM_ALLOC_ERROR));
      longjmp(g->jumper[g->jump_level], 3);
    } else {
      tp->Next = t->P1;
      tp->Num = 0;
      t->P1 = tp;
    } /* endif tp */
  }

  t->P1->Old[t->P1->Num] = oldv;
  t->P1->New[t->P1->Num++] = newv;
  } /* end of NewPointer */

#if 0
/***********************************************************************/
/*  Compare two files and return 0 if they are identical, else 1.      */
/***********************************************************************/
int FileComp(PGLOBAL g, char *file1, char *file2)
  {
  char *fn[2], *bp[2], buff1[4096], buff2[4096];
  int   i, k, n[2], h[2] = {-1,-1};
  int  len[2], rc = -1;

  fn[0] = file1; fn[1] = file2;
  bp[0] = buff1; bp[1] = buff2;

  for (i = 0; i < 2; i++) {
#if defined(WIN32)
    h[i]= global_open(g, MSGID_NONE, fn[i], _O_RDONLY | _O_BINARY);
#else   // !WIN32
    h[i]= global_open(g, MSGOD_NONE, fn[i], O_RDONLY);
#endif  // !WIN32

    if (h[i] == -1) {
//      if (errno != ENOENT) {
        sprintf(g->Message, MSG(OPEN_MODE_ERROR),
                "rb", (int)errno, fn[i]);
        strcat(strcat(g->Message, ": "), strerror(errno));
        longjmp(g->jumper[g->jump_level], 666);
//      } else
//        len[i] = 0;          // File does not exist yet

    } else {
      if ((len[i] = _filelength(h[i])) < 0) {
        sprintf(g->Message, MSG(FILELEN_ERROR), "_filelength", fn[i]);
        longjmp(g->jumper[g->jump_level], 666);
        } // endif len

    } // endif h

    } // endfor i

  if (len[0] != len[1])
    rc = 1;

  while (rc == -1) {
    for (i = 0; i < 2; i++)
      if ((n[i] = read(h[i], bp[i], 4096)) < 0) {
        sprintf(g->Message, MSG(READ_ERROR), fn[i], strerror(errno));
        goto fin;
        } // endif n

    if (n[0] != n[1])
      rc = 1;
    else if (*n == 0)
      rc = 0;
    else for (k = 0; k < *n; k++)
      if (*(bp[0] + k) != *(bp[1] + k)) {
        rc = 1;
        goto fin;
        } // endif bp

    } // endwhile

 fin:
  for (i = 0; i < 2; i++)
    if (h[i] != -1)
      close(h[i]);

  return rc;
  } // end of FileComp
#endif // 0
