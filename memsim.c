#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <limits.h> 


typedef struct {
        int pageNo;
        int modified;
} page;

enum    repl { randomRpl, fifo, lru, clockRpl};

int     createMMU( int);
int     checkInMemory( int ) ;
int     allocateFrame( int ) ;
page    selectVictim( int, enum repl) ;

const   int pageoffset = 12;            /* Page size is fixed to 4 KB */
int     numFrames ;

typedef struct {
    int valid;
    int pageNo;
    int modified; 
    int referenced; 
    unsigned long lastUsed; 
} Frame; 

static Frame *frameTable = NULL; 
static unsigned long globalTime = 0;
static int fifoPtr = 0; 
static int clockHand = 0; 

/* Creates the page table structure to record memory allocation */
int     createMMU (int frames)
{
    if (frames <= 0){
        return -1; 
    }
    
    frameTable = (Frame *) malloc(sizeof(Frame) * frames); 

    if (!frameTable){
        return -1; 
    }

    numFrames = frames; 

    for (int i = 0; i < frames; i++){
        frameTable[i].valid = 0;
        frameTable[i].pageNo = -1; 
        frameTable[i].modified = 0;
        frameTable[i].referenced = 0; 
        frameTable[i].lastUsed = 0; 
    }

    globalTime = 0; 
    fifoPtr = 0; 
    clockHand = 0; 
    srand((unsigned) time(NULL)); 
    
    return 0;
}

/* Checks for residency: returns frame no or -1 if not found */
int     checkInMemory( int page_number)
{
        for (int i = 0; i < numFrames; i++){
            if (frameTable[i].valid && frameTable[i].pageNo == page_number){
                return i;
            }
        }
        return -1;
}

/* allocate page to the next free frame and record where it put it */
int     allocateFrame( int page_number)
{
        for (int i = 0; i < numFrames; i++){
            if (!frameTable[i].valid){
                frameTable[i].valid = 1;
                frameTable[i].pageNo = page_number;
                frameTable[i].modified = 0;
                frameTable[i].referenced = 1; 
                frameTable[i].lastUsed = ++globalTime; 
                return i; 
            }
        }
        return -1;
}

static void touch_frame_on_access(int frame_index, char rw)
{
    if (frame_index < 0 || frame_index >= numFrames){
        return;
    }

    frameTable[frame_index].referenced = 1;
    frameTable[frame_index].lastUsed = ++globalTime;

    if (rw == 'W'){
        frameTable[frame_index].modified = 1;
    }
}

/* Selects a victim for eviction/discard according to the replacement algorithm,  returns chosen frame_no  */
page    selectVictim(int page_number, enum repl  mode )
{
        page victim;
        int victimIndex = -1; 

        if (mode == randomRpl) {
            victimIndex = rand() % numFrames;
        }

        else if (mode == lru) {
            unsigned long minTime = ULLONG_MAX;
            for (int i = 0; i < numFrames; i++){
                if (frameTable[i].valid && frameTable[i].lastUsed < minTime){
                    minTime = frameTable[i].lastUsed; 
                    victimIndex = i; 
                }
            }
        }

        else if (mode == clockRpl){
            int tries = 0; 
            while (tries < 2 * numFrames){
                if (!frameTable[clockHand].valid) {
                    victimIndex = clockHand; 
                    clockHand = (clockHand + 1) % numFrames; 
                    break; 
                }
                if (frameTable[clockHand].referenced == 0){
                    victimIndex = clockHand; 
                    clockHand = (clockHand + 1) % numFrames; 
                    break; 
                }

                else {
                    frameTable[clockHand].referenced = 0;
                    clockHand = (clockHand + 1) % numFrames; 
                }
                tries++; 
            }
            if (victimIndex == -1){
                victimIndex = clockHand; 
                clockHand = (clockHand + 1) % numFrames; 
            }
        }

        else{
            victimIndex = 0; 
        }

        // to do 
        victim.pageNo = frameTable[victimIndex].pageNo;
        victim.modified = frameTable[victimIndex].modified; 
        
        frameTable[victimIndex].pageNo = page_number;
        frameTable[victimIndex].valid = 1; 
        frameTable[victimIndex].modified = 0; 
        frameTable[victimIndex].referenced = 1; 
        frameTable[victimIndex].lastUsed = ++globalTime; 

        return victim;
}

		
int main(int argc, char *argv[])
{
    char *tracename;
    int page_number, frame_no, done;
    int do_line, i;
    int no_events, disk_writes, disk_reads;
    int debugmode;
    enum repl replace;
    int allocated = 0;
    unsigned address;
    char rw;
    page Pvictim;
    FILE *trace;

    if (argc < 5) {
        printf("Usage: ./memsim inputfile numberframes replacementmode debugmode \n");
        exit(-1);
    } else {
        tracename = argv[1];
        trace = fopen(tracename, "r");
        if (trace == NULL) {
            printf("Cannot open trace file %s \n", tracename);
            exit(-1);
        }
        numFrames = atoi(argv[2]);
        if (numFrames < 1) {
            printf("Frame number must be at least 1\n");
            exit(-1);
        }
        if (strcmp(argv[3], "lru") == 0)
            replace = lru;
        else if (strcmp(argv[3], "rand") == 0)
            replace = randomRpl;
        else if (strcmp(argv[3], "clock") == 0)
            replace = clockRpl;
        else if (strcmp(argv[3], "fifo") == 0)
            replace = fifo;
        else {
            printf("Replacement algorithm must be rand/fifo/lru/clock  \n");
            exit(-1);
        }

        if (strcmp(argv[4], "quiet") == 0)
            debugmode = 0;
        else if (strcmp(argv[4], "debug") == 0)
            debugmode = 1;
        else {
            printf("Replacement algorithm must be quiet/debug  \n");
            exit(-1);
        }
    }

    done = createMMU(numFrames);
    if (done == -1) {
        printf("Cannot create MMU");
        exit(-1);
    }

    no_events = 0;
    disk_writes = 0;
    disk_reads = 0;

    do_line = fscanf(trace, "%x %c", &address, &rw);
    while (do_line == 2) {
        page_number = address >> pageoffset;
        frame_no = checkInMemory(page_number); /* ask for physical address */

        if (frame_no == -1) {
            disk_reads++; /* Page fault, need to load it into memory */
            if (debugmode)
                printf("Page fault %8d \n", page_number);
            if (allocated < numFrames) { /* allocate it to an empty frame */
                frame_no = allocateFrame(page_number);
                allocated++;
            } else {
                Pvictim = selectVictim(page_number, replace); /* returns victim page info */
                frame_no = checkInMemory(page_number); /* find out the frame the new page is in */
                if (Pvictim.modified) { /* if victim was dirty, increment disk_writes */
                    disk_writes++;
                    if (debugmode) printf("Disk write %8d \n", Pvictim.pageNo);
                } else {
                    if (debugmode) printf("Discard    %8d \n", Pvictim.pageNo);
                }
            }
        }

        /* Now we have the page in memory at frame_no; mark accessed/modified as needed */
        if (frame_no != -1) {
            if (rw == 'R') {
                touch_frame_on_access(frame_no, 'R');
                if (debugmode) printf("reading    %8d \n", page_number);
            } else if (rw == 'W') {
                touch_frame_on_access(frame_no, 'W'); /* sets modified too */
                if (debugmode) printf("writting   %8d \n", page_number);
            } else {
                printf("Badly formatted file. Error on line %d\n", no_events + 1);
                exit(-1);
            }
        } else {
            /* Should not happen: ensure robustness */
            printf("Internal error: no frame found for page %d after fault handling\n", page_number);
            exit(-1);
        }

        no_events++;
        do_line = fscanf(trace, "%x %c", &address, &rw);
    }

    printf("total memory frames:  %d\n", numFrames);
    printf("events in trace:      %d\n", no_events);
    printf("total disk reads:     %d\n", disk_reads);
    printf("total disk writes:    %d\n", disk_writes);
    printf("page fault rate:      %.4f\n", (float) disk_reads / no_events);

    /* cleanup */
    if (frameTable) free(frameTable);
    if (trace) fclose(trace);

    return 0;
}