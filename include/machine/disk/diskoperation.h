#ifndef OS_DISK_OPERATION_H
#define OS_DISK_OPERATION_H

typedef struct DiskOperation {
    unsigned char Command;
    unsigned int SectorNum;
    unsigned long LBA;
    unsigned long* Addr;
    unsigned int State;
} DISK_OPERATION;

#define DISK_OPERATION_NONE (DISK_OPERATION*)0

#define DISK_OPERATION_STATE_WAITING 0
#define DISK_OPERATION_STATE_HADLING 1
#define DISK_OPERATION_STATE_ERROR 2
#define DISK_OPERATION_STATE_FINISHED 3

#endif