#ifndef __SYNC_QUEUE_H__
#define __SYNC_QUEUE_H__

#include "tracer/common.h"

#include "qemu-thread.h"

void sync_queue_init(void);
batch_t *sync_queue_get(int id);
void sync_queue_put(int id);
void sync_queue_flush(void);

#endif // __SYNC_QUEUE_H__