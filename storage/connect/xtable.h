/**************** Table H Declares Source Code File (.H) ***************/
/*  Name: TABLE.H    Version 2.3                                       */
/*                                                                     */
/*  (C) Copyright to the author Olivier BERTRAND          1999-2014    */
/*                                                                     */
/*  This file contains the TBX, OPJOIN and TDB class definitions.      */
/***********************************************************************/
#if !defined(TABLE_DEFINED)
#define      TABLE_DEFINED


/***********************************************************************/
/*  Include required application header files                          */
/*  block.h      is header containing Block    global declarations.    */
/***********************************************************************/
#include "assert.h"
#include "block.h"
#include "colblk.h"
#include "m_ctype.h"

typedef class CMD *PCMD;

// Commands executed by XDBC and MYX tables
class CMD : public BLOCK {
 public:
  // Constructor
  CMD(PGLOBAL g, char *cmd) {
    Cmd = (char*)PlugSubAlloc(g, NULL, strlen(cmd) + 1);
    strcpy(Cmd, cmd); Next = NULL; }

  // Members
  PCMD  Next;
  char *Cmd;
}; // end of class CMD

// Filter passed all tables
typedef struct _filter {
  char  *Body;
  OPVAL  Op;
  PCMD   Cmds;
} FILTER, *PFIL;

typedef class TDBCAT *PTDBCAT;
typedef class CATCOL *PCATCOL;

/***********************************************************************/
/*  Definition of class TBX (pure virtual class for TDB and OPJOIN)    */
/***********************************************************************/
class DllExport TBX: public BLOCK { // Base class for OPJOIN and TDB classes.
 public:
  // Constructors
  TBX(void);
  TBX(PTBX txp);

  // Implementation
  inline  PTBX  GetOrig(void) {return To_Orig;}
  inline  TUSE  GetUse(void) {return Use;}
  inline  void  SetUse(TUSE n) {Use = n;}
  inline  PFIL  GetFilter(void) {return To_Filter;}
  inline  void  SetOrig(PTBX txp) {To_Orig = txp;}
  inline  void  SetFilter(PFIL fp) {To_Filter = fp;}

  // Methods
  virtual bool IsSame(PTBX tp) {return tp == this;}
  virtual int  GetTdb_No(void) = 0;   // Convenience during conversion
  virtual PTDB GetNext(void) = 0;
  virtual int  Cardinality(PGLOBAL) = 0;
  virtual int  GetMaxSize(PGLOBAL) = 0;
  virtual int  GetProgMax(PGLOBAL) = 0;
  virtual int  GetProgCur(void) = 0;
  virtual int  GetBadLines(void) {return 0;}
  virtual PTBX Copy(PTABS t) = 0;

 protected:
//virtual void PrepareFilters(PGLOBAL g) = 0;

 protected:
  // Members
  PTBX  To_Orig;      // Pointer to original if it is a copy
  PFIL  To_Filter;
  TUSE  Use;
  }; // end of class TBX

/***********************************************************************/
/*  Definition of class TDB with all its method functions.             */
/***********************************************************************/
class DllExport TDB: public TBX {     // Table Descriptor Block.
 public:
  // Constructors
  TDB(PTABDEF tdp = NULL);
  TDB(PTDB tdbp);

  // Implementation
  static  void   SetTnum(int n) {Tnum = n;}
  inline  LPCSTR GetName(void) {return Name;}
  inline  PTABLE GetTable(void) {return To_Table;}
  inline  PCOL   GetColumns(void) {return Columns;}
  inline  int    GetDegree(void) {return Degree;}
  inline  MODE   GetMode(void) {return Mode;}
  inline  void   SetNext(PTDB tdbp) {Next = tdbp;}
  inline  void   SetName(LPCSTR name) {Name = name;}
  inline  void   SetTable(PTABLE tablep) {To_Table = tablep;}
  inline  void   SetColumns(PCOL colp) {Columns = colp;}
  inline  void   SetDegree(int degree) {Degree = degree;}
  inline  void   SetMode(MODE mode) {Mode = mode;}

  //Properties
  virtual int    GetTdb_No(void) {return Tdb_No;}
  virtual PTDB   GetNext(void) {return Next;}
  virtual PCATLG GetCat(void) {return NULL;}

  // Methods
  virtual AMT    GetAmType(void) {return TYPE_AM_ERROR;}
  virtual bool   GetBlockValues(PGLOBAL g) {return false;}
  virtual int    Cardinality(PGLOBAL g) {return (g) ? -1 : 0;}
  virtual int    RowNumber(PGLOBAL g, bool b = false);
  virtual bool   IsReadOnly(void) {return true;}
  virtual const CHARSET_INFO *data_charset() { return NULL; }
  virtual PTDB   Duplicate(PGLOBAL g) {return NULL;}
  virtual PTDB   CopyOne(PTABS t) {return this;}
  virtual PTBX   Copy(PTABS t);
  virtual void   PrintAM(FILE *f, char *m) 
                  {fprintf(f, "%s AM(%d)\n",  m, GetAmType());}
  virtual void   Print(PGLOBAL g, FILE *f, uint n);
  virtual void   Print(PGLOBAL g, char *ps, uint z);
  virtual PSZ    GetServer(void) = 0;

  // Database pure virtual routines
  virtual PCOL   ColDB(PGLOBAL g, PSZ name, int num) = 0;
  virtual void   MarkDB(PGLOBAL, PTDB) = 0;
  virtual bool   OpenDB(PGLOBAL) = 0;
  virtual int    ReadDB(PGLOBAL) = 0;
  virtual int    WriteDB(PGLOBAL) = 0;
  virtual int    DeleteDB(PGLOBAL, int) = 0;
  virtual void   CloseDB(PGLOBAL) = 0;
  virtual int    CheckWrite(PGLOBAL g) {return 0;}

  // Database routines
  bool OpenTable(PGLOBAL g, PSQL sqlp, MODE mode);
  void CloseTable(PGLOBAL g);

 protected:
  // Members
  static int Tnum;     // Used to generate Tdb_no's
  const  int Tdb_No;   // GetTdb_No() is always 0 for OPJOIN
  PTDB   Next;         // Next in linearized queries
  PTABLE To_Table;     // Points to the XTAB object
  LPCSTR Name;         // Table name
  PCOL   Columns;      // Points to the first column of the table
  MODE   Mode;         // 10 Read, 30 Update, 40 Insert, 50 Delete
  int    Degree;       // Number of columns
  }; // end of class TDB

/***********************************************************************/
/*  This is the base class for all query tables (except decode).       */
/***********************************************************************/
class DllExport TDBASE : public TDB {
  friend class INDEXDEF;
  friend class XINDEX;
  friend class XINDXS;
 public:
  // Constructor
  TDBASE(PTABDEF tdp = NULL);
  TDBASE(PTDBASE tdbp);

  // Implementation
  inline  int     GetKnum(void) {return Knum;}
  inline  PTABDEF GetDef(void) {return To_Def;}
  inline  PKXBASE GetKindex(void) {return To_Kindex;}
  inline  PCOL    GetSetCols(void) {return To_SetCols;}
  inline  void    SetSetCols(PCOL colp) {To_SetCols = colp;}

  // Properties
  void    SetKindex(PKXBASE kxp);
  PCOL    Key(int i) {return (To_Key_Col) ? To_Key_Col[i] : NULL;}

  // Methods
  virtual bool   IsUsingTemp(PGLOBAL g) {return false;}
  virtual PCATLG GetCat(void);
  virtual PSZ    GetPath(void);
  virtual void   PrintAM(FILE *f, char *m);
  virtual RECFM  GetFtype(void) {return RECFM_NAF;}
  virtual int    GetAffectedRows(void) {return -1;}
  virtual int    GetRecpos(void) = 0;
  virtual bool   SetRecpos(PGLOBAL g, int recpos);
  virtual bool   IsReadOnly(void) {return Read_Only;}
  virtual bool   IsView(void) {return FALSE;}
  virtual CHARSET_INFO *data_charset(void);
  virtual int    GetProgMax(PGLOBAL g) {return GetMaxSize(g);}
  virtual int    GetProgCur(void) {return GetRecpos();}
  virtual PSZ    GetFile(PGLOBAL g) {return "Not a file";}
  virtual int    GetRemote(void) {return 0;}
  virtual void   SetFile(PGLOBAL g, PSZ fn) {}
  virtual void   ResetDB(void) {}
  virtual void   ResetSize(void) {MaxSize = -1;}
  virtual void   RestoreNrec(void) {}
  virtual int    ResetTableOpt(PGLOBAL g, bool dox);
  virtual PSZ    GetServer(void) {return "Current";}

  // Database routines
  virtual PCOL ColDB(PGLOBAL g, PSZ name, int num);
  virtual PCOL MakeCol(PGLOBAL, PCOLDEF, PCOL, int)
                      {assert(false); return NULL;}
  virtual PCOL InsertSpecialColumn(PGLOBAL g, PCOL colp);
  virtual PCOL InsertSpcBlk(PGLOBAL g, PCOLDEF cdp);
  virtual void MarkDB(PGLOBAL g, PTDB tdb2);

 protected:
  // Members
  PTABDEF  To_Def;            // Points to catalog description block
  PXOB    *To_Link;           // Points to column of previous relations
  PCOL    *To_Key_Col;        // Points to key columns in current file
  PKXBASE  To_Kindex;         // Points to table key index
  PCOL     To_SetCols;        // Points to updated columns
  int      MaxSize;            // Max size in number of lines
  int      Knum;              // Size of key arrays
  bool     Read_Only;         // True for read only tables
  const CHARSET_INFO *m_data_charset;
  }; // end of class TDBASE

/***********************************************************************/
/*  The abstract base class declaration for the catalog tables.        */
/***********************************************************************/
class DllExport TDBCAT : public TDBASE {
  friend class CATCOL;
 public:
  // Constructor
  TDBCAT(PTABDEF tdp);

  // Implementation
  virtual AMT  GetAmType(void) {return TYPE_AM_CAT;}

  // Methods
  virtual int  GetRecpos(void) {return N;}
  virtual int  GetProgCur(void) {return N;}
  virtual int  RowNumber(PGLOBAL g, bool b = false) {return N + 1;}
  virtual bool SetRecpos(PGLOBAL g, int recpos); 

  // Database routines
  virtual PCOL MakeCol(PGLOBAL g, PCOLDEF cdp, PCOL cprec, int n);
  virtual int  GetMaxSize(PGLOBAL g);
  virtual bool OpenDB(PGLOBAL g);
  virtual int  ReadDB(PGLOBAL g);
  virtual int  WriteDB(PGLOBAL g);
  virtual int  DeleteDB(PGLOBAL g, int irc);
  virtual void CloseDB(PGLOBAL g);

 protected:
  // Specific routines
  virtual PQRYRES GetResult(PGLOBAL g) = 0;
          bool Initialize(PGLOBAL g);
          bool InitCol(PGLOBAL g);

  // Members
  PQRYRES Qrp;           
  int     N;                  // Row number
  bool    Init;          
  }; // end of class TDBCAT

/***********************************************************************/
/*  Class CATCOL: ODBC info column.                                    */
/***********************************************************************/
class DllExport CATCOL : public COLBLK {
  friend class TDBCAT;
 public:
  // Constructors
  CATCOL(PCOLDEF cdp, PTDB tdbp, int n);

  // Implementation
  virtual int  GetAmType(void) {return TYPE_AM_ODBC;}

  // Methods
  virtual void ReadColumn(PGLOBAL g);

 protected:
  CATCOL(void) {}              // Default constructor not to be used

  // Members
  PTDBCAT Tdbp;                // Points to ODBC table block
  PCOLRES Crp;                // The column data array
  int     Flag;
  }; // end of class CATCOL

#endif  // TABLE_DEFINED
