/* Copyright (C) 2003 MySQL AB

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA */

#define DBTUX_NODE_CPP
#include "Dbtux.hpp"

/*
 * Allocate index node in TUP.
 */
int
Dbtux::allocNode(Signal* signal, NodeHandle& node)
{
  Frag& frag = node.m_frag;
  Uint32 pageId = NullTupLoc.getPageId();
  Uint32 pageOffset = NullTupLoc.getPageOffset();
  Uint32* node32 = 0;
  int errorCode = c_tup->tuxAllocNode(signal, frag.m_tupIndexFragPtrI, pageId, pageOffset, node32);
  jamEntry();
  if (errorCode == 0) {
    jam();
    node.m_loc = TupLoc(pageId, pageOffset);
    node.m_node = reinterpret_cast<TreeNode*>(node32);
    ndbrequire(node.m_loc != NullTupLoc && node.m_node != 0);
  }
  return errorCode;
}

/*
 * Set handle to point to existing node.
 */
void
Dbtux::selectNode(Signal* signal, NodeHandle& node, TupLoc loc)
{
  Frag& frag = node.m_frag;
  ndbrequire(loc != NullTupLoc);
  Uint32 pageId = loc.getPageId();
  Uint32 pageOffset = loc.getPageOffset();
  Uint32* node32 = 0;
  c_tup->tuxGetNode(frag.m_tupIndexFragPtrI, pageId, pageOffset, node32);
  jamEntry();
  node.m_loc = loc;
  node.m_node = reinterpret_cast<TreeNode*>(node32);
  ndbrequire(node.m_loc != NullTupLoc && node.m_node != 0);
}

/*
 * Set handle to point to new node.  Uses the pre-allocated node.
 */
void
Dbtux::insertNode(Signal* signal, NodeHandle& node)
{
  Frag& frag = node.m_frag;
  TupLoc loc = frag.m_freeLoc;
  frag.m_freeLoc = NullTupLoc;
  selectNode(signal, node, loc);
  new (node.m_node) TreeNode();
#ifdef VM_TRACE
  TreeHead& tree = frag.m_tree;
  memset(node.getPref(), DataFillByte, tree.m_prefSize << 2);
  TreeEnt* entList = tree.getEntList(node.m_node);
  memset(entList, NodeFillByte, (tree.m_maxOccup + 1) * (TreeEntSize << 2));
#endif
}

/*
 * Delete existing node.
 */
void
Dbtux::deleteNode(Signal* signal, NodeHandle& node)
{
  Frag& frag = node.m_frag;
  ndbrequire(node.getOccup() == 0);
  TupLoc loc = node.m_loc;
  Uint32 pageId = loc.getPageId();
  Uint32 pageOffset = loc.getPageOffset();
  Uint32* node32 = reinterpret_cast<Uint32*>(node.m_node);
  c_tup->tuxFreeNode(signal, frag.m_tupIndexFragPtrI, pageId, pageOffset, node32);
  jamEntry();
  // invalidate handle and storage
  node.m_loc = NullTupLoc;
  node.m_node = 0;
}

/*
 * Set prefix.  Copies the number of words that fits.  Includes
 * attribute headers for now.  XXX use null mask instead
 */
void
Dbtux::setNodePref(Signal* signal, NodeHandle& node)
{
  const Frag& frag = node.m_frag;
  const TreeHead& tree = frag.m_tree;
  readKeyAttrs(frag, node.getMinMax(0), 0, c_entryKey);
  copyAttrs(frag, c_entryKey, node.getPref(), tree.m_prefSize);
}

// node operations

/*
 * Add entry at position.  Move entries greater than or equal to the old
 * one (if any) to the right.
 *
 *            X
 *            v
 *      A B C D E _ _  =>  A B C X D E _
 *      0 1 2 3 4 5 6      0 1 2 3 4 5 6
 */
void
Dbtux::nodePushUp(Signal* signal, NodeHandle& node, unsigned pos, const TreeEnt& ent)
{
  Frag& frag = node.m_frag;
  TreeHead& tree = frag.m_tree;
  const unsigned occup = node.getOccup();
  ndbrequire(occup < tree.m_maxOccup && pos <= occup);
  // fix scans
  ScanOpPtr scanPtr;
  scanPtr.i = node.getNodeScan();
  while (scanPtr.i != RNIL) {
    jam();
    c_scanOpPool.getPtr(scanPtr);
    TreePos& scanPos = scanPtr.p->m_scanPos;
    ndbrequire(scanPos.m_loc == node.m_loc && scanPos.m_pos < occup);
    if (scanPos.m_pos >= pos) {
      jam();
#ifdef VM_TRACE
      if (debugFlags & DebugScan) {
        debugOut << "Fix scan " << scanPtr.i << " " << *scanPtr.p << endl;
        debugOut << "At pushUp pos=" << pos << " " << node << endl;
      }
#endif
      scanPos.m_pos++;
    }
    scanPtr.i = scanPtr.p->m_nodeScan;
  }
  // fix node
  TreeEnt* const entList = tree.getEntList(node.m_node);
  entList[occup] = entList[0];
  TreeEnt* const tmpList = entList + 1;
  for (unsigned i = occup; i > pos; i--) {
    jam();
    tmpList[i] = tmpList[i - 1];
  }
  tmpList[pos] = ent;
  entList[0] = entList[occup + 1];
  node.setOccup(occup + 1);
  // fix prefix
  if (occup == 0 || pos == 0)
    setNodePref(signal, node);
}

/*
 * Remove and return entry at position.  Move entries greater than the
 * removed one to the left.  This is the opposite of nodePushUp.
 *
 *                               D
 *            ^                  ^
 *      A B C D E F _  =>  A B C E F _ _
 *      0 1 2 3 4 5 6      0 1 2 3 4 5 6
 */
void
Dbtux::nodePopDown(Signal* signal, NodeHandle& node, unsigned pos, TreeEnt& ent)
{
  Frag& frag = node.m_frag;
  TreeHead& tree = frag.m_tree;
  const unsigned occup = node.getOccup();
  ndbrequire(occup <= tree.m_maxOccup && pos < occup);
  ScanOpPtr scanPtr;
  // move scans whose entry disappears
  scanPtr.i = node.getNodeScan();
  while (scanPtr.i != RNIL) {
    jam();
    c_scanOpPool.getPtr(scanPtr);
    TreePos& scanPos = scanPtr.p->m_scanPos;
    ndbrequire(scanPos.m_loc == node.m_loc && scanPos.m_pos < occup);
    const Uint32 nextPtrI = scanPtr.p->m_nodeScan;
    if (scanPos.m_pos == pos) {
      jam();
#ifdef VM_TRACE
      if (debugFlags & DebugScan) {
        debugOut << "Move scan " << scanPtr.i << " " << *scanPtr.p << endl;
        debugOut << "At popDown pos=" << pos << " " << node << endl;
      }
#endif
      scanNext(signal, scanPtr);
    }
    scanPtr.i = nextPtrI;
  }
  // fix other scans
  scanPtr.i = node.getNodeScan();
  while (scanPtr.i != RNIL) {
    jam();
    c_scanOpPool.getPtr(scanPtr);
    TreePos& scanPos = scanPtr.p->m_scanPos;
    ndbrequire(scanPos.m_loc == node.m_loc && scanPos.m_pos < occup);
    ndbrequire(scanPos.m_pos != pos);
    if (scanPos.m_pos > pos) {
      jam();
#ifdef VM_TRACE
      if (debugFlags & DebugScan) {
        debugOut << "Fix scan " << scanPtr.i << " " << *scanPtr.p << endl;
        debugOut << "At popDown pos=" << pos << " " << node << endl;
      }
#endif
      scanPos.m_pos--;
    }
    scanPtr.i = scanPtr.p->m_nodeScan;
  }
  // fix node
  TreeEnt* const entList = tree.getEntList(node.m_node);
  entList[occup] = entList[0];
  TreeEnt* const tmpList = entList + 1;
  ent = tmpList[pos];
  for (unsigned i = pos; i < occup - 1; i++) {
    jam();
    tmpList[i] = tmpList[i + 1];
  }
  entList[0] = entList[occup - 1];
  node.setOccup(occup - 1);
  // fix prefix
  if (occup != 1 && pos == 0)
    setNodePref(signal, node);
}

/*
 * Add entry at existing position.  Move entries less than or equal to
 * the old one to the left.  Remove and return old min entry.
 *
 *            X            A
 *      ^     v            ^
 *      A B C D E _ _  =>  B C D X E _ _
 *      0 1 2 3 4 5 6      0 1 2 3 4 5 6
 */
void
Dbtux::nodePushDown(Signal* signal, NodeHandle& node, unsigned pos, TreeEnt& ent)
{
  Frag& frag = node.m_frag;
  TreeHead& tree = frag.m_tree;
  const unsigned occup = node.getOccup();
  ndbrequire(occup <= tree.m_maxOccup && pos < occup);
  ScanOpPtr scanPtr;
  // move scans whose entry disappears
  scanPtr.i = node.getNodeScan();
  while (scanPtr.i != RNIL) {
    jam();
    c_scanOpPool.getPtr(scanPtr);
    TreePos& scanPos = scanPtr.p->m_scanPos;
    ndbrequire(scanPos.m_loc == node.m_loc && scanPos.m_pos < occup);
    const Uint32 nextPtrI = scanPtr.p->m_nodeScan;
    if (scanPos.m_pos == 0) {
      jam();
#ifdef VM_TRACE
      if (debugFlags & DebugScan) {
        debugOut << "Move scan " << scanPtr.i << " " << *scanPtr.p << endl;
        debugOut << "At pushDown pos=" << pos << " " << node << endl;
      }
#endif
      // here we may miss a valid entry "X"  XXX known bug
      scanNext(signal, scanPtr);
    }
    scanPtr.i = nextPtrI;
  }
  // fix other scans
  scanPtr.i = node.getNodeScan();
  while (scanPtr.i != RNIL) {
    jam();
    c_scanOpPool.getPtr(scanPtr);
    TreePos& scanPos = scanPtr.p->m_scanPos;
    ndbrequire(scanPos.m_loc == node.m_loc && scanPos.m_pos < occup);
    ndbrequire(scanPos.m_pos != 0);
    if (scanPos.m_pos <= pos) {
      jam();
#ifdef VM_TRACE
      if (debugFlags & DebugScan) {
        debugOut << "Fix scan " << scanPtr.i << " " << *scanPtr.p << endl;
        debugOut << "At pushDown pos=" << pos << " " << node << endl;
      }
#endif
      scanPos.m_pos--;
    }
    scanPtr.i = scanPtr.p->m_nodeScan;
  }
  // fix node
  TreeEnt* const entList = tree.getEntList(node.m_node);
  entList[occup] = entList[0];
  TreeEnt* const tmpList = entList + 1;
  TreeEnt oldMin = tmpList[0];
  for (unsigned i = 0; i < pos; i++) {
    jam();
    tmpList[i] = tmpList[i + 1];
  }
  tmpList[pos] = ent;
  ent = oldMin;
  entList[0] = entList[occup];
  // fix prefix
  if (true)
    setNodePref(signal, node);
}

/*
 * Remove and return entry at position.  Move entries less than the
 * removed one to the right.  Replace min entry by the input entry.
 * This is the opposite of nodePushDown.
 *
 *      X                        D
 *      v     ^                  ^
 *      A B C D E _ _  =>  X A B C E _ _
 *      0 1 2 3 4 5 6      0 1 2 3 4 5 6
 */
void
Dbtux::nodePopUp(Signal* signal, NodeHandle& node, unsigned pos, TreeEnt& ent)
{
  Frag& frag = node.m_frag;
  TreeHead& tree = frag.m_tree;
  const unsigned occup = node.getOccup();
  ndbrequire(occup <= tree.m_maxOccup && pos < occup);
  ScanOpPtr scanPtr;
  // move scans whose entry disappears
  scanPtr.i = node.getNodeScan();
  while (scanPtr.i != RNIL) {
    jam();
    c_scanOpPool.getPtr(scanPtr);
    TreePos& scanPos = scanPtr.p->m_scanPos;
    ndbrequire(scanPos.m_loc == node.m_loc && scanPos.m_pos < occup);
    const Uint32 nextPtrI = scanPtr.p->m_nodeScan;
    if (scanPos.m_pos == pos) {
      jam();
#ifdef VM_TRACE
      if (debugFlags & DebugScan) {
        debugOut << "Move scan " << scanPtr.i << " " << *scanPtr.p << endl;
        debugOut << "At popUp pos=" << pos << " " << node << endl;
      }
#endif
      // here we may miss a valid entry "X"  XXX known bug
      scanNext(signal, scanPtr);
    }
    scanPtr.i = nextPtrI;
  }
  // fix other scans
  scanPtr.i = node.getNodeScan();
  while (scanPtr.i != RNIL) {
    jam();
    c_scanOpPool.getPtr(scanPtr);
    TreePos& scanPos = scanPtr.p->m_scanPos;
    ndbrequire(scanPos.m_loc == node.m_loc && scanPos.m_pos < occup);
    ndbrequire(scanPos.m_pos != pos);
    if (scanPos.m_pos < pos) {
      jam();
#ifdef VM_TRACE
      if (debugFlags & DebugScan) {
        debugOut << "Fix scan " << scanPtr.i << " " << *scanPtr.p << endl;
        debugOut << "At popUp pos=" << pos << " " << node << endl;
      }
#endif
      scanPos.m_pos++;
    }
    scanPtr.i = scanPtr.p->m_nodeScan;
  }
  // fix node
  TreeEnt* const entList = tree.getEntList(node.m_node);
  entList[occup] = entList[0];
  TreeEnt* const tmpList = entList + 1;
  TreeEnt newMin = ent;
  ent = tmpList[pos];
  for (unsigned i = pos; i > 0; i--) {
    jam();
    tmpList[i] = tmpList[i - 1];
  }
  tmpList[0] = newMin;
  entList[0] = entList[occup];
  // fix prefix
  if (true)
    setNodePref(signal, node);
}

/*
 * Move number of entries from another node to this node before the min
 * (i=0) or after the max (i=1).  Expensive but not often used.
 */
void
Dbtux::nodeSlide(Signal* signal, NodeHandle& dstNode, NodeHandle& srcNode, unsigned cnt, unsigned i)
{
  Frag& frag = dstNode.m_frag;
  TreeHead& tree = frag.m_tree;
  ndbrequire(i <= 1);
  while (cnt != 0) {
    TreeEnt ent;
    nodePopDown(signal, srcNode, i == 0 ? srcNode.getOccup() - 1 : 0, ent);
    nodePushUp(signal, dstNode, i == 0 ? 0 : dstNode.getOccup(), ent);
    cnt--;
  }
}

/*
 * Link scan to the list under the node.  The list is single-linked and
 * ordering does not matter.
 */
void
Dbtux::linkScan(NodeHandle& node, ScanOpPtr scanPtr)
{
#ifdef VM_TRACE
  if (debugFlags & DebugScan) {
    debugOut << "Link scan " << scanPtr.i << " " << *scanPtr.p << endl;
    debugOut << "To node " << node << endl;
  }
#endif
  ndbrequire(! islinkScan(node, scanPtr) && scanPtr.p->m_nodeScan == RNIL);
  scanPtr.p->m_nodeScan = node.getNodeScan();
  node.setNodeScan(scanPtr.i);
}

/*
 * Unlink a scan from the list under the node.
 */
void
Dbtux::unlinkScan(NodeHandle& node, ScanOpPtr scanPtr)
{
#ifdef VM_TRACE
  if (debugFlags & DebugScan) {
    debugOut << "Unlink scan " << scanPtr.i << " " << *scanPtr.p << endl;
    debugOut << "From node " << node << endl;
  }
#endif
  ScanOpPtr currPtr;
  currPtr.i = node.getNodeScan();
  ScanOpPtr prevPtr;
  prevPtr.i = RNIL;
  while (true) {
    jam();
    c_scanOpPool.getPtr(currPtr);
    Uint32 nextPtrI = currPtr.p->m_nodeScan;
    if (currPtr.i == scanPtr.i) {
      jam();
      if (prevPtr.i == RNIL) {
        node.setNodeScan(nextPtrI);
      } else {
        jam();
        prevPtr.p->m_nodeScan = nextPtrI;
      }
      scanPtr.p->m_nodeScan = RNIL;
      // check for duplicates
      ndbrequire(! islinkScan(node, scanPtr));
      return;
    }
    prevPtr = currPtr;
    currPtr.i = nextPtrI;
  }
}

/*
 * Check if a scan is linked to this node.  Only for ndbrequire.
 */
bool
Dbtux::islinkScan(NodeHandle& node, ScanOpPtr scanPtr)
{
  ScanOpPtr currPtr;
  currPtr.i = node.getNodeScan();
  while (currPtr.i != RNIL) {
    jam();
    c_scanOpPool.getPtr(currPtr);
    if (currPtr.i == scanPtr.i) {
      jam();
      return true;
    }
    currPtr.i = currPtr.p->m_nodeScan;
  }
  return false;
}

void
Dbtux::NodeHandle::progError(int line, int cause, const char* file)
{
  ErrorReporter::handleAssert("Dbtux::NodeHandle: assert failed", file, line);
}
