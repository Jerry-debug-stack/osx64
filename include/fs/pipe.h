#ifndef OS_PIPE_H
#define OS_PIPE_H

#include <stdint.h>
#include "lib/wait_queue.h"

#define PIPE_BUFSIZE     (4096 * 4)

typedef struct pipe
{
    bool single;
    spinlock_t lock;
    uint32_t buf_head;
    uint32_t buf_tail;
    wait_queue_t read_wq;
    wait_queue_t write_wq;
    char buf[PIPE_BUFSIZE];
} pipe_t;


#endif
