/* Copyright (C) Olivier Bertrand 2004 - 2013

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; version 2 of the License.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA */

/**************** MYCAT H Declares Source Code File (.H) ***************/
/*  Name: MYCAT.H  Version 2.3                                         */
/*                                                                     */
/*  This file contains the CONNECT plugin MYCAT class definitions.     */
/***********************************************************************/
#ifndef __MYCAT__H
#define __MYCAT__H

#include "block.h"
#include "catalog.h"

// Possible value for catalog functions
#define FNC_NO      (1 << 0)    // Not a catalog table         
#define FNC_COL     (1 << 1)    // Column catalog function     
#define FNC_TABLE   (1 << 2)    // Table catalog function      
#define FNC_DSN     (1 << 3)    // Data Source catalog function
#define FNC_DRIVER  (1 << 4)    // Column catalog function     
#define FNC_NIY     (1 << 5)    // Catalog function NIY        

typedef class ha_connect     *PHC;

TABTYPE GetTypeID(const char *type);
bool    IsFileType(TABTYPE type);
uint    GetFuncID(const char *func);

/***********************************************************************/
/*  MYCAT: class for managing the CONNECT plugin DB items.             */
/***********************************************************************/
class MYCAT : public CATALOG {
 public:
  MYCAT(PHC hc);                       // Constructor

  // Implementation
  PHC     GetHandler(void) {return Hc;}
  void    SetHandler(PHC hc) {Hc= hc;}

  // Methods
  void    Reset(void);
  void    SetDataPath(PGLOBAL g, const char *path) 
              {SetPath(g, &DataPath, path);}
  bool    GetBoolCatInfo(LPCSTR name, PSZ what, bool bdef);
  bool    SetIntCatInfo(LPCSTR name, PSZ what, int ival);
  int     GetIntCatInfo(LPCSTR name, PSZ what, int idef);
  int     GetSizeCatInfo(LPCSTR name, PSZ what, PSZ sdef);
  int     GetCharCatInfo(LPCSTR name, PSZ what, PSZ sdef, char *buf, int size);
  char   *GetStringCatInfo(PGLOBAL g, PSZ name, PSZ what, PSZ sdef);
  int     GetColCatInfo(PGLOBAL g, PTABDEF defp); 
  bool    GetIndexInfo(PGLOBAL g, PTABDEF defp);
  bool    StoreIndex(PGLOBAL g, PTABDEF defp) {return false;}  // Temporary
  PRELDEF GetTableDesc(PGLOBAL g, LPCSTR name,
                                  LPCSTR am, PRELDEF *prp = NULL);
  PTDB    GetTable(PGLOBAL g, PTABLE tablep, MODE mode = MODE_READ);
  void    ClearDB(PGLOBAL g);

 protected:
  PRELDEF MakeTableDesc(PGLOBAL g, LPCSTR name, LPCSTR am);
  void    SetPath(PGLOBAL g, LPCSTR *datapath, const char *path);

  // Members
  ha_connect *Hc;                          // The Connect handler
  }; // end of class MYCAT

#endif /* __MYCAT__H */
