#ifndef __LRU_ALGORITHM_H__
#define __LRU_ALGORITHM_H__

#include "qemu-common.h"

typedef struct line_t {
    target_ulong tag;
    struct line_t *next;
    uint8_t data[0];
} line_t;

typedef struct {
    uint32_t line_bits;
    uint32_t set_count;
    uint32_t way_count;
    uint32_t line_size;
    target_ulong tag_mask;
    line_t **set;
    void *pool;
} cache_t;

static inline void lru_reset(cache_t *cache)
{
    int i, j;
    void *pool;
    line_t *line;
    
    pool = cache->pool;
    for (i=0; i<cache->set_count; ++i) {
        cache->set[i] = (line_t *)pool;
        for (j=0; j<cache->way_count; ++j) {
            line = (line_t *)pool;
            line->tag = -1;
            pool += cache->line_size;
            if (j < cache->way_count-1)
                line->next = (line_t *)pool;
            else
                line->next = NULL;
        }
    }
}

static inline void lru_create(cache_t *cache, int line_bits, int set_bits, int way_count, uint32_t data_size)
{
    cache->line_bits = line_bits;
    cache->set_count = __size(set_bits);
    cache->way_count = way_count;
    cache->line_size = offsetof(line_t, data)+data_size;
    
    cache->tag_mask = -__size(line_bits);
    
    cache->set = (line_t **)qemu_malloc(cache->set_count*sizeof(line_t *));
    cache->pool = qemu_malloc(cache->set_count*cache->way_count*cache->line_size);
    
    lru_reset(cache);
}

static inline line_t *lru_probe(cache_t *cache, target_ulong tag)
{
    line_t *victim;
    
    int index = (tag >> cache->line_bits) & (cache->set_count-1);
    
    // fast pass for first-way hit
    victim = cache->set[index];
    if (likely(victim->tag == tag)) {
        return victim;
    }
    
    // look up lru tags
    while (likely(victim)) {
        if (likely(victim->tag == tag))
            return victim;
        victim = victim->next;
    }
    
    return NULL;
}

static inline line_t *lru_select(cache_t *cache, target_ulong tag)
{
    line_t *previous, *victim;
    
    int index = (tag >> cache->line_bits) & (cache->set_count-1);
    
    // fast pass for first-way hit
    victim = cache->set[index];
    if (likely(victim->tag == tag)) {
        return victim;
    }
    
    // look up lru tags
    previous = NULL;
    while (likely(victim->next)) {
        if (likely(victim->tag == tag))
            break;
        previous = victim;
        victim = victim->next;
    }
    
    // update lru place
    if (likely(previous != NULL)) {
        previous->next = victim->next;
        victim->next = cache->set[index];
        cache->set[index] = victim;
    }
    
    return victim;
}

#endif // __LRU_ALGORITHM_H__