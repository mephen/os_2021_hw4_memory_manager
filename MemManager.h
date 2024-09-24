#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <math.h>
//PTBR：page-table base register，紀錄某個 process 的 page table 的起始位置
#define C_CHAR_OFFSET 65 //ASCII 'A'
#define PTBR_TO_INDEX (PTBR/virPage) //PTBR 對應到的 process index
#define PTBR_TO_PROCESS ((PTBR_TO_INDEX) + 65) //PTBR 對應到的 process name
typedef enum
{
    LRU,
    RANDOM
} TLBPolicy;

typedef enum
{
    FIFO,
    CLOCK
} PagePolicy;

typedef enum
{
    LOCAL,
    GLOBAL
} AllocPolicy;

typedef struct
{
    bool valid;
    uint16_t vpn;
    uint16_t pfn;
} TLBEntry;
TLBEntry tlbEntry[32];

typedef struct
{
    union
    {
        uint8_t byte; //用於快速初始化 bitField (因為跟 bits 共享記憶體位置)
        struct
        {
            uint8_t reference : 1; //占用 8 bits 中的 1 bit，在最低位(bit 0)
            uint8_t present : 1; //在 8 bits 中的第2位(bit 1)
        } bits;
    } bitField;
    int16_t pfn_dbi;//physical frame number / disk block number; When a page is page-out to disk block K, the pfn_dbi will be set as K
} PageTableEntry;

typedef struct ReplaceListType
{
    uint8_t proc;
    uint16_t vpn;
    struct ReplaceListType *prev;
    struct ReplaceListType *next;
} ReplaceListType;
ReplaceListType *curReplaceNode;

typedef struct
{
    struct ReplaceListType *head;
} ReplaceListHeadType;
ReplaceListHeadType *curLocalReplaceNode; //陣列：儲存各個 process 的 ReplaceList

typedef struct
{
    bool frameValid;
} PhyMem;
PhyMem *phyMem;
PhyMem *swapSpace; //這裡将内存页大小和磁盘块大小设置为相同，簡化分页（paging）和换页（swapping）的过程

typedef struct
{
    float tlbHitCnt;
    float refTlbCnt;
    float pageFaultCnt;
    float refPageCnt;
} StatsType; //統計資料
StatsType *stats;

PageTableEntry *pageTable;
//system info
AllocPolicy allocPolicy;
TLBPolicy tlbPolicy;
PagePolicy pagePolicy;
int phyPage, virPage, numProc;

uint32_t tlbCounter=0;
uint32_t tlbLRU[32]; //record the used order of tlbEntry (by tlbCounter)
FILE *ftraceOutput;
void flushTLB();
int16_t TLBLookup(uint16_t vpn);
int kickTLBEntry();
void fillTLB(uint16_t vpn, int pfn);
bool getSysInfo(TLBPolicy *tlbPolicy, PagePolicy *pagePolicy, AllocPolicy *allocPolicy,
                int *numProc, int *virPage, int *phyPage);
void switchProcess(int *PTBR, int *perProcPTBR, char process);
int16_t pageTableLookup(int PTBR, uint16_t vpn);
void fillPTE(int PTBR, uint16_t vpn, int pfn);
int kickPage(int PTBR, uint16_t refPage);
int freeFrameManager();
int16_t pageFaultHandler(int PTBR, uint16_t refPage);
int16_t addrTrans(int PTBR, uint16_t refPage);
