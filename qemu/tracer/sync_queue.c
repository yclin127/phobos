#include "tracer/sync_queue.h"

typedef struct {
    volatile uint32_t head[CURSOR_COUNT];
    volatile uint32_t tail[CURSOR_COUNT];
    batch_t *entry;
    void    *pool;
    QemuThread thread;
} fifo_t;

static fifo_t fifo;

void sync_queue_init(void)
{
    int i;
    
    fifo.pool = qemu_malloc(BATCH_COUNT*BATCH_SIZE);
    fifo.entry = qemu_malloc(BATCH_COUNT*sizeof(batch_t));
    for (i = 0; i < CURSOR_COUNT; i++) {
        fifo.head[i] = 0;
        fifo.tail[i] = 0;
    }
    for (i = 0; i < BATCH_COUNT; i++) {
        fifo.entry[i].head = fifo.pool+i*BATCH_SIZE;
        fifo.entry[i].tail = fifo.pool+i*BATCH_SIZE;
    }
}

batch_t *sync_queue_get(int id)
{
    int nid = (id+CURSOR_COUNT-1)%CURSOR_COUNT;
    uint32_t head = fifo.head[id];
    if (id == 0) head = (head+1)%BATCH_COUNT;
    
    uint32_t counter = 0;
    while (head == fifo.tail[nid]) {
        if (!(++counter & 0x3fff)) {
            usleep(128);
        }
    }
    
    batch_t *batch = &fifo.entry[fifo.head[id]];
    fifo.head[id] = (fifo.head[id]+1)%BATCH_COUNT;
    return batch;
}

void sync_queue_put(int id)
{
    fifo.tail[id] = (fifo.tail[id]+1)%BATCH_COUNT;
}

void sync_queue_flush(void)
{
    uint32_t tail = fifo.tail[0];
    
    uint32_t counter = 0;
    while (tail != fifo.tail[CURSOR_COUNT-1]) {
        if (!(++counter & 0x3fff)) {
            usleep(128);
        }
    }
}
