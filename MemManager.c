#include "MemManager.h"

/* TODO:
* 1. read "sys_config.txt" and get system info
* 2. initialize environment:
*       TLB
*       the page table of each process
*       physical memory
*       swap space
*       replacement list
*       free frame list
*       statusType of each process
* 3. read "trace.txt" and handle each memory access
*       different policy of TLB, page replacement, and allocation
*       TLB hit/miss
*       page hit/fault
* 4. store the situation of each memory access in "trace_output.txt"
* 5. store the analysis of each process in "analysis.txt"
*       Effective access time 
*       Page fault rate
* 6. Write a document to show the pros and cons of each policy
*/

//---------------------------------------
/*TLB operation*/
void flushTLB() //
{
    for (int i = 0; i < 32; i++)
        tlbEntry[i].valid = true;
}
int16_t TLBLookup(uint16_t vpn)
{
    for (int i = 0; i < 32; i++)
    {
        if (!tlbEntry[i].valid && tlbEntry[i].vpn == vpn)
        {
            if (tlbPolicy == LRU)
                tlbLRU[i] = tlbCounter++;
            return tlbEntry[i].pfn;
        }
    }
    return -1;
}
int kickTLBEntry()
{
    if (tlbPolicy == RANDOM)
    {
        return rand()%32;
    }
    else
    {
        unsigned int min=tlbLRU[0];
        int victimTLB=0;
        for (int i=1; i<32; i++)
        {
            if (min > tlbLRU[i])
            {
                victimTLB = i;
                min = tlbLRU[i];
            }
        }
        //kick the victimTLB, and reset the used order of new tlbEntry
        tlbLRU[victimTLB] = 0;
        return victimTLB;
    }
}
void fillTLB(uint16_t vpn, int pfn)
{
    int i;
    for (i=0; i<32; i++)
    {
        if (tlbEntry[i].valid)
            break;
    }
    if (i == 32) // there is no empty slot for TLB entries
        i = kickTLBEntry();
    tlbEntry[i].vpn = vpn;
    tlbEntry[i].pfn = pfn;
    tlbEntry[i].valid = false;
}
//---------------------------------------
/*PTE operation*/
int16_t pageTableLookup(int PTBR, uint16_t vpn)
{
    if (pageTable[PTBR + vpn].bitField.bits.present)
        return pageTable[PTBR + vpn].pfn_dbi;
    else
        return -1;
}
void fillPTE(int PTBR, uint16_t vpn, int pfn)
{
    ReplaceListType *newNode, *curNode;
    if (allocPolicy == LOCAL)
        curNode = curLocalReplaceNode[PTBR_TO_INDEX].head;
    else
        curNode = curReplaceNode;
    
    if (curNode->next == NULL && curNode->prev == NULL) //only one victim node in the list 
    {
        curNode->vpn = vpn;
        curNode->proc = PTBR_TO_INDEX;
        curNode->next = curNode->prev = curNode;
    }
    else //newNode is the last victim (prev of curNode)
    {
        newNode = malloc(sizeof(ReplaceListType));
        newNode->vpn = vpn;
        newNode->prev = curNode->prev;
        newNode->next = curNode;
        newNode->proc = PTBR_TO_INDEX;
        curNode->prev->next = newNode;
        curNode->prev = newNode;
    }

    //allocate frame, update pageTable
    phyMem[pfn].frameValid = false;
    pageTable[PTBR + vpn].bitField.bits.present = 1;
    pageTable[PTBR + vpn].bitField.bits.reference = 0;
    pageTable[PTBR + vpn].pfn_dbi = pfn;
}
int kickFrame(int PTBR, uint16_t refPage)
{
    ReplaceListType *curNode;
    int16_t pfn;
    int dbi;

    //victim Node(vpn) in local/global replacement list
    if (allocPolicy == LOCAL)
        curNode = curLocalReplaceNode[PTBR_TO_INDEX].head; //head of local replacement list of current process
    else
        curNode = curReplaceNode;
    
    if (pagePolicy == CLOCK)
    {
        while (pageTable[(curNode->proc * virPage) + curNode->vpn].bitField.bits.reference)
        {
            pageTable[(curNode->proc * virPage) + curNode->vpn].bitField.bits.reference = 0;
            curNode = curNode->next;
        }
    }

    // 检查 curNode 是否为 NULL
    if (curNode == NULL) {
        fprintf(stderr, "Error: curNode is NULL in kickFrame.\n");
        exit(EXIT_FAILURE);
        return -1;
    }

    // 边界检查，找要 paged out 的 pfn
    if (curNode->proc >= 0 && curNode->proc < numProc && curNode->vpn >= 0 && curNode->vpn < virPage) {
        pfn = pageTable[(curNode->proc * virPage) + curNode->vpn].pfn_dbi;
    } else {
        fprintf(stderr, "Error: Invalid proc or vpn in kickFrame.\n");
        exit(EXIT_FAILURE);
        return -1;
    }

    // kick pfn to dbi
    for (dbi = 0; dbi < numProc * virPage; dbi++)
    {
        if (swapSpace[dbi].frameValid)
            break;
    }
    if (dbi < numProc * virPage) {
        swapSpace[dbi].frameValid = false;
    } else {
        fprintf(stderr, "Error: No valid frame found in swapSpace.\n");
        return -1;
    }

    fprintf(ftraceOutput, "Process %c, TLB Miss, Page Fault, (pfn)%d, Evict (vpn)%d of Process %c to (dbi)%d, (vpn)%d << (dbi)%d\n",
            PTBR_TO_PROCESS, pfn, curNode->vpn, (curNode->proc + C_CHAR_OFFSET), dbi, refPage, pageTable[PTBR + refPage].pfn_dbi); //curNode 是被换出的页面，PTBR + refPage 是要載入的頁面

    // 更新 pageTable
    pageTable[(curNode->proc * virPage) + curNode->vpn].bitField.byte = 0;
    pageTable[(curNode->proc * virPage) + curNode->vpn].pfn_dbi = dbi;

    // replacement list 操作，先检查 prev 和 next
    if (curNode->prev != NULL) {
        curNode->prev->next = curNode->next;
    }
    if (curNode->next != NULL) {
        curNode->next->prev = curNode->prev;
    }

    // 更新 victim curNode
    if (allocPolicy == LOCAL)
        curLocalReplaceNode[PTBR_TO_INDEX].head = curNode->next;
    else
        curReplaceNode = curNode->next;
    free(curNode);
    
    return pfn; //physical memory index(0~63)，不是 swapSpace index(0~255)：pageTable[(curNode->proc * virPage) + curNode->vpn].pfn_dbi
}
//---------------------------------------
/*switch to the PTBR of process*/
void switchProcess(int *PTBR, int *perProcPTBR, char process)
{
    flushTLB();
    int i = process - C_CHAR_OFFSET;
    *PTBR = perProcPTBR[i];
}
/*search free frame*/
int freeFrameManager()
{
    for (int i=0; i<phyPage; i++)
    {
        if (phyMem[i].frameValid)
            return i;
    }
    // no free frame
    return -1;
}
/*allocate frame, update PTE and TLB
 *apply TLB replacement policy, page replacement policy, and frame allocation policy
 */
int16_t pageFaultHandler(int PTBR, uint16_t refPage)
{
    int freePfn = -1;
    freePfn = freeFrameManager();
    if (freePfn == -1) // there is no free frames
        freePfn = kickFrame(PTBR, refPage);
    else //有 free frame 可直接分配，不用 evict victim page，就也不用從 swapSpace 載入 page
        fprintf(ftraceOutput, "Process %c, TLB Miss, Page Fault, (pfn)%d, Evict (vpn)-1 of Process %c to (dbi)-1, (vpn)%d << (dbi)-1\n",
                PTBR_TO_PROCESS, freePfn, PTBR_TO_PROCESS, refPage);

    fillPTE(PTBR, refPage, freePfn);
    fillTLB(refPage, freePfn);

    return freePfn;
}
/*search referenced page in page table. repeat until TLB hit*/
int16_t addrTrans(int PTBR, uint16_t refPage)
{
    int16_t pfn = -1;
    pfn = TLBLookup(refPage);
    stats[PTBR_TO_INDEX].refTlbCnt++;
    // TLB Hit
    if (pfn != -1)
    {
        stats[PTBR_TO_INDEX].tlbHitCnt++;
        fprintf(ftraceOutput, "Process %c, TLB Hit, %d=>%d\n",PTBR_TO_PROCESS, refPage, pfn);
        if (pagePolicy == CLOCK)
            pageTable[PTBR + refPage].bitField.bits.reference = 1;
        return pfn;
    }
    // TLB Miss
    else
    {
        pfn = pageTableLookup(PTBR, refPage);
        stats[PTBR_TO_INDEX].refPageCnt++;
        // Page hit
        if (pfn != -1)
        {
            fprintf(ftraceOutput, "Process %c, TLB Miss, Page Hit, %d=>%d\n",PTBR_TO_PROCESS, refPage, pfn);
            if (pagePolicy == CLOCK)
                pageTable[PTBR + refPage].bitField.bits.reference = 1;
            //update TLB & search referenced page again
            fillTLB(refPage, pfn);
            addrTrans(PTBR, refPage);
        }
        // page fault
        else
        {
            stats[PTBR_TO_INDEX].pageFaultCnt++;
            pageFaultHandler(PTBR, refPage);
            addrTrans(PTBR, refPage);
        }
    }
    return pfn;
}
//---------------------------------------
bool getSysInfo(TLBPolicy *tlbPolicy, PagePolicy *pagePolicy, AllocPolicy *allocPolicy,
                int *numProc, int *virPage, int *phyPage)
{
    FILE *fsys;
    char *contents = NULL;
    size_t len = 0;

    fsys = fopen("sys_config.txt", "r");
    if (fsys == NULL)
    {
        printf("Error: sys_config.txt not found\n");
        return false;
    }
    for (int i = 0; i < 6; i++)
    {
        getline(&contents, &len, fsys);
        if (i != 5)
            // remove '\n' at the end of the line
            contents[strlen(contents) - 1] = '\0';
        switch (i)
        {
        case 0:
            if (!strcmp(contents, "TLB Replacement Policy: LRU")) //strcmp return 0 if two strings are equal
                *tlbPolicy = LRU;
            else if (!strcmp(contents, "TLB Replacement Policy: RANDOM"))
                *tlbPolicy = RANDOM;
            else
                return false;
            break;
        case 1:
            if (!strcmp(contents, "Page Replacement Policy: FIFO"))
                *pagePolicy = FIFO;
            else if (!strcmp(contents, "Page Replacement Policy: CLOCK"))
                *pagePolicy = CLOCK;
            else
                return false;
            break;
        case 2:
            if (!strcmp(contents, "Frame Allocation Policy: LOCAL"))
                *allocPolicy = LOCAL;
            else if (!strcmp(contents, "Frame Allocation Policy: GLOBAL"))
                *allocPolicy = GLOBAL;
            else
                return false;
            break;
        case 3:
            *numProc = atoi(&contents[20]);
            if (*numProc < 1 || *numProc > 20)
                return false;
            break;
        case 4:
            *virPage = atoi(&contents[23]);
            // check if virPage is power of 2
            if (!(ceil(log2(*virPage)) == floor(log2(*virPage)))) 
                return false;
            else if (*virPage < 2 || *virPage > 2048)
                return false;
            break;
        case 5:
            *phyPage = atoi(&contents[26]);
            if (!(ceil(log2(*phyPage)) == floor(log2(*phyPage))))
                return false;
            else if (*virPage < *phyPage)
                return false;
            else if (*phyPage < 1 || *phyPage > 1024)
                return false;
            break;
        }
    }
    fclose(fsys);

    return true;
}
void analysis(){
    FILE *fanalysis;
    fanalysis = fopen("analysis.txt", "w+");
    
    float hitRatio, formula, pageFaultRate;
    for (int i=0; i<numProc; i++)
    {
        hitRatio = stats[i].tlbHitCnt/stats[i].refTlbCnt;
        formula = (hitRatio * 120) + (1-hitRatio) * 220;
        pageFaultRate = stats[i].pageFaultCnt/stats[i].refPageCnt;
        fprintf(fanalysis, "Process %c, Effective Access Time = %f\n", i+C_CHAR_OFFSET, formula); //C_CHAR_OFFSET is ASCII 'A'
        fprintf(fanalysis, "Page Fault Rate: %1.3f\n", pageFaultRate);
    }
    fclose(fanalysis);
}
void initialize(){
    // create page table, and set index for per process page table
    pageTable = malloc(numProc * virPage * sizeof(PageTableEntry)); //page table for all processes
    PerProcPTBR = malloc(numProc * sizeof(int)); //store the start index of page table for each process
    for (int i = 0; i < numProc; i++)
        PerProcPTBR[i] = i * virPage;
    for (int i = 0; i < numProc * virPage; i++)
    {
        pageTable[i].bitField.byte = 0;
        pageTable[i].pfn_dbi = -1; //demand paging 特性，初始为空
    }

    // create phyMem and swapSpace
    phyMem = malloc(phyPage * sizeof(PhyMem));
    swapSpace = malloc(numProc * virPage * sizeof(PhyMem)); //磁盘空间，用于儲存被换出的页面
    for (int i=0; i<phyPage; i++)
        phyMem[i].frameValid = true;
    for (int i=0; i<numProc * virPage; i++)
        swapSpace[i].frameValid = true;
    flushTLB();

    // init tlbLRU array
    if (tlbPolicy == LRU)
    {
        for (int i=0; i<32; i++)
            tlbLRU[i] = 0;
    }
    // create replacement list
    if (allocPolicy == LOCAL)
    {
        curLocalReplaceNode = malloc(numProc * sizeof(ReplaceListHeadType));
        for (int i=0; i<numProc; i++)
        {
            curLocalReplaceNode[i].head = malloc(sizeof(ReplaceListType));
            curLocalReplaceNode[i].head->next = curLocalReplaceNode[i].head->prev = NULL;
            curLocalReplaceNode[i].head->proc = i;
        }
    }
    else if (allocPolicy == GLOBAL)
    {
        curReplaceNode = malloc(sizeof(ReplaceListType));
        curReplaceNode->next = curReplaceNode->prev = NULL;
        // printf("global list process %d\n", curReplaceNode->proc);
        // printf("%d, %d\n", curReplaceNode->proc, curReplaceNode->vpn);
    }
    // create StatsType array: store the StatsType for each process
    stats = malloc(numProc * sizeof(StatsType));
    for (int i=0; i<numProc; i++)
        stats[i].pageFaultCnt = stats[i].refPageCnt = stats[i].refTlbCnt = stats[i].tlbHitCnt = 0;
}


int main()
{
    FILE *ftrace;
    int PTBR, proc = -1, lastProc = -1;
    char *contents = NULL;
    size_t len = 0;
    uint16_t refPage;

    //read "sys_config.txt" and get system info
    if (!getSysInfo(&tlbPolicy, &pagePolicy, &allocPolicy, &numProc, &virPage, &phyPage))
    {
        printf("abort: sys_config.txt occurs format errors\n");
        return 0;
    }

    //initialize environment
    initialize();

    //get reference information & handle each reference
    ftrace = fopen("trace.txt", "r");
    if (ftrace == NULL) {
        perror("Error opening trace.txt");
        exit(EXIT_FAILURE);
    }
    ftraceOutput = fopen("trace_output.txt", "w");
    if (ftraceOutput == NULL) {
        perror("Error opening trace_output.txt");
        exit(EXIT_FAILURE);
    }
    while (getline(&contents, &len, ftrace) != -1)
    {
        if (!(contents[strlen(contents) - 1] == ')')) // remove '\n' at the end of the line
            contents[strlen(contents) - 1] = '\0';
        proc = contents[10]; //stored in ASCII code
        refPage = atoi(&contents[12]);
        if (lastProc == -1)
            PTBR = PerProcPTBR[proc-C_CHAR_OFFSET];
        else if (lastProc != -1 && proc != lastProc)//reference process 不同時，更新 PTBR
            switchProcess(&PTBR, PerProcPTBR, proc); //int proc會被隱式轉換成 char
        // printf("%c, %d\n", PTBR_TO_PROCESS, refPage);
        addrTrans(PTBR, refPage);
        lastProc = proc;
    }
    fclose(ftrace);
    fclose(ftraceOutput);
    
    analysis();

    //Free dynamically allocated memory
    free(pageTable);
    free(PerProcPTBR);
    free(phyMem);
    free(swapSpace);
    free(stats);
    if (allocPolicy == LOCAL)
    {
        for (int i=0; i<numProc; i++)
            free(curLocalReplaceNode[i].head);
        free(curLocalReplaceNode);
    }
    else if (allocPolicy == GLOBAL)
        free(curReplaceNode);

    return 0;
}
