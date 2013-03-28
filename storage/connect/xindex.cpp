/***************** Xindex C++ Class Xindex Code (.CPP) *****************/
/*  Name: XINDEX.CPP  Version 2.7                                      */
/*                                                                     */
/*  (C) Copyright to the author Olivier BERTRAND          2004-2012    */
/*                                                                     */
/*  This file contains the class XINDEX implementation code.           */
/***********************************************************************/

/***********************************************************************/
/*  Include relevant sections of the System header files.              */
/***********************************************************************/
#include "my_global.h"
#if defined(WIN32)
#include <io.h>
#include <fcntl.h>
#include <errno.h>
//#include <windows.h>
#else   // !WIN32
#if defined(UNIX)
#define _LARGEFILE64_SOURCE
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <unistd.h>
#else   // !UNIX
#include <io.h>
#endif  // !UNIX
#include <fcntl.h>
#endif  // !WIN32

/***********************************************************************/
/*  Include required application header files                          */
/*  global.h    is header containing all global Plug declarations.     */
/*  plgdbsem.h  is header containing the DB applic. declarations.      */
/*  kindex.h    is header containing the KINDEX class definition.      */
/***********************************************************************/
#include "global.h"
#include "plgdbsem.h"
#include "osutil.h"
#include "maputil.h"
//nclude "filter.h"
#include "tabcol.h"
#include "xindex.h"
#include "xobject.h"
//nclude "scalfnc.h"
//nclude "array.h"
#include "filamtxt.h"
#include "tabdos.h"

/***********************************************************************/
/*  Macro or external routine definition                               */
/***********************************************************************/
#define NZ 7
#define NW 5
#define MAX_INDX 10
#ifndef INVALID_SET_FILE_POINTER
#define INVALID_SET_FILE_POINTER  0xFFFFFFFF
#endif

/***********************************************************************/
/*  DB static external variables.                                      */
/***********************************************************************/
extern MBLOCK Nmblk;                /* Used to initialize MBLOCK's     */

/***********************************************************************/
/*  Last two parameters are true to enable type checking, and last one */
/*  to have rows filled by blanks to be compatible with QRY blocks.    */
/***********************************************************************/
PVBLK AllocValBlock(PGLOBAL, void *, int, int, int, int,
                    bool check = true, bool blank = true);

/***********************************************************************/
/*  Check whether we have to create/update permanent indexes.          */
/***********************************************************************/
int PlgMakeIndex(PGLOBAL g, PSZ name, PIXDEF pxdf, bool add)
  {
  int     rc;
  PTABLE  tablep;
  PTDBDOS tdbp;
  PCATLG  cat = PlgGetCatalog(g, true);

  /*********************************************************************/
  /*  Open a new table in mode read and with only the keys columns.    */
  /*********************************************************************/
  tablep = new(g) XTAB(name);

  if (!(tdbp = (PTDBDOS)cat->GetTable(g, tablep)))
    rc = RC_NF;
  else if (!tdbp->GetDef()->Indexable()) {
    sprintf(g->Message, MSG(TABLE_NO_INDEX), name);
    rc = RC_NF;
  } else if ((rc = tdbp->MakeIndex(g, pxdf, add)) == RC_INFO)
    rc = RC_OK;            // No index

  return rc;
  } // end of PlgMakeIndex

/* -------------------------- Class INDEXDEF ------------------------- */

/***********************************************************************/
/*  INDEXDEF Constructor.                                              */
/***********************************************************************/
INDEXDEF::INDEXDEF(char *name, bool uniq, int n)
  {
//To_Def = NULL;
  Next = NULL;
  ToKeyParts = NULL;
  Name = name;
  Unique = uniq;
  Invalid = false;
  AutoInc = false;
  Nparts = 0;
  ID = n;
//Offset = 0;
//Offhigh = 0;
  Size = 0;
  MaxSame = 1;
  } // end of INDEXDEF constructor

/***********************************************************************/
/*  Set the max same values for each colum after making the index.     */
/***********************************************************************/
void INDEXDEF::SetMxsame(PXINDEX x)
  {
  PKPDEF  kdp;
  PXCOL   xcp;

  for (kdp = ToKeyParts, xcp = x->To_KeyCol;
       kdp && xcp; kdp = kdp->Next, xcp = xcp->Next)
    kdp->Mxsame = xcp->Mxs;
  } // end of SetMxsame

/* -------------------------- Class KPARTDEF ------------------------- */

/***********************************************************************/
/*  KPARTDEF Constructor.                                              */
/***********************************************************************/
KPARTDEF::KPARTDEF(PSZ name, int n)
  {
  Next = NULL;
  Name = name;
  Mxsame = 0;
  Ncol = n;
  Klen = 0;
  } // end of KPARTDEF constructor

/* -------------------------- XXBASE Class --------------------------- */

/***********************************************************************/
/*  XXBASE public constructor.                                         */
/***********************************************************************/
XXBASE::XXBASE(PTDBDOS tbxp, bool b) : CSORT(b),
        To_Rec((int*&)Record.Memp)
  {
  Tbxp = tbxp;
  Record = Nmblk;
  Cur_K = -1;
  Old_K = -1;
  Num_K = 0;
  Ndif = 0;
  Bot = Top = Inf = Sup = 0;
  Op = OP_EQ;
  To_KeyCol = NULL;
  Mul = false;
  Val_K = -1;
  Nblk = Sblk = 0;
  Thresh = 7;
  ID = -1;
  Nth = 0;
  } // end of XXBASE constructor

/***********************************************************************/
/*  Make file output of XINDEX contents.                               */
/***********************************************************************/
void XXBASE::Print(PGLOBAL g, FILE *f, uint n)
  {
  char m[64];

  memset(m, ' ', n);                    // Make margin string
  m[n] = '\0';
  fprintf(f, "%sXINDEX: Tbxp=%p Num=%d\n", m, Tbxp, Num_K);
  } // end of Print

/***********************************************************************/
/*  Make string output of XINDEX contents.                             */
/***********************************************************************/
void XXBASE::Print(PGLOBAL g, char *ps, uint z)
  {
  *ps = '\0';
  strncat(ps, "Xindex", z);
  } // end of Print

/* -------------------------- XINDEX Class --------------------------- */

/***********************************************************************/
/*  XINDEX public constructor.                                         */
/***********************************************************************/
XINDEX::XINDEX(PTDBDOS tdbp, PIXDEF xdp, PXLOAD pxp, PCOL *cp, PXOB *xp, int k)
      : XXBASE(tdbp, !xdp->IsUnique())
  {
  Xdp = xdp;
  ID = xdp->GetID();
  Tdbp = tdbp;
  X = pxp;
  To_LastCol = NULL;
  To_LastVal = NULL;
  To_Cols = cp;
  To_Vals = xp;
  Mul = !xdp->IsUnique();
  Srtd = false;
  Nk = xdp->GetNparts();
  Nval = (k) ? k : Nk;
  Incr = 0;
//Defoff = xdp->GetOffset();
//Defhigh = xdp->GetOffhigh();
  Size = xdp->GetSize();
  MaxSame = xdp->GetMaxSame();
  } // end of XINDEX constructor

/***********************************************************************/
/*  XINDEX Reset: re-initialize a Xindex block.                        */
/***********************************************************************/
void XINDEX::Reset(void)
  {
  for (PXCOL kp = To_KeyCol; kp; kp = kp->Next)
    kp->Val_K = kp->Ndf;

  Cur_K = Num_K;
  Old_K = -1;  // Needed to avoid not setting CurBlk for Update
  Op = (Op == OP_FIRST  || Op == OP_NEXT)   ? OP_FIRST  :
       (Op == OP_FSTDIF || Op == OP_NXTDIF) ? OP_FSTDIF : OP_EQ;
  Nth = 0;
  } // end of Reset

/***********************************************************************/
/*  XINDEX Close: terminate index and free all allocated data.         */
/*  Do not reset other values that are used at return to make.         */
/***********************************************************************/
void XINDEX::Close(void)
  {
  // Close file or view of file
  X->Close();

  // De-allocate data
  PlgDBfree(Record);
  PlgDBfree(Index);
  PlgDBfree(Offset);

  // De-allocate Key data
  for (PXCOL kcp = To_KeyCol; kcp; kcp = kcp->Next)
    kcp->FreeData();

  // Column values cannot be retrieved from key anymore
  for (int k = 0; k < Nk; k++)
    To_Cols[k]->SetKcol(NULL);

  } // end of Close

/***********************************************************************/
/*  XINDEX compare routine for C Quick/Insertion sort.                 */
/***********************************************************************/
int XINDEX::Qcompare(int *i1, int *i2)
  {
  register int  k;
  register PXCOL kcp;

  for (kcp = To_KeyCol, k = 0; kcp; kcp = kcp->Next)
    if ((k = kcp->Compare(*i1, *i2)))
      break;

#ifdef DEBTRACE
  num_comp++;
#endif

  return k;
  } // end of Qcompare

/***********************************************************************/
/*  Make: Make and index on key column(s).                             */
/***********************************************************************/
bool XINDEX::Make(PGLOBAL g, PIXDEF sxp)
  {
  /*********************************************************************/
  /*  Table can be accessed through an index.                          */
  /*********************************************************************/
  int     k, rc = RC_OK;
  int   *bof, i, j, n, ndf, nkey;
  PKPDEF  kdfp = Xdp->GetToKeyParts();
  bool    brc = true;
  PCOL    colp;
  PXCOL   kp, prev = NULL, kcp = NULL;
  PDBUSER dup = (PDBUSER)g->Activityp->Aptr;

  Defoff = 0;
  Size = 0;                     // Void index

  /*********************************************************************/
  /*  Allocate the storage that will contain the keys and the file     */
  /*  positions corresponding to them.                                 */
  /*********************************************************************/
  if ((n = Tdbp->GetMaxSize(g)) < 0)
    return true;
  else if (!n) {
    Num_K = Ndif = 0;
    MaxSame = 1;

    // The if condition was suppressed because this may be an existing
    // index that is now void because all table lines were deleted.
//  if (sxp)
      goto nox;            // Truncate eventually existing index file
//  else
//    return false;

    } // endif n

  // File position must be stored
  Record.Size = n * sizeof(int);

  if (!PlgDBalloc(g, NULL, Record)) {
    sprintf(g->Message, MSG(MEM_ALLOC_ERR), "index", n);
    goto err;    // Error
    } // endif

  /*********************************************************************/
  /*  Allocate the KXYCOL blocks used to store column values.          */
  /*********************************************************************/
  for (k = 0; k < Nk; k++) {
    colp = To_Cols[k];

    if (!kdfp) {
      sprintf(g->Message, MSG(INT_COL_ERROR),
                          (colp) ? colp->GetName() : "???");
      goto err;    // Error
      } // endif kdfp

    kcp = new(g) KXYCOL(this);

    if (kcp->Init(g, colp, n, true, kdfp->Klen))
      goto err;    // Error

    if (prev) {
      kcp->Previous = prev;
      prev->Next = kcp;
    } else
      To_KeyCol = kcp;

    prev = kcp;
    kdfp = kdfp->Next;
    } // endfor k

  To_LastCol = prev;

  /*********************************************************************/
  /*  Get the starting information for progress.                       */
  /*********************************************************************/
  dup->Step = (char*)PlugSubAlloc(g, NULL, 128);
  sprintf((char*)dup->Step, MSG(BUILD_INDEX), Xdp->GetName(), Tdbp->Name);
  dup->ProgMax = Tdbp->GetProgMax(g);
  dup->ProgCur = 0;

  /*********************************************************************/
  /*  Standard init: read the file and construct the index table.      */
  /*  Note: reading will be sequential as To_Kindex is not set.        */
  /*********************************************************************/
  for (i = nkey = 0; i < n && rc != RC_EF; i++) {
#if defined(THREAD)
    if (!dup->Step) {
      strcpy(g->Message, MSG(QUERY_CANCELLED));
      longjmp(g->jumper[g->jump_level], 99);
      } // endif Step
#endif   // THREAD

    /*******************************************************************/
    /*  Read a valid record from table file.                           */
    /*******************************************************************/
    rc = Tdbp->ReadDB(g);

    // Update progress information
    dup->ProgCur = Tdbp->GetProgCur();

    // Check return code and do whatever must be done according to it
    switch (rc) {
      case RC_OK:
        break;
      case RC_EF:
        goto end_of_file;
      case RC_NF:
        continue;
      default:
        sprintf(g->Message, MSG(RC_READING), rc, Tdbp->Name);
        goto err;
      } // endswitch rc

    /*******************************************************************/
    /*  Get and Store the file position of the last read record for    */
    /*  future direct access.                                          */
    /*******************************************************************/
    To_Rec[nkey] = Tdbp->GetRecpos();

    /*******************************************************************/
    /*  Get the keys and place them in the key blocks.                 */
    /*******************************************************************/
    for (k = 0, kcp = To_KeyCol;
         k < Nk && kcp;
         k++, kcp = kcp->Next) {
      colp = To_Cols[k];
      colp->Reset();

      colp->ReadColumn(g);
//    if (colp->ReadColumn(g))
//      goto err;

      kcp->SetValue(colp, nkey);
      } // endfor k

    nkey++;                    // A new valid key was found
    } // endfor i

 end_of_file:

  // Update progress information
  dup->ProgCur = Tdbp->GetProgMax(g);

  /*********************************************************************/
  /* Record the Index size and eventually resize memory allocation.    */
  /*********************************************************************/
  if ((Num_K = nkey) < n) {
    PlgDBrealloc(g, NULL, Record, Num_K * sizeof(int));

    for (kcp = To_KeyCol; kcp; kcp = kcp->Next)
      kcp->ReAlloc(g, Num_K);

    } // endif Num_K

  /*********************************************************************/
  /*  Sort the index so we can use an optimized Find algorithm.        */
  /*  Note: for a unique index we use the non conservative sort        */
  /*  version because normally all index values are different.         */
  /*  This was set at CSORT class construction.                        */
  /*  For all indexes, an offset array is made so we can check the     */
  /*  uniqueness of unique indexes.                                    */
  /*********************************************************************/
  Index.Size = Num_K * sizeof(int);

  if (!PlgDBalloc(g, NULL, Index)) {
    sprintf(g->Message, MSG(MEM_ALLOC_ERR), "index", Num_K);
    goto err;    // Error
    } // endif alloc

  Offset.Size = (Num_K + 1) * sizeof(int);

  if (!PlgDBalloc(g, NULL, Offset)) {
    sprintf(g->Message, MSG(MEM_ALLOC_ERR), "offset", Num_K + 1);
    goto err;    // Error
    } // endif alloc

  // Call the sort program, it returns the number of distinct values
  if ((Ndif = Qsort(g, Num_K)) < 0)
    goto err;       // Error during sort

  // Check whether the unique index is unique indeed
  if (!Mul)
    if (Ndif < Num_K) {
      strcpy(g->Message, MSG(INDEX_NOT_UNIQ));
      goto err;
    } else
      PlgDBfree(Offset);           // Not used anymore

  // Use the index to physically reorder the xindex
  Srtd = Reorder(g);

  if (Ndif < Num_K) {
    // Resize the offset array
    PlgDBrealloc(g, NULL, Offset, (Ndif + 1) * sizeof(int));

    // Initial value of MaxSame
    MaxSame = Pof[1] - Pof[0];

    // Resize the Key array by only keeping the distinct values
    for (i = 1; i < Ndif; i++) {
      for (kcp = To_KeyCol; kcp; kcp = kcp->Next)
        kcp->Move(i, Pof[i]);

      MaxSame = max(MaxSame, Pof[i + 1] - Pof[i]);
      } // endfor i

    for (kcp = To_KeyCol; kcp; kcp = kcp->Next)
      kcp->ReAlloc(g, Ndif);

  } else {
    Mul = false;                   // Current index is unique
    PlgDBfree(Offset);             // Not used anymore
    MaxSame = 1;                  // Reset it when remaking an index
  } // endif Ndif

  /*********************************************************************/
  /*  Now do the reduction of the index. Indeed a multi-column index   */
  /*  can be used for only some of the first columns. For instance if  */
  /*  an index is defined for column A, B, C PlugDB can use it for     */
  /*  only the column A or the columns A, B.                           */
  /*  What we do here is to reduce the data so column A will contain   */
  /*  only the sorted distinct values of A, B will contain data such   */
  /*  as only distinct values of A,B are stored etc.                   */
  /*  This implies that for each column set an offset array is made    */
  /*  except if the subset originally contains unique values.          */
  /*********************************************************************/
  // Update progress information
  dup->Step = STEP(REDUCE_INDEX);

  ndf = Ndif;
  To_LastCol->Mxs = MaxSame;

  for (kcp = To_LastCol->Previous; kcp; kcp = kcp->Previous) {
    if (!(bof = kcp->MakeOffset(g, ndf)))
      goto err;
    else
      *bof = 0;

    for (n = 0, i = j = 1; i < ndf; i++)
      for (kp = kcp; kp; kp = kp->Previous)
        if (kp->Compare(n, i)) {
          // Values are not equal to last ones
          bof[j++] = n = i;
          break;
          } // endif Compare

    if (j < ndf) {
      // Sub-index is multiple
      bof[j] = ndf;
      ndf = j;                  // New number of distinct values

      // Resize the Key array by only keeping the distinct values
      for (kp = kcp; kp; kp = kp->Previous) {
        for (i = 1; i < ndf; i++)
          kp->Move(i, bof[i]);

        kp->ReAlloc(g, ndf);
        } // endif kcp

      // Resize the offset array
      kcp->MakeOffset(g, ndf);

      // Calculate the max same value for this column
      kcp->Mxs = ColMaxSame(kcp);
    } else {
      // Current sub-index is unique
      kcp->MakeOffset(g, 0);   // The offset is not used anymore
      kcp->Mxs = 1;            // Unique
    } // endif j

    } // endfor kcp

  /*********************************************************************/
  /*  For sorted columns and fixed record size, file position can be   */
  /*  calculated, so the Record array can be discarted.                */
  /*  Note: for Num_K = 1 any non null value is Ok.                    */
  /*********************************************************************/
  if (Srtd && Tdbp->Ftype != RECFM_VAR) {
    Incr = (Num_K > 1) ? To_Rec[1] : Num_K;
    PlgDBfree(Record);
    } // endif Srtd

  /*********************************************************************/
  /*  Check whether a two-tier find algorithm can be implemented.      */
  /*  It is currently implemented only for single key indexes.         */
  /*********************************************************************/
  if (Nk == 1 && ndf >= 65536) {
    // Implement a two-tier find algorithm
    for (Sblk = 256; (Sblk * Sblk * 4) < ndf; Sblk *= 2) ;

    Nblk = (ndf -1) / Sblk + 1;

    if (To_KeyCol->MakeBlockArray(g, Nblk, Sblk))
      goto err;    // Error

    } // endif Num_K

 nox:
  /*********************************************************************/
  /*  No valid record read yet for secondary file.                     */
  /*********************************************************************/
  Cur_K = Num_K;

  /*********************************************************************/
  /*  Save the index so it has not to be recalculated.                 */
  /*********************************************************************/
  if (!SaveIndex(g, sxp))
    brc = false;

 err:
  // We don't need the index anymore
  Close();

  if (brc)
    printf("%s\n", g->Message);

  return brc;
  } // end of Make

/***********************************************************************/
/*  Return the max size of the intermediate column.                    */
/***********************************************************************/
int XINDEX::ColMaxSame(PXCOL kp)
  {
  int *kof, i, ck1, ck2, ckn = 1;
  PXCOL kcp;

  // Calculate the max same value for this column
  for (i = 0; i < kp->Ndf; i++) {
    ck1 = i;
    ck2 = i + 1;

    for (kcp = kp; kcp; kcp = kcp->Next) {
      if (!(kof = (kcp->Next) ? kcp->Kof : Pof))
        break;

      ck1 = kof[ck1];
      ck2 = kof[ck2];
      } // endfor kcp

    ckn = max(ckn, ck2 - ck1);
    } // endfor i

  return ckn;
  } // end of ColMaxSame

/***********************************************************************/
/*  Reorder: use the sort index to reorder the data in storage so      */
/*  it will be physically sorted and sort index can be removed.        */
/***********************************************************************/
bool XINDEX::Reorder(PGLOBAL g)
  {
  register int i, j, k, n;
  bool          sorted = true;
  PXCOL         kcp;
  PDBUSER       dup = (PDBUSER)g->Activityp->Aptr;

  if (Num_K > 500000) {
    // Update progress information
    dup->Step = STEP(REORDER_INDEX);
    dup->ProgMax = Num_K;
    dup->ProgCur = 0;
  } else
    dup = NULL;

  if (!Pex)
    return Srtd;

  for (i = 0; i < Num_K; i++) {
    if (Pex[i] == Num_K) {        // Already moved
      continue;
    } else if (Pex[i] == i) {     // Already placed
      if (dup)
        dup->ProgCur++;

      continue;
    } // endif's Pex

    sorted = false;

    for (kcp = To_KeyCol; kcp; kcp = kcp->Next)
      kcp->Save(i);

    n = To_Rec[i];

    for (j = i;; j = k) {
      k = Pex[j];
      Pex[j] = Num_K;           // Mark position as set

      if (k == i) {
        for (kcp = To_KeyCol; kcp; kcp = kcp->Next)
          kcp->Restore(j);

        To_Rec[j] = n;
        break;                  // end of loop
      } else {
        for (kcp = To_KeyCol; kcp; kcp = kcp->Next)
          kcp->Move(j, k);      // Move k to j

        To_Rec[j] = To_Rec[k];
      } // endif k

      if (dup)
        dup->ProgCur++;

      } // endfor j

    } // endfor i

  // The index is not used anymore
  PlgDBfree(Index);
  return sorted;
  } // end of Reorder

/***********************************************************************/
/*  Save the index values for this table.                              */
/*  The problem here is to avoid name duplication, because more than   */
/*  one data file can have the same name (but different types) and/or  */
/*  the same data file can be used with different block sizes. This is */
/*  why we use Ofn that defaults to the file name but can be set to a  */
/*  different name if necessary.                                       */
/***********************************************************************/
bool XINDEX::SaveIndex(PGLOBAL g, PIXDEF sxp)
  {
  char   *ftype;
  char    fn[_MAX_PATH];
  int     n[NZ], nof = (Mul) ? (Ndif + 1) : 0;
  int     id = -1;
  bool    sep, rc = false;
  PXCOL   kcp = To_KeyCol;
  PDOSDEF defp = (PDOSDEF)Tdbp->To_Def;
  PDBUSER dup = PlgGetUser(g);

  dup->Step = STEP(SAVING_INDEX);
  dup->ProgMax = 15 + 16 * Nk;
  dup->ProgCur = 0;

  switch (Tdbp->Ftype) {
    case RECFM_VAR: ftype = ".dnx"; break;
    case RECFM_FIX: ftype = ".fnx"; break;
    case RECFM_BIN: ftype = ".bnx"; break;
    case RECFM_VCT: ftype = ".vnx"; break;
    case RECFM_DBF: ftype = ".dbx"; break;
    default:
      sprintf(g->Message, MSG(INVALID_FTYPE), Tdbp->Ftype);
      return true;
    } // endswitch Ftype

  if ((sep = dup->Catalog->GetBoolCatInfo("SepIndex", false))) {
    // Index is saved in a separate file
#if !defined(UNIX)
    char drive[_MAX_DRIVE];
#else
    char *drive = NULL;
#endif
    char direc[_MAX_DIR];
    char fname[_MAX_FNAME];

    _splitpath(defp->GetOfn(), drive, direc, fname, NULL);
    strcat(strcat(fname, "_"), Xdp->GetName());
    _makepath(fn, drive, direc, fname, ftype);
    sxp = NULL;
  } else {
    id = ID;
    strcat(PlugRemoveType(fn, strcpy(fn, defp->GetOfn())), ftype);
  } // endif sep

  PlugSetPath(fn, fn, Tdbp->GetPath());

  if (X->Open(g, fn, id, (sxp) ? MODE_INSERT : MODE_WRITE)) {
    printf("%s\n", g->Message);
    return true;
    } // endif Open

  if (!Ndif)
    goto end;                // Void index

  // Defoff is the start of the definition in the index file
//X->GetOff(Defoff, Defhigh, sxp);

#if defined(TRACE)
  printf("Defoff=%d Defhigh=%d\n", Defoff, Defhigh);
#endif   // TRACE

  /*********************************************************************/
  /*  Write the index values on the index file.                        */
  /*********************************************************************/
  n[0] = ID;                  // To check validity
  n[1] = Nk;                  // The number of indexed columns
  n[2] = nof;                 // The offset array size or 0
  n[3] = Num_K;               // The index size
  n[4] = Incr;                // Increment of record positions
  n[5] = Nblk; n[6] = Sblk;

#if defined(TRACE)
  printf("Saving index %s\n", Xdp->GetName());
  printf("ID=%d Nk=%d nof=%d Num_K=%d Incr=%d Nblk=%d Sblk=%d\n",
    ID, Nk, nof, Num_K, Incr, Nblk, Sblk);
#endif   // TRACE

  Size = X->Write(g, n, NZ, sizeof(int), rc);
  dup->ProgCur = 1;

  if (Mul)             // Write the offset array
    Size += X->Write(g, Pof, nof, sizeof(int), rc);

  dup->ProgCur = 5;

  if (!Incr)           // Write the record position array(s)
    Size += X->Write(g, To_Rec, Num_K, sizeof(int), rc);

  dup->ProgCur = 15;

  for (; kcp; kcp = kcp->Next) {
    n[0] = kcp->Ndf;                 // Number of distinct sub-values
    n[1] = (kcp->Kof) ? kcp->Ndf + 1 : 0;     // 0 if unique
    n[2] = (kcp == To_KeyCol) ? Nblk : 0;
    n[3] = kcp->Klen;                // To be checked later
    n[4] = kcp->Type;                // To be checked later

    Size += X->Write(g, n, NW, sizeof(int), rc);
    dup->ProgCur += 1;

    if (n[2])
      Size += X->Write(g, kcp->To_Bkeys, Nblk, kcp->Klen, rc);

    dup->ProgCur += 5;

    Size += X->Write(g, kcp->To_Keys, n[0], kcp->Klen, rc);
    dup->ProgCur += 5;

    if (n[1])
      Size += X->Write(g, kcp->Kof, n[1], sizeof(int), rc);

    dup->ProgCur += 5;
    } // endfor kcp

#if defined(TRACE)
  printf("Index %s saved, Size=%d\n", Xdp->GetName(), Size);
#endif   // TRACE

 end:
  X->Close(fn, id);
  return rc;
  } // end of SaveIndex

#if !defined(XMAP)
/***********************************************************************/
/*  Init: Open and Initialize a Key Index.                             */
/***********************************************************************/
bool XINDEX::Init(PGLOBAL g)
  {
  /*********************************************************************/
  /*  Table will be accessed through an index table.                   */
  /*  If sorting is required, this will be done later.                 */
  /*********************************************************************/
  char   *ftype;
  char    fn[_MAX_PATH];
  int     k, n, nv[NZ], id = -1;
  bool    estim = false;
  PCOL    colp;
  PXCOL   prev = NULL, kcp = NULL;
  PDOSDEF defp = (PDOSDEF)Tdbp->To_Def;

  /*********************************************************************/
  /*  Get the estimated table size.                                    */
  /*  Note: for fixed tables we must use cardinality to avoid the call */
  /*  to MaxBlkSize that could reduce the cardinality value.           */
  /*********************************************************************/
  if (Tdbp->Cardinality(NULL)) {
    // For DBF tables, Cardinality includes bad or soft deleted lines
    // that are not included in the index, and can be larger then the
    // index size.
    estim = (Tdbp->Ftype == RECFM_DBF);
    n = Tdbp->Cardinality(g);      // n is exact table size
  } else {
    // Variable table not optimized
    estim = true;                  // n is an estimate of the size
    n = Tdbp->GetMaxSize(g);
  } // endif Cardinality

  if (n <= 0)
    return !(n == 0);             // n < 0 error, n = 0 void table

  /*********************************************************************/
  /*  Get the first key column.                                        */
  /*********************************************************************/
  if (!Nk || !To_Cols || (!To_Vals && Op != OP_FIRST && Op != OP_FSTDIF)) {
    strcpy(g->Message, MSG(NO_KEY_COL));
    return true;    // Error
  } else
    colp = To_Cols[0];

  switch (Tdbp->Ftype) {
    case RECFM_VAR: ftype = ".dnx"; break;
    case RECFM_FIX: ftype = ".fnx"; break;
    case RECFM_BIN: ftype = ".bnx"; break;
    case RECFM_VCT: ftype = ".vnx"; break;
    case RECFM_DBF: ftype = ".dbx"; break;
    default:
      sprintf(g->Message, MSG(INVALID_FTYPE), Tdbp->Ftype);
      return true;
    } // endswitch Ftype

  if (defp->SepIndex()) {
    // Index was saved in a separate file
#if !defined(UNIX)
    char drive[_MAX_DRIVE];
#else
    char *drive = NULL;
#endif
    char direc[_MAX_DIR];
    char fname[_MAX_FNAME];

    _splitpath(defp->GetOfn(), drive, direc, fname, NULL);
    strcat(strcat(fname, "_"), Xdp->GetName());
    _makepath(fn, drive, direc, fname, ftype);
  } else {
    id = ID;
    strcat(PlugRemoveType(fn, strcpy(fn, defp->GetOfn())), ftype);
  } // endif sep

  PlugSetPath(fn, fn, Tdbp->GetPath());

#if defined(TRACE)
  printf("Index %s file: %s\n", Xdp->GetName(), fn);
#endif   // TRACE

  /*********************************************************************/
  /*  Open the index file and check its validity.                      */
  /*********************************************************************/
  if (X->Open(g, fn, id, MODE_READ))
    goto err;               // No saved values

  //  Now start the reading process.
  if (X->Read(g, nv, NZ, sizeof(int)))
    goto err;

#if defined(TRACE)
  printf("nv=%d %d %d %d %d %d %d\n",
    nv[0], nv[1], nv[2], nv[3], nv[4], nv[5], nv[6]);
#endif   // TRACE

  // The test on ID was suppressed because MariaDB can change an index ID
  // when other indexes are added or deleted 
  if (/*nv[0] != ID ||*/ nv[1] != Nk) {
    sprintf(g->Message, MSG(BAD_INDEX_FILE), fn);
#if defined(TRACE)
    printf("nv[0]=%d ID=%d nv[1]=%d Nk=%d\n", nv[0], ID, nv[1], Nk);
#endif   // TRACE
    goto err;
    } // endif

  if (nv[2]) {
    Mul = true;
    Ndif = nv[2];

    // Allocate the storage that will contain the offset array
    Offset.Size = Ndif * sizeof(int);

    if (!PlgDBalloc(g, NULL, Offset)) {
      sprintf(g->Message, MSG(MEM_ALLOC_ERR), "offset", Ndif);
      goto err;
      } // endif

    if (X->Read(g, Pof, Ndif, sizeof(int)))
      goto err;

    Ndif--;   // nv[2] is offset size, equal to Ndif + 1
  } else {
    Mul = false;
    Ndif = nv[3];
  } // endif nv[2]

  if (nv[3] < n && estim)
    n = nv[3];              // n was just an evaluated max value

  if (nv[3] != n) {
    sprintf(g->Message, MSG(OPT_NOT_MATCH), fn);
    goto err;
    } // endif

  Num_K = nv[3];
  Incr = nv[4];
  Nblk = nv[5];
  Sblk = nv[6];

  if (!Incr) {
    /*******************************************************************/
    /*  Allocate the storage that will contain the file positions.     */
    /*******************************************************************/
    Record.Size = Num_K * sizeof(int);

    if (!PlgDBalloc(g, NULL, Record)) {
      sprintf(g->Message, MSG(MEM_ALLOC_ERR), "index", Num_K);
      goto err;
      } // endif

    if (X->Read(g, To_Rec, Num_K, sizeof(int)))
      goto err;

  } else
    Srtd = true;    // Sorted positions can be calculated

  /*********************************************************************/
  /*  Allocate the KXYCOL blocks used to store column values.          */
  /*********************************************************************/
  for (k = 0; k < Nk; k++) {
    if (k == Nval)
      To_LastVal = prev;

    if (X->Read(g, nv, NW, sizeof(int)))
      goto err;

    colp = To_Cols[k];

    if (nv[4] != colp->GetResultType() || !colp->GetValue() ||
       (nv[3] != colp->GetValue()->GetClen() && nv[4] != TYPE_STRING)) {
      sprintf(g->Message, MSG(XCOL_MISMATCH), colp->GetName());
      goto err;    // Error
      } // endif GetKey

    kcp = new(g) KXYCOL(this);

    if (kcp->Init(g, colp, nv[0], true, (int)nv[3]))
      goto err;    // Error

    /*******************************************************************/
    /*  Read the index values from the index file.                     */
    /*******************************************************************/
    if (k == 0 && Nblk) {
      if (kcp->MakeBlockArray(g, Nblk, 0))
        goto err;

      // Read block values
      if (X->Read(g, kcp->To_Bkeys, Nblk, kcp->Klen))
        goto err;

      } // endif Nblk

    // Read the entire (small) index
    if (X->Read(g, kcp->To_Keys, nv[0], kcp->Klen))
      goto err;

    if (nv[1]) {
      if (!kcp->MakeOffset(g, nv[1] - 1))
        goto err;

      // Read the offset array
      if (X->Read(g, kcp->Kof, nv[1], sizeof(int)))
        goto err;

      } // endif n[1]

    if (!kcp->Prefix)
      // Indicate that the key column value can be found from KXYCOL
      colp->SetKcol(kcp);

    if (prev) {
      kcp->Previous = prev;
      prev->Next = kcp;
    } else
      To_KeyCol = kcp;

    prev = kcp;
    } // endfor k

  To_LastCol = prev;

  if (Mul && prev) {
    // Last key offset is the index offset
    kcp->Koff = Offset;
    kcp->Koff.Sub = true;
    } // endif Mul

  X->Close();

  /*********************************************************************/
  /*  No valid record read yet for secondary file.                     */
  /*********************************************************************/
  Cur_K = Num_K;
  return false;

err:
  Close();
  return true;
  } // end of Init

#else    // XMAP
/***********************************************************************/
/*  Init: Open and Initialize a Key Index.                             */
/***********************************************************************/
bool XINDEX::Init(PGLOBAL g)
  {
  /*********************************************************************/
  /*  Table will be accessed through an index table.                   */
  /*  If sorting is required, this will be done later.                 */
  /*********************************************************************/
  const char *ftype;
  BYTE   *mbase;
  char    fn[_MAX_PATH];
  int   *nv, k, n;
  bool    estim;
  PCOL    colp;
  PXCOL   prev = NULL, kcp = NULL;
  PDOSDEF defp = (PDOSDEF)Tdbp->To_Def;
  PDBUSER dup = PlgGetUser(g);

  /*********************************************************************/
  /*  Get the estimated table size.                                    */
  /*  Note: for fixed tables we must use cardinality to avoid the call */
  /*  to MaxBlkSize that could reduce the cardinality value.           */
  /*********************************************************************/
  if (Tdbp->Cardinality(NULL)) {
    // For DBF tables, Cardinality includes bad or soft deleted lines
    // that are not included in the index, and can be larger then the
    // index size.
    estim = (Tdbp->Ftype == RECFM_DBF);
    n = Tdbp->Cardinality(g);      // n is exact table size
  } else {
    // Variable table not optimized
    estim = true;                  // n is an estimate of the size
    n = Tdbp->GetMaxSize(g);
  } // endif Cardinality

  if (n <= 0)
    return !(n == 0);             // n < 0 error, n = 0 void table

  /*********************************************************************/
  /*  Get the first key column.                                        */
  /*********************************************************************/
  if (!Nk || !To_Cols || (!To_Vals && Op != OP_FIRST && Op != OP_FSTDIF)) {
    strcpy(g->Message, MSG(NO_KEY_COL));
    return true;    // Error
  } else
    colp = To_Cols[0];

  switch (Tdbp->Ftype) {
    case RECFM_VAR: ftype = ".dnx"; break;
    case RECFM_FIX: ftype = ".fnx"; break;
    case RECFM_BIN: ftype = ".bnx"; break;
    case RECFM_VCT: ftype = ".vnx"; break;
    case RECFM_DBF: ftype = ".dbx"; break;
    default:
      sprintf(g->Message, MSG(INVALID_FTYPE), Tdbp->Ftype);
      return true;
    } // endswitch Ftype

  if (defp->SepIndex()) {
    // Index was save in a separate file
#if !defined(UNIX)
    char drive[_MAX_DRIVE];
#else
    char *drive = NULL;
#endif
    char direc[_MAX_DIR];
    char fname[_MAX_FNAME];

    _splitpath(defp->GetOfn(), drive, direc, fname, NULL);
    strcat(strcat(fname, "_"), Xdp->GetName());
    _makepath(fn, drive, direc, fname, ftype);
  } else
    strcat(PlugRemoveType(fn, strcpy(fn, defp->GetOfn())), ftype);

  PlugSetPath(fn, fn, Tdbp->GetPath());

#if defined(TRACE)
  printf("Index %s file: %s\n", Xdp->GetName(), fn);
#endif   // TRACE

  /*********************************************************************/
  /*  Get a view on the part of the index file containing this index.  */
  /*********************************************************************/
  if (!(mbase = (BYTE*)X->FileView(g, fn, Defoff, Defhigh, Size)))
    goto err;

  //  Now start the mapping process.
  nv = (int*)mbase;
  mbase += NZ * sizeof(int);

#if defined(TRACE)
  printf("nv=%d %d %d %d %d %d %d\n",
    nv[0], nv[1], nv[2], nv[3], nv[4], nv[5], nv[6]);
#endif   // TRACE

  // The test on ID was suppressed because MariaDB can change an index ID
  // when other indexes are added or deleted 
  if (/*nv[0] != ID ||*/ nv[1] != Nk) {
    // Not this index
    sprintf(g->Message, MSG(BAD_INDEX_FILE), fn);
#if defined(TRACE)
    printf("nv[0]=%d ID=%d nv[1]=%d Nk=%d\n", nv[0], ID, nv[1], Nk);
#endif   // TRACE
    goto err;
    } // endif nv

  if (nv[2]) {
    // Set the offset array memory block
    Offset.Memp = mbase;
    Offset.Size = nv[2] * sizeof(int);
    Offset.Sub = true;
    Mul = true;
    Ndif = nv[2] - 1;
    mbase += Offset.Size;
  } else {
    Mul = false;
    Ndif = nv[3];
  } // endif nv[2]

  if (nv[3] < n && estim)
    n = nv[3];              // n was just an evaluated max value

  if (nv[3] != n) {
    sprintf(g->Message, MSG(OPT_NOT_MATCH), fn);
    goto err;
    } // endif

  Num_K = nv[3];
  Incr = nv[4];
  Nblk = nv[5];
  Sblk = nv[6];

  if (!Incr) {
    /*******************************************************************/
    /*  Point to the storage that contains the file positions.         */
    /*******************************************************************/
    Record.Size = Num_K * sizeof(int);
    Record.Memp = mbase;
    Record.Sub = true;
    mbase += Record.Size;
  } else
    Srtd = true;    // Sorted positions can be calculated

  /*********************************************************************/
  /*  Allocate the KXYCOL blocks used to store column values.          */
  /*********************************************************************/
  for (k = 0; k < Nk; k++) {
    if (k == Nval)
      To_LastVal = prev;

    nv = (int*)mbase;
    mbase += (NW * sizeof(int));

    colp = To_Cols[k];

    if (nv[4] != colp->GetResultType() || !colp->GetValue() ||
       (nv[3] != colp->GetValue()->GetClen() && nv[4] != TYPE_STRING)) {
      sprintf(g->Message, MSG(XCOL_MISMATCH), colp->GetName());
      goto err;    // Error
      } // endif GetKey

    kcp = new(g) KXYCOL(this);

    if (!(mbase = kcp->MapInit(g, colp, nv, mbase)))
      goto err;

    if (!kcp->Prefix)
      // Indicate that the key column value can be found from KXYCOL
      colp->SetKcol(kcp);

    if (prev) {
      kcp->Previous = prev;
      prev->Next = kcp;
    } else
      To_KeyCol = kcp;

    prev = kcp;
    } // endfor k

  To_LastCol = prev;

  if (Mul && prev)
    // Last key offset is the index offset
    kcp->Koff = Offset;

  /*********************************************************************/
  /*  No valid record read yet for secondary file.                     */
  /*********************************************************************/
  Cur_K = Num_K;
  return false;

err:
  Close();
  return true;
  } // end of Init
#endif   // XMAP

/***********************************************************************/
/*  Get Ndif and Num_K from the index file.                            */
/***********************************************************************/
bool XINDEX::GetAllSizes(PGLOBAL g, int &ndif, int &numk)
  {
  char   *ftype;
  char    fn[_MAX_PATH];
  int     n, nv[NZ], id = -1;
  bool    estim = false;
  PDOSDEF defp = (PDOSDEF)Tdbp->To_Def;

  ndif = numk = 0;

  /*********************************************************************/
  /*  Get the estimated table size.                                    */
  /*  Note: for fixed tables we must use cardinality to avoid the call */
  /*  to MaxBlkSize that could reduce the cardinality value.           */
  /*********************************************************************/
  if (Tdbp->Cardinality(NULL)) {
    // For DBF tables, Cardinality includes bad or soft deleted lines
    // that are not included in the index, and can be larger then the
    // index size.
    estim = (Tdbp->Ftype == RECFM_DBF);
    n = Tdbp->Cardinality(g);      // n is exact table size
  } else {
    // Variable table not optimized
    estim = true;                  // n is an estimate of the size
    n = Tdbp->GetMaxSize(g);
  } // endif Cardinality

  if (n <= 0)
    return !(n == 0);             // n < 0 error, n = 0 void table

  /*********************************************************************/
  /*  Check the key part number.                                       */
  /*********************************************************************/
  if (!Nk) {
    strcpy(g->Message, MSG(NO_KEY_COL));
    return true;    // Error
    } // endif Nk

  switch (Tdbp->Ftype) {
    case RECFM_VAR: ftype = ".dnx"; break;
    case RECFM_FIX: ftype = ".fnx"; break;
    case RECFM_BIN: ftype = ".bnx"; break;
    case RECFM_VCT: ftype = ".vnx"; break;
    case RECFM_DBF: ftype = ".dbx"; break;
    default:
      sprintf(g->Message, MSG(INVALID_FTYPE), Tdbp->Ftype);
      return true;
    } // endswitch Ftype

  if (defp->SepIndex()) {
    // Index was saved in a separate file
#if !defined(UNIX)
    char drive[_MAX_DRIVE];
#else
    char *drive = NULL;
#endif
    char direc[_MAX_DIR];
    char fname[_MAX_FNAME];

    _splitpath(defp->GetOfn(), drive, direc, fname, NULL);
    strcat(strcat(fname, "_"), Xdp->GetName());
    _makepath(fn, drive, direc, fname, ftype);
  } else {
    id = ID;
    strcat(PlugRemoveType(fn, strcpy(fn, defp->GetOfn())), ftype);
  } // endif sep

  PlugSetPath(fn, fn, Tdbp->GetPath());

#if defined(TRACE)
  printf("Index %s file: %s\n", Xdp->GetName(), fn);
#endif   // TRACE

  /*********************************************************************/
  /*  Open the index file and check its validity.                      */
  /*********************************************************************/
  if (X->Open(g, fn, id, MODE_READ))
    goto err;               // No saved values

  // Get offset from XDB file
//if (X->Seek(g, Defoff, Defhigh, SEEK_SET))
//  goto err;

  //  Now start the reading process.
  if (X->Read(g, nv, NZ, sizeof(int)))
    goto err;

#if defined(TRACE)
  printf("nv=%d %d %d %d\n", nv[0], nv[1], nv[2], nv[3]);
#endif   // TRACE

  // The test on ID was suppressed because MariaDB can change an index ID
  // when other indexes are added or deleted 
  if (/*nv[0] != ID ||*/ nv[1] != Nk) {
    sprintf(g->Message, MSG(BAD_INDEX_FILE), fn);
#if defined(TRACE)
    printf("nv[0]=%d ID=%d nv[1]=%d Nk=%d\n", nv[0], ID, nv[1], Nk);
#endif   // TRACE
    goto err;
    } // endif

  if (nv[2]) {
    Mul = true;
    Ndif = nv[2] - 1;  // nv[2] is offset size, equal to Ndif + 1
  } else {
    Mul = false;
    Ndif = nv[3];
  } // endif nv[2]

  if (nv[3] < n && estim)
    n = nv[3];              // n was just an evaluated max value

  if (nv[3] != n) {
    sprintf(g->Message, MSG(OPT_NOT_MATCH), fn);
    goto err;
    } // endif

  Num_K = nv[3];

  if (Nk > 1) {
    if (nv[2] && X->Seek(g, nv[2] * sizeof(int), 0, SEEK_CUR))
      goto err;

    if (!nv[4] && X->Seek(g, Num_K * sizeof(int), 0, SEEK_CUR))
      goto err;

    if (X->Read(g, nv, NW, sizeof(int)))
      goto err;

    PCOL colp = *To_Cols;

    if (nv[4] != colp->GetResultType()  ||
       (nv[3] != colp->GetValue()->GetClen() && nv[4] != TYPE_STRING)) {
      sprintf(g->Message, MSG(XCOL_MISMATCH), colp->GetName());
      goto err;    // Error
      } // endif GetKey

    Ndif = nv[0];
    } // endif Nk

  /*********************************************************************/
  /*  Set size values.                                                 */
  /*********************************************************************/
  ndif = Ndif;
  numk = Num_K;
  return false;

err:
  X->Close();
  return true;
  } // end of GetAllSizes

/***********************************************************************/
/*  RANGE: Tell how many records exist for a given value, for an array */
/*  of values, or in a given value range.                              */
/***********************************************************************/
int XINDEX::Range(PGLOBAL g, int limit, bool incl)
  {
  int  i, k, n = 0;
  PXOB *xp = To_Vals;
  PXCOL kp = To_KeyCol;
  OPVAL op = Op;

  switch (limit) {
    case 1: Op = (incl) ? OP_GE : OP_GT; break;
    case 2: Op = (incl) ? OP_GT : OP_GE; break;
    default: return 0;
    } // endswitch limit

  /*********************************************************************/
  /*  Currently only range of constant values with an EQ operator is   */
  /*  implemented.  Find the number of rows for each given values.     */
  /*********************************************************************/
  if (xp[0]->GetType() == TYPE_CONST) {
    for (i = 0; kp; kp = kp->Next) {
      kp->Valp->SetValue_pval(xp[i]->GetValue(), !kp->Prefix);
      if (++i == Nval) break;
      } // endfor kp

    if ((k = FastFind(Nval)) < Num_K)
      n = k;
//      if (limit)
//        n = (Mul) ? k : kp->Val_K;
//      else 
//        n = (Mul) ? Pof[kp->Val_K + 1] - k : 1;

  } else {
    strcpy(g->Message, MSG(RANGE_NO_JOIN));
    n = -1;                        // Logical error
  } // endif'f Type

  Op = op;
  return n;
  } // end of Range

/***********************************************************************/
/*  Return the size of the group (equal values) of the current value.  */
/***********************************************************************/
int XINDEX::GroupSize(void)
  {
#if defined(_DEBUG)
  assert(To_LastCol->Val_K >= 0 && To_LastCol->Val_K < Ndif);
#endif   // _DEBUG

  if (Nval == Nk)
    return (Pof) ? Pof[To_LastCol->Val_K + 1] - Pof[To_LastCol->Val_K]
                 : 1;

#if defined(_DEBUG)
  assert(To_LastVal);
#endif   // _DEBUG

  // Index whose only some columns are used
  int ck1, ck2;

  ck1 = To_LastVal->Val_K;
  ck2 = ck1 + 1;

#if defined(_DEBUG)
  assert(ck1 >= 0 && ck1 < To_LastVal->Ndf);
#endif   // _DEBUG

  for (PXCOL kcp = To_LastVal; kcp; kcp = kcp->Next) {
    ck1 = (kcp->Kof) ? kcp->Kof[ck1] : ck1;
    ck2 = (kcp->Kof) ? kcp->Kof[ck2] : ck2;
    } // endfor kcp

  return ck2 - ck1;
  } // end of GroupSize

/***********************************************************************/
/*  Find Cur_K and Val_K's of the next distinct value of the index.    */
/*  Returns false if Ok, true if there are no more different values.   */
/***********************************************************************/
bool XINDEX::NextValDif(void)
  {
  int  curk;
  PXCOL kcp = (To_LastVal) ? To_LastVal : To_LastCol;

  if (++kcp->Val_K < kcp->Ndf) {
    Cur_K = curk = kcp->Val_K;

    // (Cur_K return is currently not used by SQLGBX)
    for (PXCOL kp = kcp; kp; kp = kp->Next)
      Cur_K = (kp->Kof) ? kp->Kof[Cur_K] : Cur_K;

  } else
    return true;

  for (kcp = kcp->Previous; kcp; kcp = kcp->Previous) {
    if (kcp->Kof && curk < kcp->Kof[kcp->Val_K + 1])
      break;                  // all previous columns have same value

    curk = ++kcp->Val_K;      // This is a break, get new column value
    } // endfor kcp

  return false;
  } // end of NextValDif

/***********************************************************************/
/*  XINDEX: Find Cur_K and Val_K's of next index entry.                */
/*  If eq is true next values must be equal to last ones up to Nval.   */
/*  Returns false if Ok, true if there are no more (equal) values.     */
/***********************************************************************/
bool XINDEX::NextVal(bool eq)
  {
  int  n, neq = Nk + 1, curk;
  PXCOL kcp;

  if (Cur_K == Num_K)
    return true;
  else
    curk = ++Cur_K;

  for (n = Nk, kcp = To_LastCol; kcp; n--, kcp = kcp->Previous) {
    if (kcp->Kof) {
      if (curk == kcp->Kof[kcp->Val_K + 1])
        neq = n;

    } else {
#ifdef _DEBUG
      assert(curk == kcp->Val_K + 1);
#endif // _DEBUG
      neq = n;
    } // endif Kof

#ifdef _DEBUG
    assert(kcp->Val_K < kcp->Ndf);
#endif // _DEBUG

    // If this is not a break...
    if (neq > n)
      break;                  // all previous columns have same value

    curk = ++kcp->Val_K;      // This is a break, get new column value
    } // endfor kcp

  // Return true if no more values or, in case of "equal" values,
  // if the last used column value has changed
  return (Cur_K == Num_K || (eq && neq <= Nval));
  } // end of NextVal

/***********************************************************************/
/*  XINDEX: Fetch a physical or logical record.                        */
/***********************************************************************/
int XINDEX::Fetch(PGLOBAL g)
  {
  int  n;
  PXCOL kp;

  if (Num_K == 0)
    return -1;                   // means end of file

  /*********************************************************************/
  /*  Table read through a sorted index.                               */
  /*********************************************************************/
  switch (Op) {
    case OP_NEXT:                 // Read next
      if (NextVal(false))
        return -1;               // End of indexed file

      break;
    case OP_FIRST:                // Read first
      for (Cur_K = 0, kp = To_KeyCol; kp; kp = kp->Next)
        kp->Val_K = 0;

      Op = OP_NEXT;
      break;
    case OP_SAME:                 // Read next same
      // Logically the key values should be the same as before
#if defined(TRACE)
      printf("looking for next same value\n");
#endif   // TRACE

      if (NextVal(true)) {
        Op = OP_EQ;
        return -2;               // no more equal values
        } // endif NextVal

      break;
    case OP_NXTDIF:               // Read next dif
//      while (!NextVal(true)) ;

//      if (Cur_K >= Num_K)
//        return -1;               // End of indexed file
      if (NextValDif())
        return -1;               // End of indexed file

      break;
    case OP_FSTDIF:               // Read first diff
      for (Cur_K = 0, kp = To_KeyCol; kp; kp = kp->Next)
        kp->Val_K = 0;

      Op = (Mul || Nval < Nk) ? OP_NXTDIF : OP_NEXT;
      break;
    default:                      // Should be OP_EQ
//    if (Tbxp->Key_Rank < 0) {
        /***************************************************************/
        /*  Look for the first key equal to the link column values     */
        /*  and return its rank whithin the index table.               */
        /***************************************************************/
        for (n = 0, kp = To_KeyCol; n < Nval && kp; n++, kp = kp->Next)
          if (kp->InitFind(g, To_Vals[n]))
            return -1;               // No more constant values

        Nth++;

#if defined(TRACE)
        printf("Fetch: Looking for new value\n");
#endif   // TRACE
        Cur_K = FastFind(Nval);

        if (Cur_K >= Num_K)
          /*************************************************************/
          /* Rank not whithin index table, signal record not found.    */
          /*************************************************************/
          return -2;

        else if (Mul || Nval < Nk)
          Op = OP_SAME;

    } // endswitch Op

  /*********************************************************************/
  /*  If rank is equal to stored rank, record is already there.        */
  /*********************************************************************/
  if (Cur_K == Old_K)
    return -3;                   // Means record already there
  else
    Old_K = Cur_K;                // Store rank of newly read record

  /*********************************************************************/
  /*  Return the position of the required record.                      */
  /*********************************************************************/
  return (Incr) ? Cur_K * Incr : To_Rec[Cur_K];
  } // end of Fetch

/***********************************************************************/
/*  FastFind: Returns the index of matching record in a join using an  */
/*  optimized algorithm based on dichotomie and optimized comparing.   */
/***********************************************************************/
int XINDEX::FastFind(int nv)
  {
  register int  curk, sup, inf, i= 0, k, n = 2;
  register PXCOL kp, kcp;

  assert((int)nv == Nval);

  if (Nblk && Op == OP_EQ) {
    // Look in block values to find in which block to search
    sup = Nblk;
    inf = -1;

    while (n && sup - inf > 1) {
      i = (inf + sup) >> 1;

      n = To_KeyCol->CompBval(i);

      if (n < 0)
        sup = i;
      else
        inf = i;

      } // endwhile

    if (inf < 0)
      return Num_K;

//  i = inf;
    inf *= Sblk;

    if ((sup = inf + Sblk) > To_KeyCol->Ndf)
      sup = To_KeyCol->Ndf;

    inf--;
  } else {
    inf = -1;
    sup = To_KeyCol->Ndf;
  } // endif Nblk

  for (k = 0, kcp = To_KeyCol; kcp; kcp = kcp->Next) {
    while (sup - inf > 1) {
      i = (inf + sup) >> 1;

      n = kcp->CompVal(i);

      if      (n < 0)
        sup = i;
      else if (n > 0)
        inf = i;
      else
        break;

      } // endwhile

    if (n) {
      if (Op != OP_EQ) {
        // Currently only OP_GT or OP_GE
        kcp->Val_K = curk = sup;

        // Check for value changes in previous key parts
        for (kp = kcp->Previous; kp; kp = kp->Previous)
          if (kp->Kof && curk < kp->Kof[kp->Val_K + 1])
            break;
          else
            curk = ++kp->Val_K;

        n = 0;
        } // endif Op

      break;
      } // endif n

    kcp->Val_K = i;

    if (++k == Nval) {
      if (Op == OP_GT) {            // n is always 0
        curk = ++kcp->Val_K;        // Increment value by 1

        // Check for value changes in previous key parts
        for (kp = kcp->Previous; kp; kp = kp->Previous)
          if (kp->Kof && curk < kp->Kof[kp->Val_K + 1])
            break;                  // Not changed
          else
            curk = ++kp->Val_K;

        } // endif Op

      break;      // So kcp remains pointing the last tested block
      } // endif k

    if (kcp->Kof) {
      inf = kcp->Kof[i] - 1;
      sup = kcp->Kof[i + 1];
    } else {
      inf = i - 1;
      sup = i + 1;
    } // endif Kof

    } // endfor k, kcp

  if (n) {
    // Record not found
    for (kcp = To_KeyCol; kcp; kcp = kcp->Next)
      kcp->Val_K = kcp->Ndf;       // Not a valid value

    return Num_K;
    } // endif n

  for (curk = kcp->Val_K; kcp; kcp = kcp->Next) {
    kcp->Val_K = curk;
    curk = (kcp->Kof) ? kcp->Kof[kcp->Val_K] : kcp->Val_K;
    } // endfor kcp

  return curk;
  } // end of FastFind

/* -------------------------- XINDXS Class --------------------------- */

/***********************************************************************/
/*  XINDXS public constructor.                                         */
/***********************************************************************/
XINDXS::XINDXS(PTDBDOS tdbp, PIXDEF xdp, PXLOAD pxp, PCOL *cp, PXOB *xp)
      : XINDEX(tdbp, xdp, pxp, cp, xp)
  {
  Srtd = To_Cols[0]->GetOpt() < 0;          // ?????
  } // end of XINDXS constructor

/***********************************************************************/
/*  XINDXS compare routine for C Quick/Insertion sort.                 */
/***********************************************************************/
int XINDXS::Qcompare(int *i1, int *i2)
  {
#ifdef DEBTRACE
  num_comp++;
#endif

  return To_KeyCol->Compare(*i1, *i2);
  } // end of Qcompare

/***********************************************************************/
/*  Range: Tell how many records exist for given value(s):             */
/*  If limit=0 return range for these values.                          */
/*  If limit=1 return the start of range.                              */
/*  If limit=2 return the end of range.                                */
/***********************************************************************/
int XINDXS::Range(PGLOBAL g, int limit, bool incl)
  {
  int  k, n = 0;
  PXOB  xp = To_Vals[0];
  PXCOL kp = To_KeyCol;
  OPVAL op = Op;

  switch (limit) {
    case 1: Op = (incl) ? OP_GE : OP_GT; break;
    case 2: Op = (incl) ? OP_GT : OP_GE; break;
    default: Op = OP_EQ;
    } // endswitch limit

  /*********************************************************************/
  /*  Currently only range of constant values with an EQ operator is   */
  /*  implemented.  Find the number of rows for each given values.     */
  /*********************************************************************/
  if (xp->GetType() == TYPE_CONST) {
    kp->Valp->SetValue_pval(xp->GetValue(), !kp->Prefix);

    if ((k = FastFind(Nval)) < Num_K)
      if (limit)
        n = (Mul) ? k : kp->Val_K;
      else 
        n = (Mul) ? Pof[kp->Val_K + 1] - k : 1;

  } else {
    strcpy(g->Message, MSG(RANGE_NO_JOIN));
    n = -1;                        // Logical error
  } // endif'f Type

  Op = op;
  return n;
  } // end of Range

/***********************************************************************/
/*  Return the size of the group (equal values) of the current value.  */
/***********************************************************************/
int XINDXS::GroupSize(void)
  {
#if defined(_DEBUG)
  assert(To_KeyCol->Val_K >= 0 && To_KeyCol->Val_K < Ndif);
#endif   // _DEBUG
  return (Pof) ? Pof[To_KeyCol->Val_K + 1] - Pof[To_KeyCol->Val_K]
               : 1;
  } // end of GroupSize

/***********************************************************************/
/*  XINDXS: Find Cur_K and Val_K of next index value.                  */
/*  If b is true next value must be equal to last one.                 */
/*  Returns false if Ok, true if there are no more (equal) values.     */
/***********************************************************************/
bool XINDXS::NextVal(bool eq)
  {
  bool rc;

  if (To_KeyCol->Val_K == Ndif)
    return true;

  if (Mul) {
    int limit = Pof[To_KeyCol->Val_K + 1];

#ifdef _DEBUG
    assert(Cur_K < limit);
    assert(To_KeyCol->Val_K < Ndif);
#endif // _DEBUG

    if (++Cur_K == limit) {
      To_KeyCol->Val_K++;
      rc = (eq || limit == Num_K);
    } else
      rc = false;

  } else
    rc = (To_KeyCol->Val_K = ++Cur_K) == Num_K || eq;

  return rc;
  } // end of NextVal

/***********************************************************************/
/*  XINDXS: Fetch a physical or logical record.                        */
/***********************************************************************/
int XINDXS::Fetch(PGLOBAL g)
  {
  if (Num_K == 0)
    return -1;                   // means end of file

  /*********************************************************************/
  /*  Table read through a sorted index.                               */
  /*********************************************************************/
  switch (Op) {
    case OP_NEXT:                 // Read next
      if (NextVal(false))
        return -1;               // End of indexed file

      break;
    case OP_FIRST:                // Read first
      To_KeyCol->Val_K = Cur_K = 0;
      Op = OP_NEXT;
      break;
    case OP_SAME:                 // Read next same
#if defined(TRACE)
//      printf("looking for next same value\n");
#endif   // TRACE

      if (!Mul || NextVal(true)) {
        Op = OP_EQ;
        return -2;               // No more equal values
        } // endif Mul

      break;
    case OP_NXTDIF:               // Read next dif
      if (++To_KeyCol->Val_K == Ndif)
        return -1;               // End of indexed file

      Cur_K = Pof[To_KeyCol->Val_K];
      break;
    case OP_FSTDIF:               // Read first diff
      To_KeyCol->Val_K = Cur_K = 0;
      Op = (Mul) ? OP_NXTDIF : OP_NEXT;
      break;
    default:                      // Should OP_EQ
      /*****************************************************************/
      /*  Look for the first key equal to the link column values       */
      /*  and return its rank whithin the index table.                 */
      /*****************************************************************/
      if (To_KeyCol->InitFind(g, To_Vals[0]))                       
        return -1;                 // No more constant values     
      else                                                         
        Nth++;                                                     
                                                                   
#if defined(TRACE)                                                 
        printf("Fetch: Looking for new value\n");                   
#endif   // TRACE                                                   
                                                                   
      Cur_K = FastFind(1);                                         
                                                                   
      if (Cur_K >= Num_K)                                           
        // Rank not whithin index table, signal record not found
        return -2;
      else if (Mul)
        Op = OP_SAME;

    } // endswitch Op

  /*********************************************************************/
  /*  If rank is equal to stored rank, record is already there.        */
  /*********************************************************************/
  if (Cur_K == Old_K)
    return -3;                   // Means record already there
  else
    Old_K = Cur_K;                // Store rank of newly read record

  /*********************************************************************/
  /*  Return the position of the required record.                      */
  /*********************************************************************/
  return (Incr) ? Cur_K * Incr : To_Rec[Cur_K];
  } // end of Fetch

/***********************************************************************/
/*  FastFind: Returns the index of matching indexed record using an    */
/*  optimized algorithm based on dichotomie and optimized comparing.   */
/***********************************************************************/
int XINDXS::FastFind(int nk)
  {
  register int  sup, inf, i= 0, n = 2;
  register PXCOL kcp = To_KeyCol;

  if (Nblk && Op == OP_EQ) {
    // Look in block values to find in which block to search
    sup = Nblk;
    inf = -1;

    while (n && sup - inf > 1) {
      i = (inf + sup) >> 1;

      n = kcp->CompBval(i);

      if (n < 0)
        sup = i;
      else
        inf = i;

      } // endwhile

    if (inf < 0)
      return Num_K;

//  i = inf;
    inf *= Sblk;

    if ((sup = inf + Sblk) > Ndif)
      sup = Ndif;

    inf--;
  } else {
    inf = -1;
    sup = Ndif;
  } // endif Nblk

  while (sup - inf > 1) {
    i = (inf + sup) >> 1;

    n = kcp->CompVal(i);

    if      (n < 0)
      sup = i;
    else if (n > 0)
      inf = i;
    else
      break;

    } // endwhile

  if (!n && Op == OP_GT) {
    ++i;
  } else if (n && Op != OP_EQ) {
    // Currently only OP_GT or OP_GE
    i = sup;
    n = 0;
  } // endif sup

  kcp->Val_K = i;                 // Used by FillValue
  return ((n) ? Num_K : (Mul) ? Pof[i] : i);
  } // end of FastFind

/* -------------------------- XLOAD Class --------------------------- */

/***********************************************************************/
/*  XLOAD constructor.                                                 */
/***********************************************************************/
XLOAD::XLOAD(void)
  {
  Hfile = INVALID_HANDLE_VALUE;
#if defined(WIN32) && defined(XMAP)    
  ViewBase = NULL;
#endif   // WIN32  &&         XMAP
  NewOff.Val = 0LL;
} // end of XLOAD constructor

/***********************************************************************/
/*  Close the index huge file.                                         */
/***********************************************************************/
void XLOAD::Close(void)
  {
  if (Hfile != INVALID_HANDLE_VALUE) {
    CloseFileHandle(Hfile);
    Hfile = INVALID_HANDLE_VALUE;
    } // endif Hfile

#if defined(WIN32) && defined(XMAP)
  if (ViewBase) {
    if (!UnmapViewOfFile(ViewBase))
      printf("Error %d closing Viewmap\n", GetLastError());

    ViewBase = NULL;
    } // endif ViewBase
#endif   // WIN32 && XMAP

  } // end of Close

/* --------------------------- XFILE Class --------------------------- */

/***********************************************************************/
/*  XFILE constructor.                                                 */
/***********************************************************************/
XFILE::XFILE(void) : XLOAD()
  {
  Xfile = NULL;
#if defined(XMAP) && !defined(WIN32)
  Mmp = NULL;
#endif   // XMAP  &&         !WIN32
  } // end of XFILE constructor

/***********************************************************************/
/*  Xopen function: opens a file using native API's.                   */
/***********************************************************************/
bool XFILE::Open(PGLOBAL g, char *filename, int id, MODE mode)
  {
  char *pmod;
  bool  rc;
  IOFF  noff[MAX_INDX];

  /*********************************************************************/
  /*  Open the index file according to mode.                           */
  /*********************************************************************/
  switch (mode) {
    case MODE_READ:   pmod = "rb"; break;
    case MODE_WRITE:  pmod = "wb"; break;
    case MODE_INSERT: pmod = "ab"; break;
    default:
      sprintf(g->Message, MSG(BAD_FUNC_MODE), "Xopen", mode);
      return true;
    } // endswitch mode

  if (!(Xfile= global_fopen(g, MSGID_OPEN_ERROR_AND_STRERROR, filename, pmod))) {
#if defined(TRACE)
    printf("Open: %s\n", g->Message);
#endif   // TRACE
    return true;
    } // endif Xfile

  if (mode == MODE_INSERT) {
    /*******************************************************************/
    /* Position the cursor at end of file so ftell returns file size.  */
    /*******************************************************************/
    if (fseek(Xfile, 0, SEEK_END)) {
      sprintf(g->Message, MSG(FUNC_ERRNO), errno, "Xseek");
      return true;
      } // endif
    
    NewOff.Low = (int)ftell(Xfile);
  } else if (mode == MODE_WRITE) {
    if (id >= 0) {
      // New not sep index file. Write the header.
      memset(noff, 0, sizeof(noff));
      Write(g, noff, sizeof(IOFF), MAX_INDX, rc);
      fseek(Xfile, 0, SEEK_END);
      NewOff.Low = (int)ftell(Xfile);
      } // endif id

  } else if (mode == MODE_READ && id >= 0) {
    // Get offset from the header
    if (fread(noff, sizeof(IOFF), MAX_INDX, Xfile) != MAX_INDX) {
      sprintf(g->Message, MSG(XFILE_READERR), errno);
      return true;
      } // endif MAX_INDX

    // Position the cursor at the offset of this index
    if (fseek(Xfile, noff[id].Low, SEEK_SET)) {
      sprintf(g->Message, MSG(FUNC_ERRNO), errno, "Xseek");
      return true;
      } // endif
    
  } // endif mode

  return false;
  } // end of Open

#if 0
/***********************************************************************/
/*  Tell were we are in the index file.                                */
/***********************************************************************/
bool XFILE::GetOff(int& low, int& high, PIXDEF sxp)
  {
  if (sxp) {
    low  = sxp->GetOffset() + sxp->GetSize();
    high = 0;
  } else
    low = high = 0;

  return false;
  } // end of GetOff
#endif // 0

/***********************************************************************/
/*  Tell were we are in a huge file.                                   */
/***********************************************************************/
bool XFILE::Seek(PGLOBAL g, int low, int high, int origin)
  {
#if defined(_DEBUG)
  assert(high == 0);
#endif  // !_DEBUG

  if (fseek(Xfile, low, origin)) {
    sprintf(g->Message, MSG(FUNC_ERRNO), errno, "Xseek");
    return true;
    } // endif

//ftell(Xfile);
  return false;
  } // end of Seek

/***********************************************************************/
/*  Read from the index file.                                          */
/***********************************************************************/
bool XFILE::Read(PGLOBAL g, void *buf, int n, int size)
  {
  if (fread(buf, size, n, Xfile) != (size_t)n) {
    sprintf(g->Message, MSG(XFILE_READERR), errno);
    return true;
    } // endif size

  return false;
  } // end of Read

/***********************************************************************/
/*  Write on index file, set rc and return the number of bytes written */
/***********************************************************************/
int XFILE::Write(PGLOBAL g, void *buf, int n, int size, bool& rc)
  {
  int niw = (int)fwrite(buf, size, n, Xfile);

  if (niw != n) {
    sprintf(g->Message, MSG(XFILE_WRITERR), strerror(errno));
    rc = true;
    } // endif size

  return niw * size;
  } // end of Write

/***********************************************************************/
/*  Update the file header and close the index file.                   */
/***********************************************************************/
void XFILE::Close(char *fn, int id)
  {
  if (id >= 0 && fn && Xfile) {
    fclose(Xfile);

    if ((Xfile = fopen(fn, "r+b")))
      if (!fseek(Xfile, id * sizeof(IOFF), SEEK_SET))
        fwrite(&NewOff,  sizeof(int), 2, Xfile);

    } // endif id

  Close();
  } // end of Close

/***********************************************************************/
/*  Close the index file.                                              */
/***********************************************************************/
void XFILE::Close(void)
  {
  XLOAD::Close();

  if (Xfile) {
    fclose(Xfile);
    Xfile = NULL;
    } // endif Xfile

#if defined(XMAP) && !defined(WIN32)
  if (Mmp) {
    CloseMemMap(Mmp->memory, Mmp->lenL);
    Mmp = NULL;
    } // endif Mmp
#endif   // XMAP
  } // end of Close

#if defined(XMAP)
#if defined(WIN32)
/***********************************************************************/
/*  Return a pointer to the segment at the given offset and size.      */
/***********************************************************************/
void *XFILE::FileView(PGLOBAL g, char *fn, int loff, int hoff, int size)
  {
  SYSTEM_INFO SysInfo;  // system information; used to get the granularity
  char   *pData;        // pointer to the data
  int     iViewDelta;   // the offset into the view where the data shows up
  HANDLE  hMapFile;     // handle for the file's memory-mapped region
  DWORD   offset;       // Where to start in the index file
  DWORD   FileMapSize;  // size of the file mapping
  DWORD   FileMapStart; // where in the file to start the file map view
  DWORD   Granularity;  // system allocation granularity
  DWORD   MapViewSize;  // the size of the view

  if (hoff) {
    strcpy(g->Message, MSG(HI_OFFSET_ERR));
    return NULL;
    } // endf hoff

  // Open the file in mode read only
  Hfile = CreateFile(fn, GENERIC_READ, FILE_SHARE_READ, NULL,
                     OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

  if (Hfile == INVALID_HANDLE_VALUE) {
    char  buf[512];
    DWORD rc = GetLastError();

    sprintf(g->Message, MSG(OPEN_ERROR), rc, MODE_READ, fn);
    FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM |
                  FORMAT_MESSAGE_IGNORE_INSERTS, NULL, rc, 0,
                  (LPTSTR)buf, sizeof(buf), NULL);
    strcat(g->Message, buf);
    return NULL;
    } // endif Hfile

  // Get the system allocation granularity.
  GetSystemInfo(&SysInfo);
  Granularity = SysInfo.dwAllocationGranularity;

  // The offset is a 64 byte integer low part
  offset = loff;

  // To calculate where to start the file mapping, round down the
  // offset of the data into the file to the nearest multiple of the
  // system allocation granularity.
  FileMapStart = (offset / Granularity) * Granularity;

  // Calculate the size of the file mapping view.
  MapViewSize = offset % Granularity + (DWORD)size;

  // How large will the file-mapping object be?
  FileMapSize = offset + (DWORD)size;

  // The data of interest isn't at the beginning of the
  // view, so determine how far into the view to set the pointer.
  iViewDelta = (int)(offset - FileMapStart);

  // Check that the index file is more than large enough
  if (GetFileSize(Hfile, NULL) < FileMapSize) {
    strcpy(g->Message, MSG(XFILE_TOO_SMALL));
    return NULL;
    } // endif FileSize

  // Create a file-mapping object for the file.
  hMapFile = CreateFileMapping( Hfile,         // current file handle
                                NULL,          // default security
                                PAGE_READONLY, // permission
                                0,             // size, high
                                FileMapSize,   // size, low
                                NULL);         // name

  if (hMapFile == NULL) {
    sprintf(g->Message, MSG(HANDLE_IS_NULL), "hMapFile", GetLastError() );
    return NULL;
    } // endif hMapFile

  // Map the view.
  ViewBase = MapViewOfFile(hMapFile,      // handle to mapping object
                           FILE_MAP_READ, // access mode
                           0,             // high-order 32 bits of file offset
                           FileMapStart,  // low-order 32 bits of file offset
                           MapViewSize);  // number of bytes to map

  if (!ViewBase) {
    sprintf(g->Message, MSG(HANDLE_IS_NULL), "ViewBase", GetLastError());
    return NULL;
    } // endif ViewBase

  // Calculate the pointer to the data.
  pData = (char *)ViewBase + iViewDelta;

  // close the file-mapping object
  if (!CloseHandle(hMapFile))
    sprintf(g->Message, MSG(MAP_OBJ_ERR), GetLastError());

  // close the file itself
  if (!CloseHandle(Hfile))
    sprintf(g->Message, MSG(FILE_CLOSE_ERR), GetLastError());
  else
    Hfile = INVALID_HANDLE_VALUE;

  return pData;
  } // end of FileView
#else  // not WIN32
  /*********************************************************************/
  /*  Map the entire index.                                            */
  /*********************************************************************/
void *XFILE::FileView(PGLOBAL g, char *fn, int loff, int hoff, int size)
  {
  HANDLE  h;

  Mmp = (MMP)PlugSubAlloc(g, NULL, sizeof(MEMMAP));
  h = CreateFileMap(g, filename, Mmp, MODE_READ, false);

  if (h == INVALID_HANDLE_VALUE || (!Mmp->lenH && !Mmp->lenL)) {
    if (!(*g->Message))
      strcpy(g->Message, MSG(FILE_MAP_ERR));

    CloseFileHandle(h);                    // Not used anymore
    return NULL;               // No saved values
    } // endif h

  CloseFileHandle(h);                    // Not used anymore
  return Mmp->memory;
  } // end of FileView
#endif // not WIN32
#endif // XMAP

/* -------------------------- XHUGE Class --------------------------- */

/***********************************************************************/
/*  Xopen function: opens a file using native API's.                   */
/***********************************************************************/
bool XHUGE::Open(PGLOBAL g, char *filename, int id, MODE mode)
  {
  LONG  high = 0;
  DWORD drc, rc;
  IOFF  noff[MAX_INDX];

  if (Hfile != INVALID_HANDLE_VALUE) {
    sprintf(g->Message, MSG(FILE_OPEN_YET), filename);
    return true;
    } // endif

#if defined(TRACE)
 printf( "Xopen: filename=%s mode=%d\n", filename, mode);
#endif   // TRACE

#if defined(WIN32)
  DWORD access, share, creation;

  /*********************************************************************/
  /*  Create the file object according to access mode                  */
  /*********************************************************************/
  switch (mode) {
    case MODE_READ:
      access = GENERIC_READ;
      share = FILE_SHARE_READ;
      creation = OPEN_EXISTING;
      break;
    case MODE_WRITE:
      access = GENERIC_WRITE;
      share = 0;
      creation = CREATE_ALWAYS;
      break;
    case MODE_INSERT:
      access = GENERIC_WRITE;
      share = 0;
      creation = OPEN_EXISTING;
      break;
    default:
      sprintf(g->Message, MSG(BAD_FUNC_MODE), "Xopen", mode);
      return true;
    } // endswitch

  Hfile = CreateFile(filename, access, share, NULL, creation,
                               FILE_ATTRIBUTE_NORMAL, NULL);

  if (Hfile == INVALID_HANDLE_VALUE) {
    rc = GetLastError();
    sprintf(g->Message, MSG(OPEN_ERROR), rc, mode, filename);
    FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM |
                  FORMAT_MESSAGE_IGNORE_INSERTS, NULL, rc, 0,
                  (LPTSTR)filename, sizeof(filename), NULL);
    strcat(g->Message, filename);
    return true;
    } // endif Hfile

#ifdef DEBTRACE
 fprintf(debug,
 " access=%p share=%p creation=%d handle=%p fn=%s\n",
  access, share, creation, Hfile, filename);
#endif

  if (mode == MODE_INSERT) {
    /*******************************************************************/
    /* In Insert mode we must position the cursor at end of file.      */
    /*******************************************************************/
    rc = SetFilePointer(Hfile, 0, &high, FILE_END);

    if (rc == INVALID_SET_FILE_POINTER && (drc = GetLastError()) != NO_ERROR) {
      sprintf(g->Message, MSG(ERROR_IN_SFP), drc);
      CloseHandle(Hfile);
      Hfile = INVALID_HANDLE_VALUE;
      return true;
      } // endif

    NewOff.Low = (int)rc;
    NewOff.High = (int)high;
  } else if (mode == MODE_WRITE) {
    if (id >= 0) {
      // New not sep index file. Write the header.
      memset(noff, 0, sizeof(noff));
      rc = WriteFile(Hfile, noff, sizeof(noff), &drc, NULL);
      NewOff.Low = (int)drc;
      } // endif id

  } else if (mode == MODE_READ && id >= 0) {
    // Get offset from the header
    rc = ReadFile(Hfile, noff, sizeof(noff), &drc, NULL);

    if (!rc) {
      sprintf(g->Message, MSG(XFILE_READERR), GetLastError());
      return true;
      } // endif rc

    // Position the cursor at the offset of this index
    rc = SetFilePointer(Hfile, noff[id].Low, 
                       (PLONG)&noff[id].High, FILE_BEGIN);

    if (rc == INVALID_SET_FILE_POINTER) {
      sprintf(g->Message, MSG(FUNC_ERRNO), GetLastError(), "SetFilePointer");
      return true;
      } // endif
    
  } // endif Mode

#else   // UNIX
  int    rc = 0;
  int    oflag = O_LARGEFILE;         // Enable file size > 2G
  mode_t pmod = 0;

  /*********************************************************************/
  /*  Create the file object according to access mode                  */
  /*********************************************************************/
  switch (mode) {
    case MODE_READ:
      oflag |= O_RDONLY;
      break;
    case MODE_WRITE:
      oflag |= O_WRONLY | O_CREAT | O_TRUNC;
      pmod = S_IREAD | S_IWRITE;
      break;
    case MODE_INSERT:
      oflag |= (O_WRONLY | O_APPEND);
      break;
    default:
      sprintf(g->Message, MSG(BAD_FUNC_MODE), "Xopen", mode);
      return true;
    } // endswitch

  Hfile= global_open(g, MSGID_OPEN_ERROR_AND_STRERROR, filename, oflag, pmod);

  if (Hfile == INVALID_HANDLE_VALUE) {
    rc = errno;
#if defined(TRACE)
    printf("Open: %s\n", g->Message);
#endif   // TRACE
    return true;
    } // endif Hfile

#if defined(TRACE)
  printf(" rc=%d oflag=%p mode=%d handle=%d fn=%s\n",
           rc, oflag, mode, Hfile, filename);
#endif   // TRACE

  if (mode == MODE_INSERT) {
    /*******************************************************************/
    /* Position the cursor at end of file so ftell returns file size.  */
    /*******************************************************************/
    if (!(Offset.Val = (longlong)lseek64(Hfile, 0LL, SEEK_END))) {
      sprintf(g->Message, MSG(FUNC_ERRNO), errno, "Seek");
      return true;
      } // endif
    
  } else if (mode == MODE_WRITE) {
    if (id >= 0) {
      // New not sep index file. Write the header.
      memset(noff, 0, sizeof(noff));
      Write(g, noff, sizeof(IOFF), MAX_INDX, rc);
      Offset.Val = (longlong)(sizeof(IOFF) * MAX_INDX);      
      } // endif id

  } else if (mode == MODE_READ && id >= 0) {
    // Get offset from the header
    if (read(Hfile, noff, sizeof(noff)) != sizeof(noff)) {
      sprintf(g->Message, MSG(READ_ERROR), "Index file", strerror(errno));
      return true;
      } // endif MAX_INDX

    // Position the cursor at the offset of this index
    if (!lseek64(Hfile, noff[id].Val, SEEK_SET)) {
      sprintf(g->Message, MSG(FUNC_ERRNO), errno, "Hseek");
      return true;
      } // endif
    
  } // endif mode
#endif  // UNIX

  return false;
  } // end of Open

#if 0
/***********************************************************************/
/*  Get the offset of this index in the index file.                    */
/***********************************************************************/
bool XHUGE::GetOff(int& low, int& high, PIXDEF sxp)
  {
  if (!sxp) {
    low = 0;
    high = 0;
    return false;
    } // endif sxp

#if defined(WIN32)
  LARGE_INTEGER ln;

  ln.LowPart = sxp->GetOffset();
  ln.HighPart = sxp->GetOffhigh();
  ln.QuadPart += (LONGLONG)sxp->GetSize();
  low = ln.LowPart;
  high = (int)ln.HighPart;
#else  // UNIX
#define G4   ((off64_t)0x100 * (off64_t)0x1000000)
#if defined(TRACE)
  printf("in GetOff...\n");
#endif   // TRACE
  off64_t pos;

  pos  = (off64_t)sxp->GetOffset() + (off64_t)sxp->GetOffhigh() * G4;
  pos += (off64_t)sxp->GetSize();
  low  = (int)(pos % G4);
  high = (int)(pos / G4);
#endif // UNIX
  return false;
  } // end of GetOff
#endif // 0

/***********************************************************************/
/*  Go to position in a huge file.                                     */
/***********************************************************************/
bool XHUGE::Seek(PGLOBAL g, int low, int high, int origin)
  {
#if defined(WIN32)
  LONG  hi = high;
  DWORD rc = SetFilePointer(Hfile, low, &hi, origin);

  if (rc == INVALID_SET_FILE_POINTER && GetLastError() != NO_ERROR) {
    sprintf(g->Message, MSG(FUNC_ERROR), "Xseek");
    return true;
    } // endif

#else // UNIX
  off64_t pos = (off64_t)low
              + (off64_t)high * ((off64_t)0x100 * (off64_t)0x1000000);

  if (lseek64(Hfile, pos, origin) < 0) {
    sprintf(g->Message, MSG(ERROR_IN_LSK), errno);
#if defined(TRACE)
    printf("lseek64 error %d\n", errno);
#endif   // TRACE
    return true;
    } // endif lseek64

#if defined(TRACE)
  printf("Seek: low=%d high=%d\n", low, high);
#endif   // TRACE
#endif // UNIX

  return false;
  } // end of Seek

/***********************************************************************/
/*  Read from a huge index file.                                       */
/***********************************************************************/
bool XHUGE::Read(PGLOBAL g, void *buf, int n, int size)
  {
  bool rc = false;

#if defined(WIN32)
  bool    brc;
  DWORD   nbr, count = (DWORD)(n * size);

  brc = ReadFile(Hfile, buf, count, &nbr, NULL);

  if (brc) {
    if (nbr != count) {
      strcpy(g->Message, MSG(EOF_INDEX_FILE));
      rc = true;
      } // endif nbr

  } else {
    char *buf[256];
    DWORD drc = GetLastError();

    FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM |
                  FORMAT_MESSAGE_IGNORE_INSERTS, NULL, drc, 0,
                  (LPTSTR)buf, sizeof(buf), NULL);
    sprintf(g->Message, MSG(READ_ERROR), "index file", buf);
    rc = true;
  } // endif brc
#else    // UNIX
  ssize_t count = (ssize_t)(n * size);

#if defined(TRACE)
  printf("Hfile=%d n=%d size=%d count=%d\n", Hfile, n, size, count);
#endif   // TRACE

  if (read(Hfile, buf, count) != count) {
    sprintf(g->Message, MSG(READ_ERROR), "Index file", strerror(errno));
#if defined(TRACE)
    printf("read error %d\n", errno);
#endif   // TRACE
    rc = true;
    } // endif nbr
#endif   // UNIX

  return rc;
  } // end of Read

/***********************************************************************/
/*  Write on a huge index file.                                        */
/***********************************************************************/
int XHUGE::Write(PGLOBAL g, void *buf, int n, int size, bool& rc)
  {
#if defined(WIN32)
  bool    brc;
  DWORD   nbw, count = (DWORD)n * (DWORD) size;

  brc = WriteFile(Hfile, buf, count, &nbw, NULL);

  if (!brc) {
    char msg[256];
    DWORD drc = GetLastError();

    FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM |
                  FORMAT_MESSAGE_IGNORE_INSERTS, NULL, drc, 0,
                  (LPTSTR)msg, sizeof(msg), NULL);
    sprintf(g->Message, MSG(WRITING_ERROR), "index file", msg);
    rc = true;
    } // endif size

  return (int)nbw;
#else    // UNIX
  ssize_t nbw;
  size_t  count = (size_t)n * (size_t)size;

  nbw = write(Hfile, buf, count);

  if (nbw != (signed)count) {
    sprintf(g->Message, MSG(WRITING_ERROR),
                        "index file", strerror(errno));
    rc = true;
    } // endif nbw

  return (int)nbw;
#endif   // UNIX
  } // end of Write

/***********************************************************************/
/*  Update the file header and close the index file.                   */
/***********************************************************************/
void XHUGE::Close(char *fn, int id)
  {
#if defined(WIN32)
  if (id >= 0 && fn) {
    CloseFileHandle(Hfile);
    Hfile = CreateFile(fn, GENERIC_READ | GENERIC_WRITE, 0, NULL,
                       OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (Hfile != INVALID_HANDLE_VALUE)
      if (SetFilePointer(Hfile, id * sizeof(IOFF), NULL, FILE_BEGIN)
              != INVALID_SET_FILE_POINTER) {
        DWORD nbw;

        WriteFile(Hfile, &NewOff,  sizeof(longlong), &nbw, NULL);
//      WriteFile(Hfile, &Newhigh, sizeof(int), &nbw, NULL);
        } // endif SetFilePointer

    } // endif id
#else   // !WIN32
  if (id >= 0 && fn) {
    fnctl(Hfile, F_SETFD, O_WRONLY);
    if (lseek(Hfile, id * sizeof(IOFF), SEEK_SET))
      write(Hfile, &noff[id], sizeof(IOFF));

    } // endif id
#endif  // !WIN32

  XLOAD::Close();
  } // end of Close

#if defined(XMAP)
#if defined(WIN32)
/***********************************************************************/
/*  Return a pointer to the segment at the given offset and size.      */
/***********************************************************************/
void *XHUGE::FileView(PGLOBAL g, char *fn, int loff, int hoff, int size)
  {
  SYSTEM_INFO SysInfo;  // system information; used to get the granularity
  char   *pData;        // pointer to the data
  int     iViewDelta;   // the offset into the view where the data shows up
  HANDLE hMapFile;      // handle for the file's memory-mapped region
  LARGE_INTEGER lint;   // a utility holder
  __int64 offset;       // Where to start in the index file
  __int64 FileMapSize;  // size of the file mapping
  __int64 FileMapStart; // where in the file to start the file map view
  __int64 Granularity;  // system allocation granularity
  DWORD   MapViewSize;  // the size of the view

  // Open the file in mode read only
  if (Open(g, fn, MODE_READ))
    return NULL;

  // Get the system allocation granularity.
  GetSystemInfo(&SysInfo);
  Granularity = (__int64)SysInfo.dwAllocationGranularity;

  // Calculate the offset as a 64 byte integer
  lint.LowPart = loff;
  lint.HighPart = hoff;
  offset = lint.QuadPart;

  // To calculate where to start the file mapping, round down the
  // offset of the data into the file to the nearest multiple of the
  // system allocation granularity.
  FileMapStart = (offset / Granularity) * Granularity;

  // Calculate the size of the file mapping view.
  MapViewSize = (DWORD)(offset % Granularity) + (DWORD)size;

  // How large will the file-mapping object be?
  FileMapSize = offset + (__int64)size;

  // The data of interest isn't at the beginning of the
  // view, so determine how far into the view to set the pointer.
  iViewDelta = (int)(offset - FileMapStart);

  // Let the user know that the index file is more than large enough
  lint.LowPart = GetFileSize(Hfile,  (LPDWORD)&lint.HighPart);

  // Prepare the low and high parts of the size.
  lint.QuadPart = FileMapSize;

  // Create a file-mapping object for the file.
  hMapFile = CreateFileMapping( Hfile,            // current file handle
                                NULL,             // default security
                                PAGE_READONLY,    // permission
                                lint.HighPart,    // size, high
                                lint.LowPart,     // size, low
                                NULL);            // name

  if (hMapFile == NULL) {
    sprintf(g->Message, MSG(HANDLE_IS_NULL), "hMapFile",  GetLastError());
    return NULL;
    } // endif hMapFile

  // Prepare the low and high parts of the starting file offset.
  lint.QuadPart = FileMapStart;

  // Map the view.
  ViewBase = MapViewOfFile(hMapFile,      // handle to mapping object
                           FILE_MAP_READ, // access mode
                           lint.HighPart, // high-order 32 bits of file offset
                           lint.LowPart,  // low-order 32 bits of file offset
                           MapViewSize);  // number of bytes to map

  if (!ViewBase) {
    sprintf(g->Message, MSG(HANDLE_IS_NULL), "ViewBase", GetLastError());
    return NULL;
    } // endif ViewBase

  // Calculate the pointer to the data.
  pData = (char *)ViewBase + iViewDelta;

  // close the file-mapping object
  if (!CloseHandle(hMapFile))
    sprintf(g->Message, MSG(MAP_OBJ_ERR), GetLastError());

  // close the file itself
  if (!CloseHandle(Hfile))
    sprintf(g->Message, MSG(FILE_CLOSE_ERR), GetLastError());
  else
    Hfile = INVALID_HANDLE_VALUE;

  return pData;
  } // end of FileView
#else  // not WIN32
/***********************************************************************/
/*  Don't know whether this is possible for non Windows OS.            */
/***********************************************************************/
void *XHUGE::FileView(PGLOBAL g, char *fn,
                         int loff, int hoff, int size)
  {
  strcpy(g->Message, MSG(NO_PART_MAP));
  return NULL;
  } // end of FileView
#endif   // not WIN32
#endif   // XMAP

/* -------------------------- XXROW Class --------------------------- */

/***********************************************************************/
/*  XXROW Public Constructor.                                          */
/***********************************************************************/
XXROW::XXROW(PTDBDOS tdbp) : XXBASE(tdbp, false)
  {
  Tdbp = tdbp;
  Valp = NULL;
  } // end of XXROW constructor

/***********************************************************************/
/*  XXROW Reset: re-initialize a Kindex block.                         */
/***********************************************************************/
void XXROW::Reset(void)
  {
#if defined(_DEBUG)
  assert(Tdbp->GetLink());                // This a join index
#endif   // _DEBUG
  } // end of Reset

/***********************************************************************/
/*  Init: Open and Initialize a Key Index.                             */
/***********************************************************************/
bool XXROW::Init(PGLOBAL g)
  {
  /*********************************************************************/
  /*  Table will be accessed through an index table.                   */
  /*  To_Link should not be NULL.                                      */
  /*********************************************************************/
  if (!Tdbp->GetLink() || Tbxp->GetKnum() != 1)
    return true;

  if ((*Tdbp->GetLink())->GetResultType() != TYPE_INT) {
    strcpy(g->Message, MSG(TYPE_MISMATCH));
    return true;
  } else
    Valp = (*Tdbp->GetLink())->GetValue();

  if ((Num_K = Tbxp->Cardinality(g)) < 0)
    return true;                   // Not a fixed file

  /*********************************************************************/
  /*  The entire table is indexed, no need to construct the index.     */
  /*********************************************************************/
  Cur_K = Num_K;
  return false;
  } // end of Init

/***********************************************************************/
/*  RANGE: Tell how many record exist in a given value range.          */
/***********************************************************************/
int XXROW::Range(PGLOBAL g, int limit, bool incl)
  {
  int  n = Valp->GetIntValue();

  switch (limit) {
    case 1: n += ((incl) ? 0 : 1); break;
    case 2: n += ((incl) ? 1 : 0); break;
    default: n = 1;
    } // endswitch limit

  return n;
  } // end of Range

/***********************************************************************/
/*  XXROW: Fetch a physical or logical record.                         */
/***********************************************************************/
int XXROW::Fetch(PGLOBAL g)
  {
  if (Num_K == 0)
    return -1;       // means end of file

  /*********************************************************************/
  /*  Look for a key equal to the link column of previous table,       */
  /*  and return its rank whithin the index table.                     */
  /*********************************************************************/
  Cur_K = FastFind(1);

  if (Cur_K >= Num_K)
    /*******************************************************************/
    /* Rank not whithin index table, signal record not found.          */
    /*******************************************************************/
    return -2;      // Means record not found

  /*********************************************************************/
  /*  If rank is equal to stored rank, record is already there.        */
  /*********************************************************************/
  if (Cur_K == Old_K)
    return -3;                   // Means record already there
  else
    Old_K = Cur_K;                // Store rank of newly read record

  return Cur_K;
  } // end of Fetch

/***********************************************************************/
/*  FastFind: Returns the index of matching record in a join.          */
/***********************************************************************/
int XXROW::FastFind(int nk)
  {
  int n = Valp->GetIntValue();

  if (n < 0)
    return (Op == OP_EQ) ? (-1) : 0;
  else if (n > Num_K)
    return Num_K;
  else
    return (Op == OP_GT) ? n : (n - 1);

  } // end of FastFind

/* ------------------------- KXYCOL Classes -------------------------- */

/***********************************************************************/
/*  KXYCOL public constructor.                                         */
/***********************************************************************/
KXYCOL::KXYCOL(PKXBASE kp) : To_Keys(Keys.Memp), 
        To_Bkeys(Bkeys.Memp), Kof((CPINT&)Koff.Memp)
  {
  Next = NULL;
  Previous = NULL;
  Kxp = kp;
  Colp = NULL;
  IsSorted = false;
  Asc = true;
  Keys = Nmblk;
  Kblp = NULL;
  Bkeys = Nmblk;
  Blkp = NULL;
  Valp = NULL;
  Klen = 0;
  Kprec = 0;
  Type = TYPE_ERROR;
  Prefix = false;
  Koff = Nmblk;
  Val_K = 0;
  Ndf = 0;
  Mxs = 0;
  } // end of KXYCOL constructor

/***********************************************************************/
/*  KXYCOL Init: initialize and allocate storage.                      */
/*  Key length kln can be smaller than column length for CHAR columns. */
/***********************************************************************/
bool KXYCOL::Init(PGLOBAL g, PCOL colp, int n, bool sm, int kln)
  {
  int len = colp->GetLength(), prec = colp->GetPrecision();

  // Currently no indexing on NULL columns
  if (colp->IsNullable()) {
    sprintf(g->Message, "Cannot index nullable column %s", colp->GetName());
    return true;
    } // endif nullable

  if (kln && len > kln && colp->GetResultType() == TYPE_STRING) {
    len = kln;
    Prefix = true;
    } // endif kln

#ifdef DEBTRACE
 htrc("KCOL(%p) Init: col=%s n=%d type=%d sm=%d\n",
  this, colp->GetName(), n, colp->GetResultType(), sm);
#endif

  // Allocate the Value object used when moving items
  Type = colp->GetResultType();

  if (!(Valp = AllocateValue(g, Type, len, colp->GetPrecision())))
    return true;

  Klen = Valp->GetClen();
  Keys.Size = n * Klen;

  if (!PlgDBalloc(g, NULL, Keys)) {
    sprintf(g->Message, MSG(KEY_ALLOC_ERROR), Klen, n);
    return true;    // Error
    } // endif

  // Allocate the Valblock. The last parameter is to have rows filled
  // by blanks (if true) or keep the zero ending char (if false).
  // Currently we set it to true to be compatible with QRY blocks,
  // and the one before last is to enable length/type checking, set to
  // true if not a prefix key.
  Kblp = AllocValBlock(g, To_Keys, Type, n, len, prec, !Prefix, true);
  Asc = sm;                    // Sort mode: Asc=true  Desc=false
  Ndf = n;

  // Store this information to avoid sorting when already done
  if (Asc)
    IsSorted = colp->GetOpt() < 0;

//MayHaveNulls = colp->HasNulls();
  return false;
  } // end of Init

#if defined(XMAP)
/***********************************************************************/
/*  KXYCOL MapInit: initialize and address storage.                    */
/*  Key length kln can be smaller than column length for CHAR columns. */
/***********************************************************************/
BYTE* KXYCOL::MapInit(PGLOBAL g, PCOL colp, int *n, BYTE *m)
  {
  int len = colp->GetLength(), prec = colp->GetPrecision();

  if (n[3] && colp->GetLength() > n[3]
           && colp->GetResultType() == TYPE_STRING) {
    len = n[3];
    Prefix = true;
    } // endif kln

  Type = colp->GetResultType();

#ifdef DEBTRACE
 htrc("MapInit(%p): colp=%p type=%d n=%d len=%d m=%p\n",
  this, colp, Type, n[0], len, m);
#endif

  // Allocate the Value object used when moving items
  Valp = AllocateValue(g, Type, len, prec, NULL);
  Klen = Valp->GetClen();

  if (n[2]) {
    Bkeys.Size = n[2] * Klen;
    Bkeys.Memp = m;
    Bkeys.Sub = true;

    // Allocate the Valblk containing initial block key values
    Blkp = AllocValBlock(g, To_Bkeys, Type, n[2], len, prec, true, true);
    } // endif nb

  Keys.Size = n[0] * Klen;
  Keys.Memp = m + Bkeys.Size;
  Keys.Sub = true;

  // Allocate the Valblock. Last two parameters are to have rows filled
  // by blanks (if true) or keep the zero ending char (if false).
  // Currently we set it to true to be compatible with QRY blocks,
  // and last one to enable type checking (no conversion).
  Kblp = AllocValBlock(g, To_Keys, Type, n[0], len, prec, true, true);

  if (n[1]) {
    Koff.Size = n[1] * sizeof(int);
    Koff.Memp = m + Bkeys.Size + Keys.Size;
    Koff.Sub = true;
    } // endif n[1]

  Ndf = n[0];
  IsSorted = colp->GetOpt() < 0;
  return m + Bkeys.Size + Keys.Size + Koff.Size;
  } // end of MapInit
#endif // XMAP

/***********************************************************************/
/*  Allocate the offset block used by intermediate key columns.        */
/***********************************************************************/
int *KXYCOL::MakeOffset(PGLOBAL g, int n)
  {
  if (!Kof) {
    // Calculate the initial size of the offset
    Koff.Size = (n + 1) * sizeof(int);

    // Allocate the required memory
    if (!PlgDBalloc(g, NULL, Koff)) {
      strcpy(g->Message, MSG(KEY_ALLOC_ERR));
      return NULL;    // Error
     } // endif

  } else if (n) {
    // This is a reallocation call
    PlgDBrealloc(g, NULL, Koff, (n + 1) * sizeof(int));
  } else
    PlgDBfree(Koff);

  return (int*)Kof;
  } // end of MakeOffset

/***********************************************************************/
/*  Make a front end array of key values that are the first value of   */
/*  each blocks (of size n). This to reduce paging in FastFind.        */
/***********************************************************************/
bool KXYCOL::MakeBlockArray(PGLOBAL g, int nb, int size)
  {
  int i, k;

  // Calculate the size of the block array in the index
  Bkeys.Size = nb * Klen;

  // Allocate the required memory
  if (!PlgDBalloc(g, NULL, Bkeys)) {
    sprintf(g->Message, MSG(KEY_ALLOC_ERROR), Klen, nb);
    return true;    // Error
    } // endif

  // Allocate the Valblk used to contains initial block key values
  Blkp = AllocValBlock(g, To_Bkeys, Type, nb, Klen, Kprec);

  // Populate the array with values
  for (i = k = 0; i < nb; i++, k += size)
    Blkp->SetValue(Kblp, i, k);

  return false;
  } // end of MakeBlockArray

/***********************************************************************/
/*  KXYCOL SetValue: read column value for nth array element.           */
/***********************************************************************/
void KXYCOL::SetValue(PCOL colp, int i)
  {
#if defined(_DEBUG)
  assert (Kblp != NULL);
#endif

  Kblp->SetValue(colp->GetValue(), (int)i);
  } // end of SetValue

/***********************************************************************/
/*  InitFind: initialize finding the rank of column value in index.    */
/***********************************************************************/
bool KXYCOL::InitFind(PGLOBAL g, PXOB xp)
  {
  if (xp->GetType() == TYPE_CONST) {
    if (Kxp->Nth)
      return true;

    Valp->SetValue_pval(xp->GetValue(), !Prefix);
  } else {
    xp->Reset();
    xp->Eval(g);
    Valp->SetValue_pval(xp->GetValue(), false);
//  Valp->SetValue_pval(xp->GetValue(), !Prefix);
  } // endif Type

  return false;
  } // end of InitFind

/***********************************************************************/
/*  InitBinFind: initialize Value to the value pointed by vp.          */
/***********************************************************************/
void KXYCOL::InitBinFind(void *vp)
  {
  Valp->SetBinValue(vp);
  } // end of InitBinFind

/***********************************************************************/
/*  KXYCOL FillValue: called by COLBLK::Eval when a column value is    */
/*  already in storage in the corresponding KXYCOL.                    */
/***********************************************************************/
void KXYCOL::FillValue(PVAL valp)
  {
  valp->SetValue_pvblk(Kblp, Val_K);
  } // end of FillValue

/***********************************************************************/
/*  KXYCOL: Compare routine for one numeric value.                     */
/***********************************************************************/
int KXYCOL::Compare(int i1, int i2)
  {
  // Do the actual comparison between values.
  register int k = (int)Kblp->CompVal((int)i1, (int)i2);

#ifdef DEBUG2
 htrc("Compare done result=%d\n", k);
#endif

  return (Asc) ? k : -k;
  } // end of Compare

/***********************************************************************/
/*  KXYCOL: Compare the ith key to the stored Value.                   */
/***********************************************************************/
int KXYCOL::CompVal(int i)
  {
  // Do the actual comparison between numerical values.
#ifdef DEBUG2
  register int k = (int)Kblp->CompVal(Valp, (int)i);

  htrc("Compare done result=%d\n", k);
  return k;
#endif
  return (int)Kblp->CompVal(Valp, (int)i);
  } // end of CompVal

/***********************************************************************/
/*  KXYCOL: Compare the key to the stored block value.                 */
/***********************************************************************/
int KXYCOL::CompBval(int i)
  {
  // Do the actual comparison between key values.
  return (int)Blkp->CompVal(Valp, (int)i);
  } // end of CompBval

/***********************************************************************/
/*  KXYCOL ReAlloc: ReAlloc To_Data if it is not suballocated.         */
/***********************************************************************/
void KXYCOL::ReAlloc(PGLOBAL g, int n)
  {
  PlgDBrealloc(g, NULL, Keys, n * Klen);
  Kblp->ReAlloc(To_Keys, n);
  Ndf = n;
  } // end of ReAlloc

/***********************************************************************/
/*  KXYCOL FreeData: Free To_Keys if it is not suballocated.           */
/***********************************************************************/
void KXYCOL::FreeData(void)
  {
  PlgDBfree(Keys);
  Kblp = NULL;
  PlgDBfree(Bkeys);
  Blkp = NULL;
  PlgDBfree(Koff);
  Ndf = 0;
  } // end of FreeData
