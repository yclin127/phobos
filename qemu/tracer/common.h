#ifndef __CONFIG_H__
#define __CONFIG_H__

#include "exec-all.h"
#include "qemu-common.h"

#define __size(b)   ((target_ulong)1<<(b))
#define __mask(x,b,o) ((x)&(__size((b)+(o))-__size(o)))
#define __trim(x,b,o) (((x)>>(o))&(__size(b)-1))
#define __cross(x,y,b) ((x)^(y)>>(b))

#define IFETCH_TABLE_SIZE 256

// between memory tracer & cache filter
#define CURSOR_COUNT 2
#define BATCH_COUNT (1<<8)
#define BATCH_SIZE (1<<16)

// between cache filter & python backend
#define PACKAGE_SIZE (1<<12)

typedef union {
    uint64_t value;
    struct {
        uint32_t flags;
        uint16_t size;
        uint16_t count;
    };
} type_t;

typedef struct {
    union {
        struct {
            target_ulong vaddr;
            target_ulong paddr;
        };
        void *pointer;
    };
    type_t type;
} request_t;

typedef struct {
    target_ulong vaddr;
    target_ulong paddr;
    uint64_t flags;
    uint64_t icount;
} log_t;

typedef struct {
    void   *head;
    void   *tail;
} batch_t;

#endif // __CONFIG_H__
