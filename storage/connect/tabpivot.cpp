/************ TabPivot C++ Program Source Code File (.CPP) *************/
/* PROGRAM NAME: TABPIVOT                                              */
/* -------------                                                       */
/*  Version 1.4                                                        */
/*                                                                     */
/* COPYRIGHT:                                                          */
/* ----------                                                          */
/*  (C) Copyright to the author Olivier BERTRAND          2005-2013    */
/*                                                                     */
/* WHAT THIS PROGRAM DOES:                                             */
/* -----------------------                                             */
/*  This program are the PIVOT classes DB execution routines.          */
/***********************************************************************/

/***********************************************************************/
/*  Include relevant sections of the operating system header file.     */
/***********************************************************************/
#include "my_global.h"
#if defined(WIN32)
#if defined(__BORLANDC__)
#define __MFC_COMPAT__                   // To define min/max as macro
#endif
//#include <windows.h>
#elif defined(UNIX)
#include <errno.h>
#include <unistd.h>
#include "osutil.h"
#else
#include <io.h>
#endif

/***********************************************************************/
/*  Include application header files:                                  */
/*  global.h    is header containing all global declarations.          */
/*  plgdbsem.h  is header containing the DB application declarations.  */
/***********************************************************************/
#define FRM_VER 6
#include "table.h"       // MySQL table definitions
#include "sql_const.h"
#include "field.h"
#include "global.h"
#include "plgdbsem.h"
#include "xtable.h"
//#include "xindex.h"
#include "tabcol.h"
#include "colblk.h"
//#include "tabmysql.h"
#include "myconn.h"
#include "csort.h"
#include "tabutil.h"
#include "tabpivot.h"
#include "valblk.h"
#include "ha_connect.h"
#include "mycat.h"       // For GetHandler

extern "C" int trace;

#if 0
/***********************************************************************/
/*  Prepare the source table Query.                                    */
/***********************************************************************/
PQRYRES TDBPIVOT::GetSourceTable(PGLOBAL g)
  {
  if (Qryp)
    return Qryp;             // Already done

  if (Tabname) {
    char   *def, *colist;
    size_t  len = 0;
    PCOL    colp;
    PDBUSER dup = (PDBUSER)g->Activityp->Aptr;

    if (InitTable(g))
      return NULL;

    // Evaluate the length of the column list
    for (colp = Tdbp->Columns; colp; colp = colp->GetNext())
      len += (strlen(colp->GetName()) + 2);

    *(colist = (char*)PlugSubAlloc(g, NULL, len)) = 0;

    // Locate the suballocated string (size is not known yet)
    def = (char*)PlugSubAlloc(g, NULL, 0);
    strcpy(def, "SELECT ");
      
    if (!Fncol) {
      for (colp = Tdbp->Columns; colp; colp = colp->GetNext())
        if (!Picol || stricmp(Picol, colp->GetName()))
          Fncol = colp->GetName();

      if (!Fncol) {
        strcpy(g->Message, MSG(NO_DEF_FNCCOL));
        return NULL;
        } // endif Fncol

    }  else if (!(ColDB(g, Fncol, 0))) {
      // Function column not found in table                                       
      sprintf(g->Message, MSG(COL_ISNOT_TABLE), Fncol, Tabname);
      return NULL;
    } // endif Fcolp

    if (!Picol) {
      // Find default Picol as the last one not equal  to Fncol
      for (colp = Tdbp->Columns; colp; colp = colp->GetNext())
        if (!Fncol || stricmp(Fncol, colp->GetName()))
          Picol = colp->GetName();

      if (!Picol) {
        strcpy(g->Message, MSG(NO_DEF_PIVOTCOL));
        return NULL;
        } // endif Picol

    } else if (!(ColDB(g, Picol, 0))) {
      // Pivot column not found in table                                       
      sprintf(g->Message, MSG(COL_ISNOT_TABLE), Picol, Tabname);
      return NULL;
    } // endif Xcolp

    // Make the other column list
    for (colp = Columns; colp; colp = colp->GetNext())
      if (stricmp(Picol, colp->GetName()) &&
          stricmp(Fncol, colp->GetName()))
        strcat(strcat(colist, colp->GetName()), ", ");

    // Add the Pivot column at the end of the list
    strcat(strcat(def, strcat(colist, Picol)), ", ");

    // Continue making the definition
    if (!GBdone) {
      // Make it suitable for Pivot by doing the group by
      strcat(strcat(def, Function), "(");
      strcat(strcat(strcat(def, Fncol), ") "), Fncol);
      strcat(strcat(def, " FROM "), Tabname);
      strcat(strcat(def, " GROUP BY "), colist);
    } else   // Gbdone
      strcat(strcat(strcat(def, Fncol), " FROM "), Tabname);

    // Now we know how much was suballocated
    Tabsrc = (char*)PlugSubAlloc(g, NULL, strlen(def));
  } else {
    strcpy(g->Message, MSG(SRC_TABLE_UNDEF));
    return NULL;
  } // endif Tabsrc

  int w;

  // Open a MySQL connection for this table
  if (Myc.Open(g, Host, Database, User, Pwd, Port))
    return NULL;

  // Send the source command to MySQL
  if (Myc.ExecSQL(g, Tabsrc, &w) == RC_FX) {
    Myc.Close();
    return NULL;
    } // endif Exec

  // We must have a storage query to get pivot column values
  Qryp = Myc.GetResult(g);
  Myc.Close();
  Tqrp = new(g) TDBQRS(Qryp);
  Tqrp->OpenDB(g);

  if (MakePivotColumns(g) < 0)
    return NULL;

  return Qryp;
  } // end of GetSourceTable

/***********************************************************************/
/*  Allocate PIVOT columns description block.                          */
/***********************************************************************/
int TDBPIVOT::MakePivotColumns(PGLOBAL g)
  {
  if (Mult < 0) {
    int     ndif, n = 0, nblin = Qryp->Nblin;
    PVAL    valp;
    PCOL    cp;
    PSRCCOL colp;
    PFNCCOL cfnp;

    // Allocate all the source columns
    Tqrp->ColDB(g, NULL, 0);
    Columns = NULL;        // Discard dummy columns blocks

    for (cp = Tqrp->GetColumns(); cp; cp = cp->GetNext()) {
      if (cp->InitValue(g))
        return -1;

      if (!stricmp(cp->GetName(), Picol)) {
        Xcolp = (PQRSCOL)cp;
        Xresp = Xcolp->GetCrp();
        Rblkp = Xresp->Kdata;
      } else if (!stricmp(cp->GetName(), Fncol)) {
        Fcolp = (PQRSCOL)cp;
      } else
        if ((colp = new(g) SRCCOL(cp, this, ++n))->Init(g, this))
          return -1;

      } // endfor cp

    if (!Xcolp) {
      sprintf(g->Message, MSG(COL_ISNOT_TABLE), 
              Picol, Tabname ? Tabname : "TabSrc");
      return -1;
    } else if (!Fcolp) {
      sprintf(g->Message, MSG(COL_ISNOT_TABLE), 
              Fncol, Tabname ? Tabname : "TabSrc");
      return -1;
    } // endif Fcolp

    // Before calling sort, initialize all
    Index.Size = nblin * sizeof(int);
    Index.Sub = TRUE;                   // Should be small enough

    if (!PlgDBalloc(g, NULL, Index))
      return -1;

    Offset.Size = (nblin + 1) * sizeof(int);
    Offset.Sub = TRUE;                 // Should be small enough

    if (!PlgDBalloc(g, NULL, Offset))
      return -2;

    ndif = Qsort(g, nblin);

    if (ndif < 0) {           // error
      return -3;
    } else
      Ncol = ndif;

    // Now make the functional columns
    for (int i = 0; i < Ncol; i++) {
      // Allocate the Value used to retieve column names
      if (!(valp = AllocateValue(g, Xcolp->GetResultType(),
          Xcolp->GetLengthEx(),  Xcolp->GetPrecision(),
          Xcolp->GetDomain(), Xcolp->GetTo_Tdb()->GetCat())))
        return -4;

      // Get the value that will be the generated column name
      valp->SetValue_pvblk(Rblkp, Pex[Pof[i]]);

      // Copy the functional column with new Name and new Value
      cfnp = (PFNCCOL)new(g) FNCCOL(Fcolp, this);

      // Initialize the generated column
      if (cfnp->InitColumn(g, valp))
        return -5;

      } // endfor i

    // Fields must be updated for ha_connect
//  if (UpdateTableFields(g, n + Ncol))
//    return -6;

    // This should be refined later
    Mult = nblin;
    } // endif Mult

  return Mult;
  } // end of MakePivotColumns

/***********************************************************************/
/*  Update fields in the MySQL table structure                         */
/*  Note: this does not work. Indeed the new rows are correctly made   */
/*  but the final result still specify the unmodified table and the    */
/*  returned table only contains the original column values.           */
/*  In addition, a new query on the table, when it is into the cache,  */
/*  specifies all the new columns and fails because they do not belong */
/*  to the original table.                                             */
/***********************************************************************/
bool TDBPIVOT::UpdateTableFields(PGLOBAL g, int n)
  {
  uchar  *trec, *srec, *tptr, *sptr;
  int     i = 0, k = 0;
  uint    len;
  uint32  nmp, lwm;
  size_t  buffsize;
  PCOL    colp;
  PHC      hc = ((MYCAT*)((PIVOTDEF*)To_Def)->Cat)->GetHandler();
  TABLE   *table = hc->GetTable();
  st_mem_root *tmr = &table->mem_root;
  st_mem_root *smr = &table->s->mem_root;
  Field* *field;
  Field  *fp, *tfncp, *sfncp;
  Field* *ntf;
  Field* *nsf;
//my_bitmap_map   *org_bitmap;
  const MY_BITMAP *map;

  // When sorting read_set selects all columns, so we use def_read_set
  map= (const MY_BITMAP *)&table->def_read_set;

  // Find the function field 
  for (field= table->field; *field; field++) {
    fp= *field;

    if (bitmap_is_set(map, fp->field_index))
      if (!stricmp(fp->field_name, Fncol)) {
        tfncp = fp;
        break;
        } // endif Name

    } // endfor field

  for (field= table->s->field; *field; field++) {
    fp= *field;

    if (bitmap_is_set(map, fp->field_index))
      if (!stricmp(fp->field_name, Fncol)) {
        sfncp = fp;
        break;
        } // endif Name

    } // endfor field

  // Calculate the new buffer size
  len = tfncp->max_data_length();
   buffsize = table->s->rec_buff_length + len * Ncol;

  // Allocate the new record space
  if (!(tptr = trec = (uchar*)alloc_root(tmr, 2 * buffsize)))
    return TRUE;

  if (!(sptr = srec = (uchar*)alloc_root(smr, 2 * buffsize)))
    return TRUE;


  // Allocate the array of all new table field pointers
  if (!(ntf = (Field**)alloc_root(tmr, (uint)((n+1) * sizeof(Field*)))))
    return TRUE;

  // Allocate the array of all new table share field pointers
  if (!(nsf = (Field**)alloc_root(smr, (uint)((n+1) * sizeof(Field*)))))
    return TRUE;

  // First fields are the the ones of the source columns
  for (colp = Columns; colp; colp = colp->GetNext())
    if (colp->GetAmType() == TYPE_AM_SRC) {
      for (field= table->field; *field; field++) {
        fp= *field;

        if (bitmap_is_set(map, fp->field_index))
          if (!stricmp(colp->GetName(),  fp->field_name)) {
            ntf[i] = fp;
            fp->field_index =  i++;
            fp->ptr = tptr;
            tptr += fp->max_data_length();
            break;
            } // endif Name

        } // endfor field

      for (field= table->s->field; *field; field++) {
        fp= *field;

        if (bitmap_is_set(map, fp->field_index))
          if (!stricmp(colp->GetName(),  fp->field_name)) {
            nsf[k] = fp;
            fp->field_index =  k++;
            fp->ptr = srec;
            srec += fp->max_data_length();
            break;
            } // endif Name

        } // endfor field

      } // endif  AmType

  // Now add the pivot generated columns
  for (colp = Columns; colp; colp = colp->GetNext())
    if (colp->GetAmType() == TYPE_AM_FNC) {
      if ((fp = (Field*)memdup_root(tmr, (char*)tfncp, tfncp->size_of()))) {
        ntf[i] = fp;
        fp->ptr = tptr;
        fp->field_name = colp->GetName();
        fp->field_index = i++;
        fp->vcol_info = NULL;
        fp->stored_in_db = TRUE;
        tptr += len;
      } else
        return TRUE;

      if ((fp = (Field*)memdup_root(smr, (char*)sfncp, sfncp->size_of()))) {
        nsf[i] = fp;
        fp->ptr = sptr;
        fp->field_name = colp->GetName();
        fp->field_index = k++;
        fp->vcol_info = NULL;
        fp->stored_in_db = TRUE;
        sptr += len;
      } else
        return TRUE;

      } // endif AM_FNC

  // Mark end of the list
  ntf[i] = NULL;
  nsf[k] = NULL;

  // Update the table fields
  nmp = (uint32)((1<<i) - 1);
  lwm = (uint32)((-1)<<i);
  table->field = ntf;
  table->used_fields = i;
  table->record[0] = trec;
  table->record[1] = trec + buffsize;
  *table->def_read_set.bitmap = nmp;
  *table->def_read_set.last_word_ptr = nmp;
  table->def_read_set.last_word_mask = lwm;
  table->def_read_set.n_bits = i;
  *table->read_set->bitmap = nmp;
  *table->read_set->last_word_ptr = nmp;
  table->read_set->last_word_mask = lwm;
  table->read_set->n_bits = i;
  table->write_set->n_bits = i;
  *table->vcol_set->bitmap = 0;
  table->vcol_set->n_bits = i;

  // and the share fields
  table->s->field = nsf;
  table->s->reclength = sptr - srec;
  table->s->stored_rec_length = sptr - srec;
  table->s->fields = k;
  table->s->stored_fields = k;
  table->s->rec_buff_length = buffsize;
//table->s->varchar_fields = ???;
//table->s->db_record_offset = ???;
//table->s->null_field_first = ???;
  return FALSE;
  } // end of UpdateTableFields
#endif // 0

/* --------------- Implementation of the PIVOT classes --------------- */

/***********************************************************************/
/*  PIVOTDEF constructor.                                              */
/***********************************************************************/
  PIVOTDEF::PIVOTDEF(void) 
  {
  Host = User = Pwd = DB = NULL;
  Tabname = Tabsrc = Picol = Fncol = Function = NULL;
  GBdone = Accept = false;
  Port = 0;
  } // end of PIVOTDEF constructor

/***********************************************************************/
/*  DefineAM: define specific AM block values from PIVOT table.        */
/***********************************************************************/
bool PIVOTDEF::DefineAM(PGLOBAL g, LPCSTR am, int poff)
  {
  char *p1, *p2;
  PHC    hc = ((MYCAT*)Cat)->GetHandler();

  if (!PRXDEF::DefineAM(g, am, poff)) {
    Tabname = (char*)Tablep->GetName();
    DB = (char*)Tablep->GetQualifier();
  } else {
    DB = Cat->GetStringCatInfo(g, "Database", "*");
    Tabsrc = Cat->GetStringCatInfo(g, "SrcDef", NULL);
  } // endif

  Host = Cat->GetStringCatInfo(g, "Host", "localhost");
  User = Cat->GetStringCatInfo(g, "User", "*");
  Pwd = Cat->GetStringCatInfo(g, "Password", NULL);
  Picol = Cat->GetStringCatInfo(g, "PivotCol", NULL);
  Fncol = Cat->GetStringCatInfo(g, "FncCol", NULL);
  
  // If fncol is like avg(colname), separate Fncol and Function
  if (Fncol && (p1 = strchr(Fncol, '(')) && (p2 = strchr(p1, ')')) &&
      (*Fncol != '"') &&  (!*(p2+1))) {
    *p1++ = '\0'; *p2 = '\0';
    Function = Fncol;
    Fncol = p1;
  } else
    Function = Cat->GetStringCatInfo(g, "Function", "SUM");

  GBdone = Cat->GetBoolCatInfo("Groupby", false);
  Accept = Cat->GetBoolCatInfo("Accept", false);
  Port = Cat->GetIntCatInfo("Port", 3306);
  Desc = (Tabname) ? Tabname : Tabsrc;
  return FALSE;
  } // end of DefineAM

/***********************************************************************/
/*  GetTable: makes a new TDB of the proper type.                      */
/***********************************************************************/
PTDB PIVOTDEF::GetTable(PGLOBAL g, MODE m)
  {
  return new(g) TDBPIVOT(this);
  } // end of GetTable

/* ------------------------------------------------------------------- */

/***********************************************************************/
/*  Implementation of the TDBPIVOT class.                              */
/***********************************************************************/
TDBPIVOT::TDBPIVOT(PPIVOTDEF tdp) : TDBPRX(tdp)
  {
  Host = tdp->Host;
  Database = tdp->DB;
  User = tdp->User;
  Pwd = tdp->Pwd;
  Port = tdp->Port;
  Tabname = tdp->Tabname;    // Name of source table
  Tabsrc = tdp->Tabsrc;      // SQL description of source table
  Picol = tdp->Picol;        // Pivot column name
  Fncol = tdp->Fncol;        // Function column name
  Function = tdp->Function;  // Aggregate function name
  Xcolp = NULL;              // To the FNCCOL column
//Xresp = NULL;              // To the pivot result column
//Rblkp = NULL;              // The value block of the pivot column
  Fcolp = NULL;              // To the function column
  Dcolp = NULL;              // To the dump column
  GBdone = tdp->GBdone;
  Accept = tdp->Accept;
  Mult = -1;                // Estimated table size
  N = 0;                    // The current table index
  M = 0;                    // The occurence rank
  FileStatus = 0;           // Logical End-of-File
  RowFlag = 0;              // 0: Ok, 1: Same, 2: Skip
  } // end of TDBPIVOT constructor

/***********************************************************************/
/*  Prepare the source table Query.                                    */
/***********************************************************************/
bool TDBPIVOT::GetSourceTable(PGLOBAL g)
  {
  if (Tdbp)
    return false;             // Already done

  if (Tabname) {
    PTABDEF defp;
    PCOLDEF cdp;

    if (InitTable(g))
      return true;
    else
      defp = Tdbp->GetDef();

    if (!Fncol) {
      for (cdp = defp->GetCols(); cdp; cdp = cdp->GetNext())
        if (!Picol || stricmp(Picol, cdp->GetName()))
          Fncol = cdp->GetName();

      if (!Fncol) {
        strcpy(g->Message, MSG(NO_DEF_FNCCOL));
        return true;
        } // endif Fncol

      } // endif Fncol

    if (!Picol) {
      // Find default Picol as the last one not equal to Fncol
      for (cdp = defp->GetCols(); cdp; cdp = cdp->GetNext())
        if (!Fncol || stricmp(Fncol, cdp->GetName()))
          Picol = cdp->GetName();

      if (!Picol) {
        strcpy(g->Message, MSG(NO_DEF_PIVOTCOL));
        return true;
        } // endif Picol

      } // endif Picol

    if (!GBdone) {
      char  *colist;
  
      // Locate the suballocated colist (size is not known yet)
      *(colist = (char*)PlugSubAlloc(g, NULL, 0)) = 0;
  
      // Make the column list
      for (cdp = To_Def->GetCols(); cdp; cdp = cdp->GetNext())
        if (!cdp->GetOffset())
          strcat(strcat(colist, cdp->GetName()), ", ");
  
      // Add the Pivot column at the end of the list
      strcat(colist, Picol);
  
      // Now we know how much was suballocated
      PlugSubAlloc(g, NULL, strlen(colist));
  
      // Locate the source string (size is not known yet)
      Tabsrc = (char*)PlugSubAlloc(g, NULL, 0);

      // Start making the definition
      strcat(strcat(strcpy(Tabsrc, "SELECT "), colist), ", ");
  
      // Make it suitable for Pivot by doing the group by
      strcat(strcat(Tabsrc, Function), "(");
      strcat(strcat(strcat(Tabsrc, Fncol), ") "), Fncol);
      strcat(strcat(Tabsrc, " FROM "), Tabname);
      strcat(strcat(Tabsrc, " GROUP BY "), colist);
  
      // Now we know how much was suballocated
      PlugSubAlloc(g, NULL, strlen(Tabsrc));
      } // endif !GBdone

  } else if (!Tabsrc) {
    strcpy(g->Message, MSG(SRC_TABLE_UNDEF));
    return true;
  } // endif

  if (Tabsrc) {
    MYSQLC  myc;                    // MySQL connection class
    PQRYRES qryp;
    PCOLRES crp;
    int     w;

    // Open a MySQL connection for this table
    if (myc.Open(g, Host, Database, User, Pwd, Port))
      return true;

    // Send the source command to MySQL
    if (myc.ExecSQL(g, Tabsrc, &w) == RC_FX) {
      myc.Close();
      return true;
      } // endif Exec

    // We must have a storage query to get pivot column values
    qryp = myc.GetResult(g);
    myc.Close();
    Tdbp = new(g) TDBQRS(qryp);

    if (!Fncol) {
      for (crp = qryp->Colresp; crp; crp = crp->Next)
        if (!Picol || stricmp(Picol, crp->Name))
          Fncol = crp->Name;

      if (!Fncol) {
        strcpy(g->Message, MSG(NO_DEF_FNCCOL));
        return true;
        } // endif Fncol

      } // endif Fncol

    if (!Picol) {
      // Find default Picol as the last one not equal to Fncol
      for (crp = qryp->Colresp; crp; crp = crp->Next)
        if (!Fncol || stricmp(Fncol, crp->Name))
          Picol = crp->Name;

      if (!Picol) {
        strcpy(g->Message, MSG(NO_DEF_PIVOTCOL));
        return true;
        } // endif Picol

      } // endif Picol

    } // endif Tabsrc

  // Now it is time to allocate the pivot and function columns
  if (!(Fcolp = Tdbp->ColDB(g, Fncol, 0))) {
    // Function column not found in table                                       
    sprintf(g->Message, MSG(COL_ISNOT_TABLE), Fncol, Tabname);
    return true;
  } else if (Fcolp->InitValue(g))
    return true;

  if (!(Xcolp = Tdbp->ColDB(g, Picol, 0))) {
    // Pivot column not found in table                                       
    sprintf(g->Message, MSG(COL_ISNOT_TABLE), Picol, Tabname);
    return true;
  } else if (Xcolp->InitValue(g))
    return true;

  return false;
  } // end of GetSourceTable

/***********************************************************************/
/*  Allocate source column description block.                          */
/***********************************************************************/
PCOL TDBPIVOT::MakeCol(PGLOBAL g, PCOLDEF cdp, PCOL cprec, int n)
  {
  PCOL colp;

  if (cdp->GetOffset()) {
    colp = new(g) FNCCOL(cdp, this, cprec, n);

    if (cdp->GetOffset() > 1)
      Dcolp = colp;

  } else
    colp = new(g) SRCCOL(cdp, this, cprec, n);

  return colp;
  } // end of MakeCol

/***********************************************************************/
/*  PIVOT GetMaxSize: returns the maximum number of rows in the table. */
/***********************************************************************/
int TDBPIVOT::GetMaxSize(PGLOBAL g)
  {     
#if  0
  if (MaxSize < 0)
    MaxSize = MakePivotColumns(g);

  return MaxSize;
#endif // 0
  return 0;
  } // end of GetMaxSize

/***********************************************************************/
/*  In this sample, ROWID will be the (virtual) row number,            */
/*  while ROWNUM will be the occurence rank in the multiple column.    */
/***********************************************************************/
int TDBPIVOT::RowNumber(PGLOBAL g, bool b)
  {
  return (b) ? M : N;
  } // end of RowNumber
 
/***********************************************************************/
/*  PIVOT Access Method opening routine.                               */
/***********************************************************************/
bool TDBPIVOT::OpenDB(PGLOBAL g)
  {
//PDBUSER dup = (PDBUSER)g->Activityp->Aptr;

  if (Use == USE_OPEN) {
    /*******************************************************************/
    /*  Table already open, just replace it at its beginning.          */
    /*******************************************************************/
    N = M = 0;
    RowFlag = 0;
    FileStatus = 0;
    return FALSE;
    } // endif use

  if (Mode != MODE_READ) {
    /*******************************************************************/
    /* Currently PIVOT tables cannot be modified.                      */
    /*******************************************************************/
    sprintf(g->Message, MSG(TABLE_READ_ONLY), "PIVOT");
    return TRUE;
    } // endif Mode

  if (To_Key_Col || To_Kindex) {
    /*******************************************************************/
    /* Direct access of PIVOT tables is not implemented yet.           */
    /*******************************************************************/
    strcpy(g->Message, MSG(NO_PIV_DIR_ACC));
    return TRUE;
    } // endif To_Key_Col

  /*********************************************************************/
  /*  Do it here if not done yet (should not be the case).             */
  /*********************************************************************/
  if (GetSourceTable(g))
    return TRUE;

  /*********************************************************************/
  /*  Check and initialize the subtable columns.                       */
  /*********************************************************************/
  for (PCOL cp = Columns; cp; cp = cp->GetNext())
    if (cp->GetAmType() == TYPE_AM_SRC) {
      if (((PPRXCOL)cp)->Init(g))
        return TRUE;

    } else if (cp->GetAmType() == TYPE_AM_FNC)
      if (((PFNCCOL)cp)->InitColumn(g))
        return TRUE;

  /*********************************************************************/
  /*  Physically open the object table.                                */
  /*********************************************************************/
	if (Tdbp->OpenDB(g))
		return TRUE;

  return FALSE;
  } // end of OpenDB

/***********************************************************************/
/*  Data Base read routine for PIVOT access method.                    */
/***********************************************************************/
int TDBPIVOT::ReadDB(PGLOBAL g)
  {
  int  rc = RC_OK;
  bool newrow = FALSE;
  PCOL colp;

  if (FileStatus == 2)
    return RC_EF;

  if (FileStatus)
    for (colp = Columns; colp; colp = colp->GetNext())
      if (colp->GetAmType() == TYPE_AM_SRC)
        ((PSRCCOL)colp)->SetColumn();

  // New row, reset all function column values
  for (colp = Columns; colp; colp = colp->GetNext())
    if (colp->GetAmType() == TYPE_AM_FNC)
      colp->GetValue()->Reset();

  /*********************************************************************/
  /*  Now start the multi reading process.                             */
  /*********************************************************************/
  do {
    if (RowFlag != 1) {
      if ((rc = Tdbp->ReadDB(g)) != RC_OK) {
        if (FileStatus && rc == RC_EF) {
          // A prepared row remains to be sent
          FileStatus = 2;
          rc = RC_OK;
          } // endif FileStatus

        break;
        } // endif rc

      for (colp = Tdbp->GetColumns(); colp; colp = colp->GetNext())
        colp->ReadColumn(g);

      for (colp = Columns; colp; colp = colp->GetNext())
        if (colp->GetAmType() == TYPE_AM_SRC)
          if (FileStatus) {
            if (((PSRCCOL)colp)->CompareLast()) {
              newrow = (RowFlag) ? TRUE : FALSE;
              break;
              } // endif CompareLast

          } else
            ((PSRCCOL)colp)->SetColumn();

      FileStatus = 1;
      } // endif RowFlag

    if (newrow) {
      RowFlag = 1;
      break;
    } else
      RowFlag = 2;

    // Look for the column having this header
    for (colp = Columns; colp; colp = colp->GetNext())
      if (colp->GetAmType() == TYPE_AM_FNC) {
        if (((PFNCCOL)colp)->CompareColumn())
          break;

        } // endif AmType

    if (!colp && !(colp = Dcolp)) {
      if (!Accept) {
        strcpy(g->Message, MSG(NO_MATCH_COL));
        return RC_FX;
      } else
        continue;

      } // endif colp

    // Set the value of the matching column from the fonction value
    colp->GetValue()->SetValue_pval(Fcolp->GetValue());
    } while (RowFlag == 2);

  N++;
  return rc;
  } // end of ReadDB

/***********************************************************************/
/*  WriteDB: Data Base write routine for PIVOT access methods.         */
/***********************************************************************/
int TDBPIVOT::WriteDB(PGLOBAL g)
  {
  sprintf(g->Message, MSG(TABLE_READ_ONLY), "PIVOT");
  return RC_FX;
  } // end of WriteDB

/***********************************************************************/
/*  Data Base delete line routine for PIVOT access methods.            */
/***********************************************************************/
int TDBPIVOT::DeleteDB(PGLOBAL g, int irc)
  {
  sprintf(g->Message, MSG(NO_TABLE_DEL), "PIVOT");
  return RC_FX;
  } // end of DeleteDB

/***********************************************************************/
/*  Data Base close routine for PIVOT access method.                   */
/***********************************************************************/
void TDBPIVOT::CloseDB(PGLOBAL g)
  {
//Tdbp->CloseDB(g);
  } // end of CloseDB

#if 0
/***********************************************************************/
/*  TDBPIVOT: Compare routine for sorting pivot column values.         */
/***********************************************************************/
int TDBPIVOT::Qcompare(int *i1, int *i2)
  {
  // TODO: the actual comparison between pivot column result values.
  return Rblkp->CompVal(*i1, *i2);
  } // end of Qcompare
#endif // 0

// ------------------------ FNCCOL functions ----------------------------

/***********************************************************************/
/*  FNCCOL public constructor.                                       */
/***********************************************************************/
FNCCOL::FNCCOL(PCOLDEF cdp, PTDB tdbp, PCOL cprec, int i)
      : COLBLK(cdp, tdbp, i)
  {
  if (cprec) {
    Next = cprec->GetNext();
    cprec->SetNext(this);
  } else {
    Next = tdbp->GetColumns();
    tdbp->SetColumns(this);
  } // endif cprec

  Value = NULL;     // We'll get a new one later
  Hval = NULL;      // The unconverted header value
  Xcolp = NULL;
  } // end of FNCCOL constructor

/***********************************************************************/
/*  FNCCOL initialization function.                                    */
/***********************************************************************/
bool FNCCOL::InitColumn(PGLOBAL g)
{
  // Must have its own value block
  if (InitValue(g))
    return TRUE;

  // Make a value from the column name
  Hval = AllocateValue(g, Name, TYPE_STRING);
  Hval->SetPrec(1);         // Case insensitive

  Xcolp = ((PTDBPIVOT)To_Tdb)->Xcolp;
  AddStatus(BUF_READ);      // All is done here
  return FALSE;
} // end of InitColumn

/***********************************************************************/
/*  CompareColumn: Compare column value with source column value.      */
/***********************************************************************/
bool FNCCOL::CompareColumn(void)
  {
  // Compare the unconverted values
  return Hval->IsEqual(Xcolp->GetValue(), false);
  } // end of CompareColumn

// ------------------------ SRCCOL functions ----------------------------

/***********************************************************************/
/*  SRCCOL public constructor.                                         */
/***********************************************************************/
SRCCOL::SRCCOL(PCOLDEF cdp, PTDB tdbp, PCOL cprec, int n)
      : PRXCOL(cdp, tdbp, cprec, n)
  {
  // Set additional SRC access method information for column.
//Cnval = NULL;
  } // end of SRCCOL constructor

/***********************************************************************/
/*  Initialize the column as pointing to the source column.            */
/***********************************************************************/
bool SRCCOL::Init(PGLOBAL g)
  {
  if (PRXCOL::Init(g))
    return true;

  // Will contain the last value
//Cnval = AllocateValue(g, Value, TYPE_VOID);

  AddStatus(BUF_READ);     // All is done here
  return false;
  } // end of SRCCOL constructor

/***********************************************************************/
/*  SetColumn: have the column value set from the source column.       */
/***********************************************************************/
void SRCCOL::SetColumn(void)
  {
//Cnval->SetValue_pval(Value);
  Value->SetValue_pval(To_Val);
  } // end of SetColumn

/***********************************************************************/
/*  SetColumn: Compare column value with source column value.          */
/***********************************************************************/
bool SRCCOL::CompareLast(void)
  {
  // Compare the unconverted values
//return !Cnval->IsEqual(Colp->GetValue(), true);
  return !Value->IsEqual(To_Val, true);
  } // end of CompareColumn


/* ------------------------------------------------------------------- */

/***********************************************************************/
/*  Implementation of the TDBQRS class.                                */
/***********************************************************************/
TDBQRS::TDBQRS(PTDBQRS tdbp) : TDBASE(tdbp)
  {
  Qrp = tdbp->Qrp;
  CurPos = tdbp->CurPos;
  } // end of TDBQRS copy constructor

// Method
PTDB TDBQRS::CopyOne(PTABS t)
  {
  PTDB    tp;
  PQRSCOL cp1, cp2;
  PGLOBAL g = t->G;        // Is this really useful ???

  tp = new(g) TDBQRS(this);

  for (cp1 = (PQRSCOL)Columns; cp1; cp1 = (PQRSCOL)cp1->GetNext()) {
    cp2 = new(g) QRSCOL(cp1, tp);  // Make a copy
    NewPointer(t, cp1, cp2);
    } // endfor cp1

  return tp;
  } // end of CopyOne

#if 0    // The TDBASE functions return NULL when To_Def is NULL
/***********************************************************************/
/*  Return the pointer on the DB catalog this table belongs to.        */
/***********************************************************************/
PCATLG TDBQRS::GetCat(void)
  {
  // To_Def is null for QRYRES tables
  return NULL;
  }  // end of GetCat

/***********************************************************************/
/*  Return the datapath of the DB this table belongs to.               */
/***********************************************************************/
PSZ TDBQRS::GetPath(void)
  {
  // To_Def is null for QRYRES tables
  return NULL;
  }  // end of GetPath
#endif // 0

/***********************************************************************/
/*  Initialize QRS column description block construction.              */
/*        name is used to call columns by name.                        */
/*        num is used by LNA to construct columns by index number.     */
/*  Note: name=Null and num=0 for constructing all columns (select *)  */
/***********************************************************************/
PCOL TDBQRS::ColDB(PGLOBAL g, PSZ name, int num)
  {
  int     i;
  PCOLRES crp;
  PCOL    cp, colp = NULL, cprec = NULL;

  if (trace)
    htrc("QRS ColDB: colname=%s tabname=%s num=%d\n",
                    SVP(name), Name, num);

  for (crp = Qrp->Colresp, i = 1; crp; crp = crp->Next, i++)
    if ((!name && !num) ||
         (name && !stricmp(crp->Name, name)) || num == i) {
      // Check for existence of desired column
      // Also find where to insert the new block
      for (cp = Columns; cp; cp = cp->GetNext())
        if (cp->GetIndex() < i)
          cprec = cp;
        else if (cp->GetIndex() == i)
          break;

      if (trace) {
        if (cp)
          htrc("cp(%d).Name=%s cp=%p\n", i, cp->GetName(), cp);
        else
          htrc("cp(%d) cp=%p\n", i, cp);
        } // endif trace

      // Now take care of Column Description Block
      if (cp)
        colp = cp;
      else
        colp = new(g) QRSCOL(g, crp, this, cprec, i);

      if (name || num)
        break;
      else
        cprec = colp;

      } // endif Name

  return (colp);
  } // end of ColDB

/***********************************************************************/
/*  QRS GetMaxSize: returns maximum table size in number of lines.     */
/***********************************************************************/
int TDBQRS::GetMaxSize(PGLOBAL g)
  {
  MaxSize = Qrp->Maxsize;
  return MaxSize;
  } // end of GetMaxSize

/***********************************************************************/
/*  RowNumber: returns the current row ordinal number.                 */
/***********************************************************************/
int TDBQRS::RowNumber(PGLOBAL g, BOOL b)
  {
  return (CurPos + 1);
  } // end of RowNumber

/***********************************************************************/
/*  QRS Access Method opening routine.                                 */
/*  New method now that this routine is called recursively (last table */
/*  first in reverse order): index blocks are immediately linked to    */
/*  join block of next table if it exists or else are discarted.       */
/***********************************************************************/
bool TDBQRS::OpenDB(PGLOBAL g)
  {
  if (trace)
    htrc("QRS OpenDB: tdbp=%p tdb=R%d use=%d key=%p mode=%d\n",
                      this, Tdb_No, Use, To_Key_Col, Mode);

  if (Mode != MODE_READ) {
    sprintf(g->Message, MSG(BAD_QUERY_OPEN), Mode);
    return TRUE;
    } // endif Mode

  CurPos = -1;

  if (Use == USE_OPEN)
    return FALSE;

  /*********************************************************************/
  /*  Open (retrieve data from) the query if not already open.         */
  /*********************************************************************/
  Use = USE_OPEN;       // Do it now in case we are recursively called

  return FALSE;
  } // end of OpenDB

/***********************************************************************/
/*  GetRecpos: returns current position of next sequential read.       */
/***********************************************************************/
int TDBQRS::GetRecpos(void)
  {
  return (CurPos);
  } // end of GetRecpos

/***********************************************************************/
/*  ReadDB: Data Base read routine for QRS access method.              */
/***********************************************************************/
int TDBQRS::ReadDB(PGLOBAL g)
  {
  int rc = RC_OK;

  if (trace)
    htrc("QRS ReadDB: R%d CurPos=%d key=%p link=%p Kindex=%p\n",
              GetTdb_No(), CurPos, To_Key_Col, To_Link, To_Kindex);

#if 0
  if (To_Kindex) {
    /*******************************************************************/
    /*  Reading is by an index table.                                  */
    /*******************************************************************/
    int recpos = To_Kindex->Fetch(g);

    switch (recpos) {
      case -1:           // End of file reached
        rc = RC_EF;
        break;
      case -2:           // No match for join
        rc = RC_NF;
        break;
      case -3:           // Same record as last non null one
        rc = RC_OK;
        break;
      default:
        /***************************************************************/
        /*  Set the file position according to record to read.         */
        /***************************************************************/
        CurPos = recpos;
      } // endswitch recpos

    if (trace)
      htrc("Position is now %d\n", CurPos);

  } else
#endif // 0
    /*******************************************************************/
    /*  !To_Kindex ---> sequential reading                             */
    /*******************************************************************/
    rc = (++CurPos < Qrp->Nblin) ? RC_OK : RC_EF;

  return rc;
  } // end of ReadDB

/***********************************************************************/
/*  Dummy WriteDB: just send back an error return.                     */
/***********************************************************************/
int TDBQRS::WriteDB(PGLOBAL g)
  {
  strcpy(g->Message, MSG(QRY_READ_ONLY));
  return RC_FX;
  } // end of WriteDB

/***********************************************************************/
/*  Dummy DeleteDB routine, just returns an error code.                */
/***********************************************************************/
int TDBQRS::DeleteDB(PGLOBAL g, int irc)
  {
  strcpy(g->Message, MSG(NO_QRY_DELETE));
  return RC_FX;
  } // end of DeleteDB

/***********************************************************************/
/*  Data Base close routine for QRS access method.                     */
/***********************************************************************/
void TDBQRS::CloseDB(PGLOBAL g)
  {
//if (To_Kindex) {
//  To_Kindex->Close();
//    To_Kindex = NULL;
//    } // endif

  if (trace)
    htrc("Qryres CloseDB");

//Qryp->Sqlp->CloseDB();
  } // end of CloseDB

// ------------------------ QRSCOL functions ----------------------------

/***********************************************************************/
/*  QRSCOL public constructor.                                         */
/***********************************************************************/
QRSCOL::QRSCOL(PGLOBAL g, PCOLRES crp, PTDB tdbp, PCOL cprec, int i)
      : COLBLK(NULL, tdbp, i) 
  {
  if (cprec) {
    Next = cprec->GetNext();
    cprec->SetNext(this);
  } else {
    Next = tdbp->GetColumns();
    tdbp->SetColumns(this);
  } // endif cprec

  // Set additional QRS access method information for column.
  Crp = crp;
  Name = Crp->Name;
  Long = Crp->Clen;
  Buf_Type = crp->Type;
  strcpy(Format.Type, GetFormatType(Buf_Type));
  Format.Length = (SHORT)Long;
  Format.Prec = (SHORT)Crp->Prec;

  if (trace) {
    htrc("Making new QRSCOL C%d %s at %p\n", Index, Name, this);
    htrc(" BufType=%d Long=%d length=%d clen=%d\n",
          Buf_Type, Long, Format.Length, Crp->Clen);
    } // endif trace

  } // end of QRSCOL constructor

/***********************************************************************/
/*  QRSCOL constructor used for copying columns.                       */
/*  tdbp is the pointer to the new table descriptor.                   */
/***********************************************************************/
QRSCOL::QRSCOL(QRSCOL *col1, PTDB tdbp) : COLBLK(col1, tdbp)
  {
  Crp = col1->Crp;
  } // end of QRSCOL copy constructor

/***********************************************************************/
/*  ReadColumn: what this routine does is to extract the RESCOL block  */
/*  current value and convert it to the column buffer type.            */
/***********************************************************************/
void QRSCOL::ReadColumn(PGLOBAL g)
  {
  PTDBQRS tdbp = (PTDBQRS)To_Tdb;

  if (trace)
    htrc("QRS RC: col %s R%d type=%d CurPos=%d Len=%d\n",
          Name, tdbp->GetTdb_No(), Buf_Type, tdbp->CurPos, Crp->Clen);

  if (Crp->Kdata)
    Value->SetValue_pvblk(Crp->Kdata, tdbp->CurPos);
  else
    Value->Reset();

  } // end of ReadColumn

/***********************************************************************/
/*  Make file output of a Dos column descriptor block.                 */
/***********************************************************************/
void QRSCOL::Print(PGLOBAL g, FILE *f, UINT n)
  {
  COLBLK::Print(g, f, n);

  fprintf(f, " Crp=%p\n", Crp);
  } // end of Print

/* --------------------- End of TabPivot/TabQrs ---------------------- */
