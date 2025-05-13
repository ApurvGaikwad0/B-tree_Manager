## B⁺ Tree Index Manager Implementation

### Overview
This project implements a simplified B⁺ tree index manager integrated with the record manager. The index manager supports the following operations:
•⁠  ⁠Creation of a new index file.
•⁠  ⁠Insertion, deletion, and search of keys.
•⁠  ⁠Scanning of keys in sorted order.

The design uses a flat–page approach where each page (node) can hold up to 2 keys (order = 2). A boolean flag at the beginning of each page indicates whether the node is full. When a node is full and a new key is inserted, a new page is allocated. Global variables track the last allocated page and the scan position.

### File Structure
The project directory is organized as follows:

```bash
Assignment-4/

├── .test_assign4_1.c.swp
├── btree_mgr.c
├── btree_mgr.h
├── buffer_mgr.c
├── buffer_mgr.h
├── buffer_mgr_stat.c
├── buffer_mgr_stat.h
├── Contribution Table-4 G04.docx
├── dberror.c
├── dberror.h
├── dt.h
├── expr.c
├── expr.h
├── Makefile
├── README.md
├── record_mgr.c
├── record_mgr.h
├── rm_serializer.c
├── storage_mgr.c
├── storage_mgr.h
├── tables.h
├── test_assign4_1.c
├── test_expr.c
├── test_helper.h
```



### Implementation Details

#### B⁺ Tree Manager
•⁠  ⁠*Node Storage:*  
  Each page starts with a boolean flag (⁠ sizeof(bool) ⁠ bytes) that indicates whether the node is full. Immediately following the flag is a ⁠ BTreeNode ⁠ structure that, for this implementation (order = 2), stores up to 2 keys:
  - *parentPage (int):* Parent page number (or ⁠ -1 ⁠ if none).
  - *isLeaf (bool):* Indicates if the node is a leaf.
  - *leftEntry (RID):* RID for the first key.
  - *key1 (int):* First key value.
  - *midEntry (RID):* RID for the second key.
  - *key2 (int):* Second key value (set to ⁠ -1 ⁠ if not present).
  - *rightEntry (RID):* Unused; set to ⁠ INIT_RID ⁠.

•⁠  ⁠*Tree Management Structure:*  
  The ⁠ BTreeInfo ⁠ structure serves as the control block for the index and stores:
  - A pointer to the buffer pool.
  - A reusable page handle for pin/unpin operations.
  - The root page number (set to 1 after the first insertion).
  - The total number of keys inserted.
  - The maximum number of keys per node (order), which is read from the header (page 0).

•⁠  ⁠*Global Counters:*  
  - ⁠ currentLastPage ⁠: Tracks the highest page number allocated.
  - ⁠ currentScanIndex ⁠: Tracks the current index during tree scans.

•⁠  ⁠*Operations Implemented:*
  - *createBtree:*  
    Creates a new index file and writes the tree order to the header (page 0). Only integer keys (⁠ DT_INT ⁠) are supported.
    
  - *openBtree:*  
    Initializes the B⁺ tree management structure (allocates a buffer pool and page handle), and reads the order from the header.
    
  - *insertKey:*  
    Implements a flat–page strategy:
    - For the first key, page 1 is allocated and the key is stored.
    - For subsequent keys, the last allocated page is used; if the node is full (flag is true), a new page is allocated. Otherwise, the key is inserted into the remaining slot and the node is marked as full.
    
  - *findKey:*  
    Sequentially scans active pages (from page 1 up to ⁠ currentLastPage ⁠) to locate a key and returns the associated RID.
    
  - *deleteKey:*  
    Searches for a key in the active pages, adjusts node values, and updates the global page counter.
    
  - *openTreeScan / nextEntry / closeTreeScan:*  
    - *openTreeScan:* Collects all key values from active pages and sorts them in ascending order.
    - *nextEntry:* Returns the RID associated with the next key in the sorted order.
    - *closeTreeScan:* Releases memory allocated for the scan.
    
  - *getNumNodes:*  
    Returns the number of active nodes, calculated as ⁠ currentLastPage + 1 ⁠.
    
  - *getNumEntries:*  
    Returns the total number of keys inserted.

### How to Build and Run

#### Build and Execution Commands
1. Cleaning the older artifacts:
   You need to execute the following command in the terminal.
   ```
   make clean
   ```

2. Build the Project:
   ```
   make
   ```

3. Run the Expression Tests:
    ```
   ./test_expr
   ```

4. Run the B⁺ Tree Index Tests:
    ```
   ./test_assign4
   ```

5. Clean the Build Files:
     ```
   make clean
   ```

### Authors
- #### Apurv Gaikwad (A20569178)
- #### Nishant Dalvi (A20556507)
- #### Satyam Borade (A20586631)
