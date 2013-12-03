/************** PlgDBSem H Declares Source Code File (.H) **************/
/*  Name: PLGDBSEM.H  Version 3.5                                      */
/*                                                                     */
/*  (C) Copyright to the author Olivier BERTRAND          1998-2012    */
/*                                                                     */
/*  This file contains the PlugDB++ application type definitions.      */
/***********************************************************************/

/***********************************************************************/
/*  Include required application header files                          */
/***********************************************************************/
#include "checklvl.h"

/***********************************************************************/
/*  DB Constant definitions.                                           */
/***********************************************************************/
#if defined(FRENCH)
#define DEFAULT_LOCALE  "French"
#else   // !FRENCH
#define DEFAULT_LOCALE  "English"
#endif  // !FRENCH

#define DOS_MAX_PATH    144   /* Must be the same across systems       */
#define DOS_BUFF_LEN    100   /* Number of lines in binary file buffer */
#undef  DOMAIN                /* For Unix version                      */

enum BLKTYP {TYPE_TABLE      = 50,    /* Table Name/Srcdef/... Block   */
             TYPE_COLUMN     = 51,    /* Column Name/Qualifier Block   */
//           TYPE_OPVAL      = 52,    /* Operator value (OPVAL)        */
             TYPE_TDB        = 53,    /* Table Description Block       */
             TYPE_COLBLK     = 54,    /* Column Description Block      */
             TYPE_PSZ        = 64,    /* Pointer to String ended by 0  */
             TYPE_SQL        = 65,    /* Pointer to SQL block          */
             TYPE_XOBJECT    = 69,    /* Extended DB object            */
             TYPE_COLCRT     = 71,    /* Column creation block         */
             TYPE_CONST      = 72,    /* Constant                      */
//           TYPE_INDEXDEF   = 73,    /* Index definition block        */
//           TYPE_OPER       = 74,    /* Operator block (OPER)         */

/*-------------------- type tokenized string --------------------------*/
             TYPE_DATE       =  8,    /* Timestamp                     */
/*-------------------- additional values used by LNA ------------------*/
             TYPE_COLIST     = 14,    /* Column list                   */
             TYPE_COL        = 41,    /* Column                        */
/*-------------------- types used by scalar functions -----------------*/
             TYPE_NUM        = 12,
             TYPE_UNDEF      = 13,
/*-------------------- file blocks used when closing ------------------*/
             TYPE_FB_FILE    = 22,    /* File block (stream)           */
             TYPE_FB_MAP     = 23,    /* Mapped file block (storage)   */
             TYPE_FB_HANDLE  = 24,    /* File block (handle)           */
             TYPE_FB_XML     = 21,    /* DOM XML file block            */
             TYPE_FB_XML2    = 27};   /* libxml2 XML file block        */

enum TABTYPE {TAB_UNDEF =  0,   /* Table of undefined type             */
              TAB_DOS   =  1,   /* Fixed column offset, variable LRECL */
              TAB_FIX   =  2,   /* Fixed column offset, fixed LRECL    */
              TAB_BIN   =  3,   /* Like FIX but can have binary fields */
              TAB_CSV   =  4,   /* DOS files with CSV records          */
              TAB_FMT   =  5,   /* DOS files with formatted recordss   */
              TAB_DBF   =  6,   /* DBF Dbase or Foxpro files           */
              TAB_XML   =  7,   /* XML or HTML files                   */
              TAB_INI   =  8,   /* INI or CFG files                    */
              TAB_VEC   =  9,   /* Vector column arrangement           */
              TAB_ODBC  = 10,   /* Table accessed via (unix)ODBC       */
              TAB_MYSQL = 11,   /* MySQL table accessed via MySQL API  */
              TAB_DIR   = 12,   /* Returns a list of files             */
              TAB_MAC   = 13,   /* MAC address (Windows only)          */
              TAB_WMI   = 14,   /* WMI tables  (Windows only)          */
              TAB_TBL   = 15,   /* Collection of CONNECT tables        */
              TAB_OEM   = 16,   /* OEM implemented table               */
              TAB_XCL   = 17,   /* XCL table                           */
              TAB_OCCUR = 18,   /* OCCUR table                         */
              TAB_PRX   = 19,   /* Proxy (catalog) table               */
              TAB_PLG   = 20,   /* PLG NIY                             */
              TAB_PIVOT = 21,   /* PIVOT NIY                           */
              TAB_JCT   = 22,   /* Junction tables NIY                 */
              TAB_DMY   = 23,   /* DMY Dummy tables NIY                */
              TAB_NIY   = 24};  /* Table not implemented yet           */

enum AMT {TYPE_AM_ERROR =   0,        /* Type not defined              */
          TYPE_AM_ROWID =   1,        /* ROWID type (special column)   */
          TYPE_AM_FILID =   2,        /* FILEID type (special column)  */
          TYPE_AM_TAB   =   3,        /* Table (any type)              */
          TYPE_AM_VIEW  =   4,        /* VIEW (any type)               */
          TYPE_AM_SRVID =   5,        /* SERVID type (special column)  */
          TYPE_AM_TABID =   6,        /* TABID  type (special column)  */
          TYPE_AM_CNSID =   7,        /* CONSTID type (special column) */
          TYPE_AM_COUNT =  10,        /* CPT AM type no (count table)  */
          TYPE_AM_DCD   =  20,        /* Decode access method type no  */
          TYPE_AM_CMS   =  30,        /* CMS access method type no     */
          TYPE_AM_MAP   =  32,        /* MAP access method type no     */
          TYPE_AM_FMT   =  33,        /* DOS files with formatted recs */
          TYPE_AM_CSV   =  34,        /* DOS files with CSV records    */
          TYPE_AM_MCV   =  35,        /* MAP files with CSV records    */
          TYPE_AM_DOS   =  36,        /* DOS am with Lrecl = V         */
          TYPE_AM_FIX   =  38,        /* DOS am with Lrecl = F         */
          TYPE_AM_BIN   =  39,        /* DOS am with Lrecl = B         */
          TYPE_AM_VCT   =  40,        /* VCT access method type no     */
          TYPE_AM_VMP   =  43,        /* VMP access method type no     */
          TYPE_AM_QRY   =  50,        /* QRY access method type no     */
          TYPE_AM_QRS   =  51,        /* QRYRES access method type no  */
          TYPE_AM_SQL   =  60,        /* SQL VIEW access method type   */
          TYPE_AM_PLG   =  70,        /* PLG access method type no     */
          TYPE_AM_PLM   =  71,        /* PDM access method type no     */
          TYPE_AM_DOM   =  80,        /* DOM access method type no     */
          TYPE_AM_DIR   =  90,        /* DIR access method type no     */
          TYPE_AM_ODBC  = 100,        /* ODBC access method type no    */
          TYPE_AM_XDBC  = 101,        /* XDBC access method type no    */
          TYPE_AM_OEM   = 110,        /* OEM access method type no     */
          TYPE_AM_TBL   = 115,        /* TBL access method type no     */
          TYPE_AM_PIVOT = 120,        /* PIVOT access method type no   */
          TYPE_AM_SRC   = 121,        /* PIVOT multiple column type no */
          TYPE_AM_FNC   = 122,        /* PIVOT source column type no   */
          TYPE_AM_XCOL  = 124,        /* XCOL access method type no    */
          TYPE_AM_XML   = 127,        /* XML access method type no     */
          TYPE_AM_OCCUR = 128,        /* OCCUR access method type no   */
          TYPE_AM_PRX   = 129,        /* PROXY access method type no   */
          TYPE_AM_XTB   = 130,        /* SYS table access method type  */
          TYPE_AM_BLK   = 131,        /* BLK access method type no     */
          TYPE_AM_ZIP   = 132,        /* ZIP access method type no     */
          TYPE_AM_ZLIB  = 133,        /* ZLIB access method type no    */
          TYPE_AM_MAC   = 137,        /* MAC table access method type  */
          TYPE_AM_WMI   = 139,        /* WMI table access method type  */
          TYPE_AM_XCL   = 140,        /* SYS column access method type */
          TYPE_AM_INI   = 150,        /* INI files access method       */
          TYPE_AM_TFC   = 155,        /* TFC (Circa) (Fuzzy compare)   */
          TYPE_AM_DBF   = 160,        /* DBF Dbase files am type no    */
          TYPE_AM_JCT   = 170,        /* Junction tables am type no    */
          TYPE_AM_DMY   = 172,        /* DMY Dummy tables am type no   */
          TYPE_AM_SET   = 180,        /* SET Set tables am type no     */
          TYPE_AM_MYSQL = 192,        /* MYSQL access method type no   */
          TYPE_AM_MYX   = 193,        /* MYSQL EXEC access method type */
          TYPE_AM_CAT   = 195,        /* Catalog access method type no */
          TYPE_AM_OUT   = 200};       /* Output relations (storage)    */

enum RECFM {RECFM_NAF   =    -2,      /* Not a file                    */
            RECFM_OEM   =    -1,      /* OEM file access method        */
            RECFM_VAR   =     0,      /* Varying length DOS files      */
            RECFM_FIX   =     1,      /* Fixed length DOS files        */
            RECFM_BIN   =     2,      /* Binary DOS files (also fixed) */
            RECFM_VCT   =     3,      /* VCT formatted files           */
            RECFM_ODBC  =     4,      /* Table accessed via ODBC       */
            RECFM_PLG   =     5,      /* Table accessed via PLGconn    */
            RECFM_DBF   =     6};     /* DBase formatted file          */

#if 0
enum MISC {DB_TABNO     =     1,      /* DB routines in Utility Table  */
           MAX_MULT_KEY =    10,      /* Max multiple key number       */
           NAM_LEN      =   128,      /* Length of col and tab names   */
           ARRAY_SIZE   =    50,      /* Default array block size      */
           MAXRES       =   500,      /* Default maximum result lines  */
           MAXLIN       = 10000,      /* Default maximum data lines    */
           MAXBMP       =    32};     /* Default XDB2 max bitmap size  */

enum ALGMOD {AMOD_AUTO =  0,          /* PLG chooses best algorithm    */
             AMOD_SQL  =  1,          /* Use SQL algorithm             */
             AMOD_QRY  =  2};         /* Use QUERY algorithm           */
#else   // !0
#define    NAM_LEN          128
#endif // !0

enum MODE {MODE_ERROR   = -1,         /* Invalid mode                  */
           MODE_ANY     =  0,         /* Unspecified mode              */
           MODE_READ    = 10,         /* Input/Output mode             */
           MODE_WRITE   = 20,         /* Input/Output mode             */
           MODE_UPDATE  = 30,         /* Input/Output mode             */
           MODE_INSERT  = 40,         /* Input/Output mode             */
           MODE_DELETE  = 50};        /* Input/Output mode             */

#if !defined(RC_OK_DEFINED)
#define RC_OK_DEFINED
enum RCODE {RC_OK      =   0,         /* No error return code          */
            RC_NF      =   1,         /* Not found return code         */
            RC_EF      =   2,         /* End of file return code       */
            RC_FX      =   3,         /* Error return code             */
            RC_INFO    =   4};        /* Success with info             */
#endif   // !RC_OK_DEFINED

enum OPVAL {OP_EQ      =   1,         /* Filtering operator =          */
            OP_NE      =   2,         /* Filtering operator !=         */
            OP_GT      =   3,         /* Filtering operator >          */
            OP_GE      =   4,         /* Filtering operator >=         */
            OP_LT      =   5,         /* Filtering operator <          */
            OP_LE      =   6,         /* Filtering operator <=         */
            OP_IN      =   7,         /* Filtering operator IN         */
            OP_NULL    =   8,         /* Filtering operator IS NULL    */
            OP_EXIST   =   9,         /* Filtering operator EXISTS     */
            OP_LIKE    =  10,         /* Filtering operator LIKE       */
            OP_LOJ     =  -1,         /* Filter op LEFT  OUTER JOIN    */
            OP_ROJ     =  -2,         /* Filter op RIGHT OUTER JOIN    */
            OP_DTJ     =  -3,         /* Filter op DISTINCT    JOIN    */
            OP_XX      =  11,         /* Filtering operator unknown    */
            OP_AND     =  12,         /* Filtering operator AND        */
            OP_OR      =  13,         /* Filtering operator OR         */
            OP_CNC     =  14,         /* Expression Concat operator    */
            OP_NOT     =  15,         /* Filtering operator NOT        */
            OP_SEP     =  20,         /* Filtering separator           */
            OP_ADD     =  16,         /* Expression Add operator       */
            OP_SUB     =  17,         /* Expression Substract operator */
            OP_MULT    =  18,         /* Expression Multiply operator  */
            OP_DIV     =  19,         /* Expression Divide operator    */
            OP_NOP     =  21,         /* Scalar function is nopped     */
            OP_NUM     =  22,         /* Scalar function Op Num        */
            OP_ABS     =  23,         /* Scalar function Op Abs        */
            OP_MAX     =  24,         /* Scalar function Op Max        */
            OP_MIN     =  25,         /* Scalar function Op Min        */
            OP_CEIL    =  26,         /* Scalar function Op Ceil       */
            OP_FLOOR   =  27,         /* Scalar function Op Floor      */
            OP_MOD     =  28,         /* Scalar function Op Mod        */
            OP_ROUND   =  29,         /* Scalar function Op Round      */
            OP_SIGN    =  30,         /* Scalar function Op Sign       */
            OP_LEN     =  31,         /* Scalar function Op Len        */
            OP_INSTR   =  32,         /* Scalar function Op Instr      */
            OP_LEFT    =  33,         /* Scalar function Op Left       */
            OP_RIGHT   =  34,         /* Scalar function Op Right      */
            OP_ASCII   =  35,         /* Scalar function Op Ascii      */
            OP_EXP     =  36,         /* Scalar function Op Exp        */
            OP_LN      =  37,         /* Scalar function Op Ln         */
            OP_LOG     =  38,         /* Scalar function Op Log        */
            OP_POWER   =  39,         /* Scalar function Op Power      */
            OP_SQRT    =  40,         /* Scalar function Op Sqrt       */
            OP_COS     =  41,         /* Scalar function Op Cos        */
            OP_COSH    =  42,         /* Scalar function Op Cosh       */
            OP_SIN     =  43,         /* Scalar function Op Sin        */
            OP_SINH    =  44,         /* Scalar function Op Sinh       */
            OP_TAN     =  45,         /* Scalar function Op Tan        */
            OP_TANH    =  46,         /* Scalar function Op Tanh       */
            OP_USER    =  47,         /* Scalar function Op User       */
            OP_CHAR    =  48,         /* Scalar function Op Char       */
            OP_UPPER   =  49,         /* Scalar function Op Upper      */
            OP_LOWER   =  50,         /* Scalar function Op Lower      */
            OP_RPAD    =  51,         /* Scalar function Op Rpad       */
            OP_LPAD    =  52,         /* Scalar function Op Lpad       */
            OP_LTRIM   =  53,         /* Scalar function Op Ltrim      */
            OP_RTRIM   =  54,         /* Scalar function Op Rtrim      */
            OP_REPL    =  55,         /* Scalar function Op Replace    */
            OP_SUBST   =  56,         /* Scalar function Op Substr     */
            OP_LJUST   =  57,         /* Scalar function Op Ljustify   */
            OP_RJUST   =  58,         /* Scalar function Op Rjustify   */
            OP_CJUST   =  59,         /* Scalar function Op Cjustify   */
            OP_ENCODE  =  60,         /* Scalar function Op Encode     */
            OP_DECODE  =  61,         /* Scalar function Op Decode     */
            OP_SEQU    =  62,         /* Scalar function Op Sequence   */
            OP_IF      =  63,         /* Scalar function Op If         */
            OP_STRING  =  64,         /* Scalar function Op String     */
            OP_TOKEN   =  65,         /* Scalar function Op Token      */
            OP_SNDX    =  66,         /* Scalar function Op Soundex    */
            OP_DATE    =  67,         /* Scalar function Op Date       */
            OP_MDAY    =  68,         /* Scalar function Op Month Day  */
            OP_MONTH   =  69,         /* Scalar function Op Month of   */
            OP_YEAR    =  70,         /* Scalar function Op Year of    */
            OP_WDAY    =  71,         /* Scalar function Op Week Day   */
            OP_YDAY    =  72,         /* Scalar function Op Year Day   */
            OP_DBTWN   =  73,         /* Scalar function Op Days betwn */
            OP_MBTWN   =  74,         /* Scalar function Op Months btw */
            OP_YBTWN   =  75,         /* Scalar function Op Years btwn */
            OP_ADDAY   =  76,         /* Scalar function Op Add Days   */
            OP_ADDMTH  =  77,         /* Scalar function Op Add Months */
            OP_ADDYR   =  78,         /* Scalar function Op Add Years  */
            OP_NXTDAY  =  79,         /* Scalar function Op Next Day   */
            OP_SYSDT   =  80,         /* Scalar function Op SysDate    */
            OP_DELTA   =  81,         /* Scalar function Op Delta      */
            OP_LAST    =  82,         /* Scalar function Op Last       */
            OP_IFF     =  83,         /* Scalar function Op Iff        */
            OP_MAVG    =  84,         /* Scalar function Op Moving Avg */
            OP_VWAP    =  85,         /* Scalar function Op VWAP       */
            OP_TIME    =  86,         /* Scalar function Op TIME       */
            OP_SETLEN  =  87,         /* Scalar function Op Set Length */
            OP_TRANSL  =  88,         /* Scalar function Op Translate  */
            OP_BITAND  =  89,         /* Expression BitAnd operator    */
            OP_BITOR   =  90,         /* Expression BitOr  operator    */
            OP_BITXOR  =  91,         /* Expression XOR    operator    */
            OP_BITNOT  =  92,         /* Expression Complement operator*/
            OP_CNTIN   =  93,         /* Scalar function Count In      */
            OP_FDISK   =  94,         /* Scalar function Disk of fileid*/
            OP_FPATH   =  95,         /* Scalar function Path of fileid*/
            OP_FNAME   =  96,         /* Scalar function Name of fileid*/
            OP_FTYPE   =  97,         /* Scalar function Type of fileid*/
            OP_XDATE   =  98,         /* Scalar function Op Fmt Date   */
            OP_SWITCH  =  99,         /* Scalar function Op Switch     */
            OP_EXIT    = 100,         /* Scalar function Op Exit       */
            OP_LIT     = 101,         /* Scalar function Op Literal    */
            OP_LOCALE  = 102,         /* Scalar function Op Locale     */
            OP_FRNCH   = 103,         /* Scalar function Op French     */
            OP_ENGLSH  = 104,         /* Scalar function Op English    */
            OP_RAND    = 105,         /* Scalar function Op Rand(om)   */
            OP_FIRST   = 106,         /* Index operator Find First     */
            OP_NEXT    = 107,         /* Index operator Find Next      */
            OP_SAME    = 108,         /* Index operator Find Next Same */
            OP_FSTDIF  = 109,         /* Index operator Find First dif */
            OP_NXTDIF  = 110,         /* Index operator Find Next dif  */
            OP_VAL     = 111,         /* Scalar function Op Valist     */
            OP_QUART   = 112,         /* Scalar function Op QUARTER    */
            OP_CURDT   = 113,         /* Scalar function Op CurDate    */
            OP_NWEEK   = 114,         /* Scalar function Op Week number*/
            OP_ROW     = 115,         /* Scalar function Op Row        */
            OP_SYSTEM  = 200,         /* Scalar function Op System     */
            OP_REMOVE  = 201,         /* Scalar function Op Remove     */
            OP_RENAME  = 202,         /* Scalar function Op Rename     */
            OP_FCOMP   = 203};        /* Scalar function Op Compare    */

enum TUSE {USE_NO      =   0,         /* Table is not yet linearized   */
           USE_LIN     =   1,         /* Table is linearized           */
           USE_READY   =   2,         /* Column buffers are allocated  */
           USE_OPEN    =   3,         /* Table is open                 */
           USE_CNT     =   4,         /* Specific to LNA               */
           USE_NOKEY   =   5};        /* Specific to SqlToHql          */

/***********************************************************************/
/*  Following definitions are used to indicate the status of a column. */
/***********************************************************************/
enum STATUS {BUF_NO      = 0x00,      /* Column buffer not allocated   */
             BUF_EMPTY   = 0x01,      /* Column buffer is empty        */
             BUF_READY   = 0x02,      /* Column buffer is ready        */
             BUF_READ    = 0x04,      /* Column buffer has read value  */
             BUF_MAPPED  = 0x08};     /* Used by the VMPFAM class      */

/***********************************************************************/
/*  Following definitions are used to indicate how a column is used.   */
/*  Corresponding bits are ON if the column is used in:                */
/***********************************************************************/
enum COLUSE {U_P         = 0x01,      /* the projection list.          */
             U_J_EXT     = 0x02,      /* a join filter.                */
             U_J_INT     = 0x04,      /* a join after linearisation.   */
/*-- Such a column have a constant value throughout a subquery eval. --*/
             U_CORREL    = 0x08,      /* a correlated sub-query        */
/*-------------------- additional values used by CONNECT --------------*/
             U_VAR       = 0x10,      /* a VARCHAR column              */
             U_VIRTUAL   = 0x20,      /* a VIRTUAL column              */
             U_NULLS     = 0x40,      /* The column may have nulls     */
             U_IS_NULL   = 0x80,      /* The column has a null value   */
             U_SPECIAL   = 0x100};    /* The column is special         */

/***********************************************************************/
/*  DB description class and block pointer definitions.                */
/***********************************************************************/
typedef class XTAB       *PTABLE;
typedef class COLUMN     *PCOLUMN;
typedef class XOBJECT    *PXOB;
typedef class COLBLK     *PCOL;
typedef class TBX        *PTBX;
typedef class TDB        *PTDB;
typedef       void       *PSQL;          // Not used
typedef class TDBASE     *PTDBASE;
typedef class TDBDOS     *PTDBDOS;
typedef class TDBFIX     *PTDBFIX;
typedef class TDBFMT     *PTDBFMT;
typedef class TDBCSV     *PTDBCSV;
typedef class TDBDOM     *PTDBDOM;
typedef class TDBDIR     *PTDBDIR;
typedef class DOSCOL     *PDOSCOL;
typedef class CSVCOL     *PCSVCOL;
typedef class MAPCOL     *PMAPCOL;
typedef class TDBMFT     *PTDBMFT;
typedef class TDBMCV     *PTDBMCV;
typedef class MCVCOL     *PMCVCOL;
typedef class RESCOL     *PRESCOL;
typedef class XXBASE     *PKXBASE;
typedef class KXYCOL     *PXCOL;
typedef class CATALOG    *PCATLG;
typedef class RELDEF     *PRELDEF;
typedef class TABDEF     *PTABDEF;
typedef class DOSDEF     *PDOSDEF;
typedef class CSVDEF     *PCSVDEF;
typedef class VCTDEF     *PVCTDEF;
typedef class PIVOTDEF   *PPIVOTDEF;
typedef class DOMDEF     *PDOMDEF;
typedef class DIRDEF     *PDIRDEF;
typedef class OEMDEF     *POEMDEF;
typedef class COLCRT     *PCOLCRT;
typedef class COLDEF     *PCOLDEF;
typedef class CONSTANT   *PCONST;
typedef class VALUE      *PVAL;
typedef class VALBLK     *PVBLK;

typedef struct _fblock   *PFBLOCK;
typedef struct _mblock   *PMBLOCK;
typedef struct _cblock   *PCBLOCK;
typedef struct _tabs     *PTABS;
typedef struct _qryres   *PQRYRES;
typedef struct _colres   *PCOLRES;
typedef struct _datpar   *PDTP;
typedef struct indx_used *PXUSED;

/***********************************************************************/
/*  Utility blocks for file and storage.                               */
/***********************************************************************/
typedef struct _fblock {               /* Opened (mapped) file block   */
  struct _fblock *Next;
  LPCSTR     Fname;                    /* Point on file name           */
  size_t     Length;                   /* File length  (<4GB)          */
  short      Count;                    /* Nb of times map is used      */
  short      Type;                     /* TYPE_FB_FILE or TYPE_FB_MAP  */
  MODE       Mode;                     /* Open mode                    */
  char      *Memory;                   /* Pointer to file mapping view */
  void      *File;                     /* FILE pointer                 */
  HANDLE     Handle;                   /* File handle                  */
  } FBLOCK;

typedef struct _mblock {               /* Memory block                 */
  PMBLOCK Next;
  bool    Inlist;                      /* True if in mblock list       */
  size_t  Size;                        /* Size of allocation           */
  bool    Sub;                         /* True if suballocated         */
  void   *Memp;                        /* Memory pointer               */
  } MBLOCK;

/***********************************************************************/
/*  The QUERY application User Block.                                  */
/***********************************************************************/
typedef struct {                       /* User application block       */
//void      *Act2;                     /* RePoint to activity block    */
//short      LineLen;                  /* Current output line len      */
  NAME       Name;                     /* User application name        */
//NAME       Password;                 /* User application password    */
//PSZ        UserFile;                 /* User application filename    */
  char       Server[17];               /* Server name                  */
  char       DBName[17];               /* Current database name        */
//char       Host[65];                 /* Caller's host name           */
//char       User[17];                 /* Caller's user name           */
//uint       Granted;                  /* Grant bitmap                 */
  PCATLG     Catalog;                  /* To CATALOG class             */
  PQRYRES    Result;                   /* To query result blocks       */
  PFBLOCK    Openlist;                 /* To file/map open list        */
  PMBLOCK    Memlist;                  /* To memory block list         */
  PXUSED     Xlist;                    /* To used index list           */
//int        Maxres;                   /* Result Max nb of lines       */
//int        Maxtmp;                   /* Intermediate tables Maxres   */
//int        Maxlin;                   /* Query Max nb of data lines   */
//int        Maxbmp;                   /* Maximum XDB2 bitmap size     */
  int        Check;                    /* General level of checking    */
  int        Numlines;                 /* Number of lines involved     */
//ALGMOD     AlgChoice;                /* Choice of algorithm mode     */
//AREADEF    DescArea;                 /* Table desc. area size        */
  USETEMP    UseTemp;                  /* Use temporary file           */
//int        Curtype;                  /* 0: static else: dynamic      */
  int        Vtdbno;                   /* Used for TDB number setting  */
  bool       Remote;                   /* true: if remotely called     */
//bool       NotFinal;                 /* true: for intermediate table */
  bool       Proginfo;                 /* true: return progress info   */
  bool       Subcor;                   /* Used for Progress info       */
  size_t     ProgMax;                  /* Used for Progress info       */
  size_t     ProgCur;                  /* Used for Progress info       */
  size_t     ProgSav;                  /* Used for Progress info       */
  LPCSTR     Step;                     /* Execution step name          */
//char       Work[_MAX_PATH];          /* Local work path              */
  } DBUSERBLK, *PDBUSER;

/***********************************************************************/
/*  Column output format.                                              */
/***********************************************************************/
typedef struct _format {  /* Format descriptor block                   */
  char   Type[2];         /* C:char, F:double, N:int, Dx: date         */
  ushort Length;          /* Output length                             */
  short  Prec;            /* Output precision                          */
  } FORMAT, *PFORMAT;

/***********************************************************************/
/*  Definition of blocks used in type and copy routines.               */
/***********************************************************************/
typedef struct _tabptr {                                   /* start=P1 */
  struct _tabptr *Next;
  int   Num;                                             /* alignement */
  void *Old[50];
  void *New[50];                  /* old and new values of copied ptrs */
  } TABPTR, *PTABPTR;

typedef struct _tabadr {                                   /* start=P3 */
  struct _tabadr *Next;
  int   Num;
  void *Adx[50];                       /* addr of pointers to be reset */
  } TABADR, *PTABADR;

typedef struct _tabs {
  PGLOBAL G;
  PTABPTR P1;
  PTABADR P3;
  } TABS;

/***********************************************************************/
/*  Following definitions are used to define table fields (columns).   */
/***********************************************************************/
enum XFLD {FLD_NO       =  0,         /* Not a field definition item   */
           FLD_NAME     =  1,         /* Item name                     */
           FLD_TYPE     =  2,         /* Field type                    */
           FLD_TYPENAME =  3,         /* Field type name               */
           FLD_PREC     =  4,         /* Field precision (length?)     */
           FLD_LENGTH   =  5,         /* Field length (?)              */
           FLD_SCALE    =  6,         /* Field scale (precision)       */
           FLD_RADIX    =  7,         /* Field radix                   */
           FLD_NULL     =  8,         /* Field nullable property       */
           FLD_REM      =  9,         /* Field comment (remark)        */
           FLD_CHARSET  = 10,         /* Field collation               */
           FLD_KEY      = 11,         /* Field key property            */
           FLD_DEFAULT  = 12,         /* Field default value           */
           FLD_PRIV     = 13,         /* Field priviledges             */
           FLD_DATEFMT  = 14,         /* Field date format             */
           FLD_QUALIF   = 15,         /* Table qualifier               */
           FLD_OWNER    = 16,         /* Table owner                   */
           FLD_TABNAME  = 17};        /* Column Table name             */

/***********************************************************************/
/*  Result of last SQL noconv query.                                   */
/***********************************************************************/
typedef  struct _qryres {
  PCOLRES Colresp;                 /* Points to columns of result      */
  bool    Continued;               /* true when more rows to fetch     */
  bool    Truncated;               /* true when truncated by maxres    */
  bool    Suball;                  /* true when entirely suballocated  */
  bool    Info;                    /* true when info msg generated     */
  int     Maxsize;                 /* Max query number of lines        */
  int     Maxres;                  /* Allocation size                  */
  int     Nblin;                   /* Number of rows in result set     */
  int     Nbcol;                   /* Number of columns in result set  */
  int     Cursor;                  /* Starting position to get data    */
  int     BadLines;                /* Skipped bad lines in table file  */
  } QRYRES, *PQRYRES;

typedef  struct _colres {
  PCOLRES Next;                    /* To next result column            */
  PCOL    Colp;                    /* To matching column block         */
  PSZ     Name;                    /* Column header                    */
  PVBLK   Kdata;                   /* Column block of values           */
  char   *Nulls;                   /* Column null value array          */
  int     Type;                    /* Internal type                    */
  int     Datasize;                /* Overall data size                */
  int     Ncol;                    /* Column number                    */
  int     Clen;                    /* Data individual internal size    */
  int     Length;                  /* Data individual print length     */
  int     Prec;                    /* Precision                        */
  int     Flag;                    /* Flag option value                */
  XFLD    Fld;                     /* Type of field info               */
  } COLRES;

#if defined(WIN32) && !defined(NOEX)
#define DllExport  __declspec( dllexport )
#else   // !WIN32
#define DllExport
#endif  // !WIN32

/***********************************************************************/
/*  Utility routines.                                                  */
/***********************************************************************/
PPARM    Vcolist(PGLOBAL, PTDB, PSZ, bool);
void     PlugPutOut(PGLOBAL, FILE *, short, void *, uint);
void     PlugLineDB(PGLOBAL, PSZ, short, void *, uint);
char    *PlgGetDataPath(PGLOBAL g);
void    *PlgDBalloc(PGLOBAL, void *, MBLOCK&);
void    *PlgDBrealloc(PGLOBAL, void *, MBLOCK&, size_t);
void     AddPointer(PTABS, void *);
PDTP     MakeDateFormat(PGLOBAL, PSZ, bool, bool, int);
int      ExtractDate(char *, PDTP, int, int val[6]);

/**************************************************************************/
/*  Allocate the result structure that will contain result data.          */
/**************************************************************************/
PQRYRES PlgAllocResult(PGLOBAL g, int ncol, int maxres, int ids,
                       int *buftyp, XFLD *fldtyp, 
                       unsigned int *length, bool blank, bool nonull);

/***********************************************************************/
/*  Exported utility routines.                                         */
/***********************************************************************/
DllExport FILE   *PlugOpenFile(PGLOBAL, LPCSTR, LPCSTR);
DllExport int     PlugCloseFile(PGLOBAL, PFBLOCK, bool all = false);
DllExport void    PlugCleanup(PGLOBAL, bool);
DllExport bool    GetPromptAnswer(PGLOBAL, char *);
DllExport char   *GetAmName(PGLOBAL g, AMT am, void *memp = NULL);
DllExport PDBUSER PlgMakeUser(PGLOBAL g);
DllExport PDBUSER PlgGetUser(PGLOBAL g);
DllExport PCATLG  PlgGetCatalog(PGLOBAL g, bool jump = true);
DllExport bool    PlgSetXdbPath(PGLOBAL g, PSZ, PSZ, char *, int, char *, int);
DllExport void    PlgDBfree(MBLOCK&);
//lExport PSZ     GetIniString(PGLOBAL, void *, LPCSTR, LPCSTR, LPCSTR, LPCSTR);
//lExport int     GetIniSize(char *, char *, char *, char *);
//lExport bool    WritePrivateProfileInt(LPCSTR, LPCSTR, int, LPCSTR);
DllExport void    NewPointer(PTABS, void *, void *);
DllExport char    *GetIni(int n= 0);
DllExport void    SetTrc(void);

#define MSGID_NONE                         0
#define MSGID_CANNOT_OPEN                  1
#define MSGID_OPEN_MODE_ERROR              2
#define MSGID_OPEN_STRERROR                3
#define MSGID_OPEN_ERROR_AND_STRERROR      4
#define MSGID_OPEN_MODE_STRERROR           5
#define MSGID_OPEN_EMPTY_FILE              6

FILE *global_fopen(GLOBAL *g, int msgid, const char *path, const char *mode);
int global_open(GLOBAL *g, int msgid, const char *filename, int flags);
int global_open(GLOBAL *g, int msgid, const char *filename, int flags, int mode);
DllExport LPCSTR PlugSetPath(LPSTR to, LPCSTR name, LPCSTR dir);
char *MakeEscape(PGLOBAL g, char* str, char q);

bool PushWarning(PGLOBAL, PTDBASE, int level = 1);
