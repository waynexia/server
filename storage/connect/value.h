/**************** Value H Declares Source Code File (.H) ***************/
/*  Name: VALUE.H    Version 2.0                                       */
/*                                                                     */
/*  (C) Copyright to the author Olivier BERTRAND          2001-2013    */
/*                                                                     */
/*  This file contains the VALUE and derived classes declares.         */
/***********************************************************************/
#ifndef __VALUE__H__
#define __VALUE__H__

/***********************************************************************/
/*  Include required application header files                          */
/*  assert.h     is header required when using the assert function.    */
/*  block.h      is header containing Block    global declarations.    */
/***********************************************************************/
#include "assert.h"
#include "block.h"

/***********************************************************************/
/*  Types used in some class definitions.                              */
/***********************************************************************/
enum CONV {CNV_ANY     =   0,         /* Convert to any type           */
           CNV_CHAR    =   1,         /* Convert to character type     */
           CNV_NUM     =   2};        /* Convert to numeric type       */

/***********************************************************************/
/*  Types used in some class definitions.                              */
/***********************************************************************/
class CONSTANT;                       // For friend setting
typedef struct _datpar *PDTP;         // For DTVAL


/***********************************************************************/
/*  Utilities used to test types and to allocated values.              */
/***********************************************************************/
PVAL  AllocateValue(PGLOBAL, void *, short);

// Exported functions
DllExport PSZ   GetTypeName(int);
DllExport int   GetTypeSize(int, int);
#ifdef ODBC_SUPPORT
/* This function is exported for use in EOM table type DLLs */
DllExport int   TranslateSQLType(int stp, int prec, int& len, char& v);
#endif
DllExport char *GetFormatType(int);
DllExport int   GetFormatType(char);
DllExport bool  IsTypeChar(int type);
DllExport bool  IsTypeNum(int type);
//lExport int   ConvertType(int, int, CONV, bool match = false);
DllExport PVAL  AllocateValue(PGLOBAL, PVAL, int = TYPE_VOID, int = 0);
DllExport PVAL  AllocateValue(PGLOBAL, int, int len = 0, int prec = 0,
                              PSZ fmt = NULL);
DllExport ulonglong CharToNumber(char *, int, ulonglong, bool, 
                                 bool *minus = NULL, bool *rc = NULL);

/***********************************************************************/
/*  Class VALUE represents a constant or variable of any valid type.   */
/***********************************************************************/
class DllExport VALUE : public BLOCK {
  friend class CONSTANT; // The only object allowed to use SetConstFormat
 public:
  // Constructors

  // Implementation
  virtual bool   IsTypeNum(void) = 0;
  virtual bool   IsZero(void) = 0;
  virtual bool   IsCi(void) {return false;}
  virtual bool   IsUnsigned(void) {return Unsigned;}
  virtual void   Reset(void) = 0;
  virtual int    GetSize(void) = 0;
  virtual int    GetValLen(void) = 0;
  virtual int    GetValPrec(void) = 0;
  virtual int    GetLength(void) {return 1;}
  virtual PSZ    GetCharValue(void) {assert(false); return NULL;}
  virtual char   GetTinyValue(void) {assert(false); return 0;}
  virtual uchar  GetUTinyValue(void) {assert(false); return 0;}
  virtual short  GetShortValue(void) {assert(false); return 0;}
  virtual ushort GetUShortValue(void) {assert(false); return 0;}
  virtual int    GetIntValue(void) = 0;
  virtual uint   GetUIntValue(void) = 0;
  virtual longlong GetBigintValue(void) = 0;
  virtual ulonglong GetUBigintValue(void) = 0;
  virtual double GetFloatValue(void) = 0;
  virtual void  *GetTo_Val(void) = 0;
  virtual void   SetPrec(int prec) {Prec = prec;}
          bool   IsNull(void) {return Null;}
          void   SetNull(bool b) {Null = b;}
          bool   GetNullable(void) {return Nullable;}
          void   SetNullable(bool b) {Nullable = b;}
          int    GetType(void) {return Type;}
          int    GetClen(void) {return Clen;}
          void   SetGlobal(PGLOBAL g) {Global = g;}

  // Methods
  virtual bool   SetValue_pval(PVAL valp, bool chktype = false) = 0;
  virtual bool   SetValue_char(char *p, int n) = 0;
  virtual void   SetValue_psz(PSZ s) = 0;
  virtual void   SetValue(char c) {assert(false);}
  virtual void   SetValue(uchar c) {assert(false);}
  virtual void   SetValue(short i) {assert(false);}
  virtual void   SetValue(ushort i) {assert(false);}
  virtual void   SetValue(int n) {assert(false);}
  virtual void   SetValue(uint n) {assert(false);}
  virtual void   SetValue(longlong n) {assert(false);}
  virtual void   SetValue(ulonglong n) {assert(false);}
  virtual void   SetValue(double f) {assert(false);}
  virtual void   SetValue_pvblk(PVBLK blk, int n) = 0;
  virtual void   SetBinValue(void *p) = 0;
  virtual bool   GetBinValue(void *buf, int buflen, bool go) = 0;
  virtual char  *ShowValue(char *buf, int len = 0) = 0;
  virtual char  *GetCharString(char *p) = 0;
  virtual bool   IsEqual(PVAL vp, bool chktype) = 0;
  virtual bool   FormatValue(PVAL vp, char *fmt) = 0;

 protected:
  virtual bool   SetConstFormat(PGLOBAL, FORMAT&) = 0;
  const   char  *GetXfmt(void);

  // Constructor used by derived classes
  VALUE(int type, bool un = false);

  // Members
  PGLOBAL     Global;               // To reduce arglist
  const char *Fmt;
  const char *Xfmt;
  bool        Nullable;             // True if value can be null
  bool        Null;                 // True if value is null
  bool        Unsigned;             // True if unsigned
  int         Type;                 // The value type
  int         Clen;                 // Internal value length
  int         Prec;
  }; // end of class VALUE

/***********************************************************************/
/*  Class TYPVAL: represents a typed value.                            */
/***********************************************************************/
template <class TYPE>
class DllExport TYPVAL : public VALUE {
 public:
  // Constructor
  TYPVAL(TYPE n, int type, int prec = 0, bool un = false);

  // Implementation
  virtual bool   IsTypeNum(void) {return true;}
  virtual bool   IsZero(void) {return Tval == 0;}
  virtual void   Reset(void) {Tval = 0;}
  virtual int    GetValLen(void);
  virtual int    GetValPrec() {return 0;}
  virtual int    GetSize(void) {return sizeof(TYPE);}
  virtual PSZ    GetCharValue(void) {return VALUE::GetCharValue();}
  virtual char   GetTinyValue(void) {return (char)Tval;}
  virtual uchar  GetUTinyValue(void) {return (uchar)Tval;}
  virtual short  GetShortValue(void) {return (short)Tval;}
  virtual ushort GetUShortValue(void) {return (ushort)Tval;}
  virtual int    GetIntValue(void) {return (int)Tval;}
  virtual uint   GetUIntValue(void) {return (uint)Tval;}
  virtual longlong GetBigintValue(void) {return (longlong)Tval;}
  virtual ulonglong GetUBigintValue(void) {return (ulonglong)Tval;}
  virtual double GetFloatValue(void) {return (double)Tval;}
  virtual void  *GetTo_Val(void) {return &Tval;}

  // Methods
  virtual bool   SetValue_pval(PVAL valp, bool chktype);
  virtual bool   SetValue_char(char *p, int n);
  virtual void   SetValue_psz(PSZ s);
  virtual void   SetValue(char c) {Tval = (TYPE)c; Null = false;}
  virtual void   SetValue(uchar c) {Tval = (TYPE)c; Null = false;}
  virtual void   SetValue(short i) {Tval = (TYPE)i; Null = false;}
  virtual void   SetValue(ushort i) {Tval = (TYPE)i; Null = false;}
  virtual void   SetValue(int n) {Tval = (TYPE)n; Null = false;}
  virtual void   SetValue(uint n) {Tval = (TYPE)n; Null = false;}
  virtual void   SetValue(longlong n) {Tval = (TYPE)n; Null = false;}
  virtual void   SetValue(ulonglong n) {Tval = (TYPE)n; Null = false;}
  virtual void   SetValue(double f) {Tval = (TYPE)f; Null = false;}
  virtual void   SetValue_pvblk(PVBLK blk, int n);
  virtual void   SetBinValue(void *p);
  virtual bool   GetBinValue(void *buf, int buflen, bool go);
  virtual char  *ShowValue(char *buf, int);
  virtual char  *GetCharString(char *p);
  virtual bool   IsEqual(PVAL vp, bool chktype);
  virtual bool   SetConstFormat(PGLOBAL, FORMAT&);
  virtual bool   FormatValue(PVAL vp, char *fmt);
  virtual void   Print(PGLOBAL g, FILE *, uint);
  virtual void   Print(PGLOBAL g, char *, uint);

 protected:
  // Default constructor not to be used
  TYPVAL(void) : VALUE(TYPE_ERROR) {}

  // Specialized functions
  static  ulonglong MaxVal(void);
          TYPE   GetTypedValue(PVAL vp);
          TYPE   GetTypedValue(PVBLK blk, int n);
//        TYPE   GetTypedValue(PSZ s);

  // Members
  TYPE        Tval;
  }; // end of class TYPVAL

/***********************************************************************/
/*  Specific STRING class.                                             */
/***********************************************************************/
template <>
class DllExport TYPVAL<PSZ>: public VALUE { 
 public:
  // Constructors
  TYPVAL(PSZ s);
  TYPVAL(PGLOBAL g, PSZ s, int n, int c);

  // Implementation
  virtual bool   IsTypeNum(void) {return false;}
  virtual bool   IsZero(void) {return *Strp == 0;}
  virtual void   Reset(void) {*Strp = 0;}
  virtual int    GetValLen(void) {return Len;};
  virtual int    GetValPrec() {return (Ci) ? 1 : 0;}
  virtual int    GetSize(void) {return (Strp) ? strlen(Strp) : 0;}
  virtual PSZ    GetCharValue(void) {return Strp;}
  virtual char   GetTinyValue(void);
  virtual uchar  GetUTinyValue(void);
  virtual short  GetShortValue(void);
  virtual ushort GetUShortValue(void);
  virtual int    GetIntValue(void);
  virtual uint   GetUIntValue(void);
  virtual longlong GetBigintValue(void);
  virtual ulonglong GetUBigintValue(void);
  virtual double GetFloatValue(void) {return atof(Strp);}
  virtual void  *GetTo_Val(void) {return Strp;}
  virtual void   SetPrec(int prec) {Ci = prec != 0;}

  // Methods
  virtual bool   SetValue_pval(PVAL valp, bool chktype);
  virtual bool   SetValue_char(char *p, int n);
  virtual void   SetValue_psz(PSZ s);
  virtual void   SetValue_pvblk(PVBLK blk, int n);
  virtual void   SetValue(char c);
  virtual void   SetValue(uchar c);
  virtual void   SetValue(short i);
  virtual void   SetValue(ushort i);
  virtual void   SetValue(int n);
  virtual void   SetValue(uint n);
  virtual void   SetValue(longlong n);
  virtual void   SetValue(ulonglong n);
  virtual void   SetValue(double f);
  virtual void   SetBinValue(void *p);
  virtual bool   GetBinValue(void *buf, int buflen, bool go);
  virtual char  *ShowValue(char *buf, int);
  virtual char  *GetCharString(char *p);
  virtual bool   IsEqual(PVAL vp, bool chktype);
  virtual bool   FormatValue(PVAL vp, char *fmt);
  virtual bool   SetConstFormat(PGLOBAL, FORMAT&);

  // Members
  PSZ         Strp;
  bool        Ci;                   // true if case insensitive
  int         Len;
  }; // end of class TYPVAL<PSZ>

/***********************************************************************/
/*  Class DTVAL: represents a time stamp value.                        */
/***********************************************************************/
class DllExport DTVAL : public TYPVAL<int> {
 public:
  // Constructors
  DTVAL(PGLOBAL g, int n, int p, PSZ fmt);
  DTVAL(PGLOBAL g, PSZ s, int n);
  DTVAL(PGLOBAL g, short i);
  DTVAL(PGLOBAL g, int n);
  DTVAL(PGLOBAL g, longlong n);
  DTVAL(PGLOBAL g, double f);

  // Implementation
  virtual bool   SetValue_pval(PVAL valp, bool chktype);
  virtual bool   SetValue_char(char *p, int n);
  virtual void   SetValue_psz(PSZ s);
  virtual void   SetValue_pvblk(PVBLK blk, int n);
  virtual char  *GetCharString(char *p);
  virtual char  *ShowValue(char *buf, int);
  virtual bool   FormatValue(PVAL vp, char *fmt);
          bool   SetFormat(PGLOBAL g, PSZ fmt, int len, int year = 0);
          bool   SetFormat(PGLOBAL g, PVAL valp);
          bool   IsFormatted(void) {return Pdtp != NULL;}
//        bool   GetTmMember(OPVAL op, int& mval);
//        bool   DateDiff(DTVAL *dtp, OPVAL op, int& tdif);
          bool   MakeTime(struct tm *ptm);
  static  void   SetTimeShift(void);
  static  int    GetShift(void) {return Shift;}

  // Methods
          bool   MakeDate(PGLOBAL g, int *val, int nval);
//        bool   WeekNum(PGLOBAL g, int& nval);

  struct  tm    *GetGmTime(struct tm *);

 protected:
  // Default constructor not to be used
  DTVAL(void) : TYPVAL<int>() {}

  // Members
  static int    Shift;        // Time zone shift in seconds
  PDTP          Pdtp;         // To the DATPAR structure
  char         *Sdate;        // Utility char buffer
  int           DefYear;      // Used by ExtractDate
  int           Len;          // Used by CHAR scalar function
  }; // end of class DTVAL

#endif // __VALUE__H__
