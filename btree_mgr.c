/*
 * btree_mgr.c
 *
 * A minimal B+ tree manager that uses a straightforward sequential allocation model.
 *   - Page 0 contains the maximum keys per node (the "fan out" or node limit).
 *   - Node pages start at page 1. Each node page begins with a boolean that indicates
 *     whether the node has one key (false) or two keys (true). The rest of the page
 *     stores the node details.
 *
 * Global variables:
 *   gHighestPage  - Tracks the largest page number allocated (1-based for nodes)
 *   gScanIndex    - Tracks the current index into the sorted key list while scanning
 *
 * Only DT_INT keys are supported.
 *
 * Author: Apurv Gaikwad, Nishant Dalvi, Satyam Borade

 */

 #include <stdio.h>
 #include <stdlib.h>
 #include <string.h>
 #include "buffer_mgr.h"
 #include "storage_mgr.h"
 #include "dberror.h"
 #include "btree_mgr.h"
 #include "tables.h"
 #include "expr.h"
 
 /* Global counters used in this B+ tree manager */
 int gHighestPage = 0;         /* The highest node page number allocated (1-based) */
 int gScanIndex   = 0;         /* Position in the sorted key list during a tree scan */
 const RID RID_NONE = { -1, -1 }; /* A sentinel RID for invalid references */
 
 /*
  * NodeInPage:
  * Describes how a node is represented on disk after an initial bool flag.
  *
  * Fields:
  *   parentIdx   : Page number of this node's parent (-1 if root)
  *   nodeLeafBit : True if the node is a leaf; in this design all nodes are leaves
  *   leftSlot    : RID tied to the first key
  *   leftKey     : The first integer key; -1 if unused
  *   rightSlot   : RID for the second key (if used)
  *   rightKey    : The second integer key; -1 if unused
  *   chainLink   : Placeholder for leaf chaining (unused)
  */
 typedef struct NodeInPage {
     int  parentIdx;
     bool nodeLeafBit;
     RID  leftSlot;
     int  leftKey;
     RID  rightSlot;
     int  rightKey;
     RID  chainLink;
 } NodeInPage;
 
 /*
  * CoreIndex:
  * Maintains metadata for the B+ tree and references buffer management objects.
  * A pointer to CoreIndex is stored in BTreeHandle->mgmtData.
  *
  * Fields:
  *   poolRef   : A pointer to the buffer pool
  *   pageRef   : A page handle for read/write operations
  *   topNode   : The page number of the root node (initialized on the first insertion)
  *   keysTotal : The total count of keys in the tree
  *   nodeLimit : The maximum keys allowed per node (loaded from page 0)
  */
 typedef struct CoreIndex {
     BM_BufferPool *poolRef;
     BM_PageHandle *pageRef;
     int topNode;
     int keysTotal;
     int nodeLimit;
 } CoreIndex;
 
 /* ========================= INDEX MANAGER FUNCTIONS ========================= */
 
 /* initIndexManager: prepares storage management. */
 RC initIndexManager(void *unused) {
     printf("Initializing the minimal B+ tree manager.\n");
     initStorageManager();
     return RC_OK;
 }
 
 /* shutdownIndexManager: no special teardown needed. */
 RC shutdownIndexManager() {
     printf("Shutting down the minimal B+ tree manager.\n");
     return RC_OK;
 }
 
 /*
  * createBtree:
  * Produces a page file named idxId and writes n (the node limit) into page 0.
  * Supports only DT_INT keys.
  */
 RC createBtree(char *idxId, DataType keyType, int n) {
     printf("Creating index file '%s'\n", idxId);
     RC rc = createPageFile(idxId);
     if (rc != RC_OK)
         return rc;
 
     SM_FileHandle fileCtrl;
     rc = openPageFile(idxId, &fileCtrl);
     if (rc != RC_OK)
         return rc;
 
     ensureCapacity(1, &fileCtrl);
     SM_PageHandle pageBuf = calloc(PAGE_SIZE, sizeof(char));
     if (keyType != DT_INT) {
         printf("Error: Only integer keys are allowed.\n");
         free(pageBuf);
         return RC_RM_UNKOWN_DATATYPE;
     }
     *((int *)pageBuf) = n;  /* store the node limit in page 0 */
     rc = writeCurrentBlock(&fileCtrl, pageBuf);
     free(pageBuf);
 
     closePageFile(&fileCtrl);
     return rc;
 }
 
 /*
  * openBtree:
  * Opens the B+ tree index file, initializes a buffer pool, and reads the node limit from page 0 (via page 1).
  */
 RC openBtree(BTreeHandle **tree, char *idxId) {
     printf("Opening the B+ tree index: %s\n", idxId);
     CoreIndex *cindex = (CoreIndex *)malloc(sizeof(CoreIndex));
     if (!cindex)
         return RC_MEMORY_ALLOCATION_ERROR;
 
     cindex->poolRef   = MAKE_POOL();
     cindex->pageRef   = MAKE_PAGE_HANDLE();
     cindex->keysTotal = 0;
     cindex->topNode   = 0;
 
     /* Setup buffer pool for up to 10 pages with FIFO replacement. */
     initBufferPool(cindex->poolRef, idxId, 10, RS_FIFO, NULL);
 
     /* Pin page 1 to read the node limit from page 0 */
     pinPage(cindex->poolRef, cindex->pageRef, 1);
     cindex->nodeLimit = *((int *)cindex->pageRef->data);
     printf("Index config: nodeLimit = %d\n", cindex->nodeLimit);
     unpinPage(cindex->poolRef, cindex->pageRef);
 
     BTreeHandle *bh = (BTreeHandle *)malloc(sizeof(BTreeHandle));
     if (!bh)
         return RC_MEMORY_ALLOCATION_ERROR;
     bh->keyType = DT_INT;
     bh->idxId   = strdup(idxId);
     bh->mgmtData= cindex;
     *tree       = bh;
     return RC_OK;
 }
 
 /*
  * closeBtree:
  * Flushes changes, shuts down the buffer pool, frees memory, and resets global counters.
  */
 RC closeBtree(BTreeHandle *tree) {
     printf("Closing the B+ tree index.\n");
     gHighestPage = 0;
     gScanIndex   = 0;
 
     CoreIndex *cindex = (CoreIndex *)tree->mgmtData;
     shutdownBufferPool(cindex->poolRef);
     free(cindex->pageRef);
     free(cindex->poolRef);
     free(cindex);
     free(tree->idxId);
     free(tree);
     return RC_OK;
 }
 
 /*
  * deleteBtree:
  * Removes the index file from disk.
  */
 RC deleteBtree(char *idxId) {
     printf("Deleting B+ tree file: %s\n", idxId);
     return (remove(idxId) != 0) ? RC_FILE_NOT_FOUND : RC_OK;
 }
 
 /*
  * getNumNodes:
  * Returns how many node pages have been allocated (gHighestPage is zero-based).
  */
 RC getNumNodes(BTreeHandle *tree, int *result) {
     *result = gHighestPage + 1;
     return RC_OK;
 }
 
 /*
  * getNumEntries:
  * Returns how many keys exist in the index.
  */
 RC getNumEntries(BTreeHandle *tree, int *result) {
     CoreIndex *cindex = (CoreIndex *)tree->mgmtData;
     *result = cindex->keysTotal;
     return RC_OK;
 }
 
 /*
  * getKeyType:
  * Returns the type of keys (DT_INT).
  */
 RC getKeyType(BTreeHandle *tree, DataType *result) {
     *result = DT_INT;
     return RC_OK;
 }
 
 /* ====================== INDEX ACCESS FUNCTIONS ====================== */
 
 /*
  * findKey:
  * Looks for a key among pages 1..gHighestPage. Each node page starts with a bool flag, then the NodeInPage data.
  * If the key matches leftKey or rightKey in a node, returns the corresponding RID.
  */
 RC findKey(BTreeHandle *tree, Value *key, RID *result) {
     int neededVal = key->v.intV;
     CoreIndex *cindex = (CoreIndex *)tree->mgmtData;
     for (int pg = 1; pg <= gHighestPage; pg++) {
         pinPage(cindex->poolRef, cindex->pageRef, pg);
         NodeInPage *nodeObj = (NodeInPage *)((char *)cindex->pageRef->data + sizeof(bool));
         if ((nodeObj->leftKey == neededVal) || (nodeObj->rightKey == neededVal)) {
             *result = (neededVal == nodeObj->leftKey) ? nodeObj->leftSlot : nodeObj->rightSlot;
             unpinPage(cindex->poolRef, cindex->pageRef);
             return RC_OK;
         }
         unpinPage(cindex->poolRef, cindex->pageRef);
     }
     return RC_IM_KEY_NOT_FOUND;
 }
 
 /*
  * insertKey:
  * Appends a key-RID pair to the index. If no nodes exist (gHighestPage == 0), allocate page 1.
  * If the last node is full (bool flag == true), allocate a fresh node page; else place the key in the second slot.
  */
 RC insertKey(BTreeHandle *tree, Value *key, RID rid) {
     CoreIndex *cindex = (CoreIndex *)tree->mgmtData;
     NodeInPage *nodeObj;
 
     if (gHighestPage == 0) {
         gHighestPage     = 1;
         cindex->topNode  = 1;
         pinPage(cindex->poolRef, cindex->pageRef, gHighestPage);
         *((bool *)cindex->pageRef->data) = false;  /* only one key is used */
         nodeObj = (NodeInPage *)((char *)cindex->pageRef->data + sizeof(bool));
         nodeObj->parentIdx  = -1;
         nodeObj->nodeLeafBit= true;
         nodeObj->leftSlot   = rid;
         nodeObj->leftKey    = key->v.intV;
         nodeObj->rightSlot  = RID_NONE;
         nodeObj->rightKey   = -1;
         nodeObj->chainLink  = RID_NONE;
         unpinPage(cindex->poolRef, cindex->pageRef);
     } else {
         pinPage(cindex->poolRef, cindex->pageRef, gHighestPage);
         bool nodeFull = *((bool *)cindex->pageRef->data);
         if (nodeFull) {
             /* The last node page is full; allocate a new node page. */
             gHighestPage++;
             unpinPage(cindex->poolRef, cindex->pageRef);
             pinPage(cindex->poolRef, cindex->pageRef, gHighestPage);
             *((bool *)cindex->pageRef->data) = false;
             nodeObj = (NodeInPage *)((char *)cindex->pageRef->data + sizeof(bool));
             nodeObj->parentIdx   = -1;
             nodeObj->nodeLeafBit = true;
             nodeObj->leftSlot    = rid;
             nodeObj->leftKey     = key->v.intV;
             nodeObj->rightSlot   = RID_NONE;
             nodeObj->rightKey    = -1;
             nodeObj->chainLink   = RID_NONE;
             unpinPage(cindex->poolRef, cindex->pageRef);
         } else {
             /* Place the new key in the second slot of the existing last node. */
             nodeObj = (NodeInPage *)((char *)cindex->pageRef->data + sizeof(bool));
             nodeObj->rightSlot = rid;
             nodeObj->rightKey  = key->v.intV;
             *((bool *)cindex->pageRef->data) = true;
             unpinPage(cindex->poolRef, cindex->pageRef);
         }
     }
     cindex->keysTotal++;
     return RC_OK;
 }
 
 /*
  * deleteKey:
  * Searches for the given key among pages 1..gHighestPage. If found in the last page, remove or shift keys.
  * Otherwise, "borrow" a key from the last page to overwrite the key being deleted.
  */
 RC deleteKey(BTreeHandle *tree, Value *key) {
     int removingVal = key->v.intV;
     CoreIndex *cindex = (CoreIndex *)tree->mgmtData;
     bool located = false;
     int foundPg  = 0;
     int whichKey = 0;  /* 1 => leftKey, 2 => rightKey */
     NodeInPage *nodeObj;
 
     for (int pg = 1; pg <= gHighestPage && !located; pg++) {
         pinPage(cindex->poolRef, cindex->pageRef, pg);
         nodeObj = (NodeInPage *)((char *)cindex->pageRef->data + sizeof(bool));
         if (nodeObj->leftKey == removingVal)  { located = true; whichKey = 1; foundPg = pg; }
         else if (nodeObj->rightKey == removingVal) { located = true; whichKey = 2; foundPg = pg; }
         unpinPage(cindex->poolRef, cindex->pageRef);
     }
     if (!located)
         return RC_IM_KEY_NOT_FOUND;
 
     if (foundPg == gHighestPage) {
         /* If the key is in the last node page, remove or shift in place. */
         pinPage(cindex->poolRef, cindex->pageRef, gHighestPage);
         nodeObj = (NodeInPage *)((char *)cindex->pageRef->data + sizeof(bool));
         bool wasFull = *((bool *)cindex->pageRef->data);
         if (whichKey == 2) {
             nodeObj->rightSlot = RID_NONE;
             nodeObj->rightKey  = -1;
             *((bool *)cindex->pageRef->data) = false;
         } else {
             /* whichKey == 1 */
             if (wasFull) {
                 nodeObj->leftSlot = nodeObj->rightSlot;
                 nodeObj->leftKey  = nodeObj->rightKey;
                 nodeObj->rightSlot= RID_NONE;
                 nodeObj->rightKey = -1;
                 *((bool *)cindex->pageRef->data) = false;
             } else {
                 nodeObj->leftSlot = RID_NONE;
                 nodeObj->leftKey  = -1;
                 gHighestPage--;
             }
         }
         unpinPage(cindex->poolRef, cindex->pageRef);
     } else {
         /* Borrow a key from the last node to replace the removed key. */
         pinPage(cindex->poolRef, cindex->pageRef, gHighestPage);
         nodeObj = (NodeInPage *)((char *)cindex->pageRef->data + sizeof(bool));
         RID borrowSlot;
         int borrowVal;
         bool lastFull = *((bool *)cindex->pageRef->data);
         if (lastFull) {
             borrowSlot      = nodeObj->rightSlot;
             borrowVal       = nodeObj->rightKey;
             nodeObj->rightSlot = RID_NONE;
             nodeObj->rightKey  = -1;
             *((bool *)cindex->pageRef->data) = false;
         } else {
             borrowSlot      = nodeObj->leftSlot;
             borrowVal       = nodeObj->leftKey;
             nodeObj->leftSlot= RID_NONE;
             nodeObj->leftKey = -1;
             gHighestPage--;
         }
         unpinPage(cindex->poolRef, cindex->pageRef);
 
         pinPage(cindex->poolRef, cindex->pageRef, foundPg);
         nodeObj = (NodeInPage *)((char *)cindex->pageRef->data + sizeof(bool));
         if (whichKey == 1) {
             nodeObj->leftSlot = borrowSlot;
             nodeObj->leftKey  = borrowVal;
         } else {
             nodeObj->rightSlot= borrowSlot;
             nodeObj->rightKey = borrowVal;
         }
         unpinPage(cindex->poolRef, cindex->pageRef);
     }
     cindex->keysTotal--;
     return RC_OK;
 }
 
 /* ====================== TREE SCAN FUNCTIONS ====================== */
 
 /*
  * openTreeScan:
  * Collects all keys from pages 1..gHighestPage into an array, sorts them, and stores them
  * in the scan handle so that nextEntry can retrieve them in ascending order.
  */
 RC openTreeScan(BTreeHandle *tree, BT_ScanHandle **handle) {
     printf("Initiating a tree scan...\n");
     CoreIndex *cindex = (CoreIndex *)tree->mgmtData;
     int *gathered = (int *)malloc(sizeof(int) * cindex->keysTotal);
     int indexPos = 0;
     for (int pg = 1; pg <= gHighestPage; pg++) {
         pinPage(cindex->poolRef, cindex->pageRef, pg);
         NodeInPage *nd = (NodeInPage *)((char *)cindex->pageRef->data + sizeof(bool));
         if (nd->leftKey != -1)
             gathered[indexPos++] = nd->leftKey;
         if (*((bool *)cindex->pageRef->data) && (nd->rightKey != -1))
             gathered[indexPos++] = nd->rightKey;
         unpinPage(cindex->poolRef, cindex->pageRef);
     }
     /* Sort using a simple selection sort */
     for (int i = 0; i < cindex->keysTotal - 1; i++) {
         int minIdx = i;
         for (int j = i + 1; j < cindex->keysTotal; j++) {
             if (gathered[j] < gathered[minIdx])
                 minIdx = j;
         }
         int temp = gathered[minIdx];
         gathered[minIdx] = gathered[i];
         gathered[i] = temp;
     }
     BT_ScanHandle *scanH = (BT_ScanHandle *)malloc(sizeof(BT_ScanHandle));
     scanH->tree = tree;
     scanH->mgmtData = gathered;
     gScanIndex = 0;
     *handle = scanH;
     return RC_OK;
 }
 
 /*
  * nextEntry:
  * Reads the next key from the sorted list in mgmtData, uses findKey to locate its RID,
  * and returns that RID as the next entry.
  */
 RC nextEntry(BT_ScanHandle *handle, RID *result) {
     CoreIndex *cindex = (CoreIndex *)handle->tree->mgmtData;
     int *sortedKeys = (int *)handle->mgmtData;
     if (gScanIndex >= cindex->keysTotal)
         return RC_IM_NO_MORE_ENTRIES;
     Value valNext;
     valNext.dt = DT_INT;
     valNext.v.intV = sortedKeys[gScanIndex];
     RID foundHere;
     RC rc = findKey(handle->tree, &valNext, &foundHere);
     if (rc != RC_OK)
         return rc;
     *result = foundHere;
     gScanIndex++;
     return RC_OK;
 }
 
 /*
  * closeTreeScan:
  * Deallocates the sorted key array and the scan handle, and resets the scan index.
  */
 RC closeTreeScan(BT_ScanHandle *handle) {
     printf("Concluding the tree scan...\n");
     free(handle->mgmtData);
     free(handle);
     gScanIndex = 0;
     return RC_OK;
 }
 
 /*
  * printTree:
  * For debugging, returns the file name (idxId) for this B+ tree.
  */
 char* printTree(BTreeHandle *tree) {
     return tree->idxId;
 }