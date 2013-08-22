#include "tracer/cache_filter.h"
#include "tracer/sync_queue.h"
#include "tracer/trace_file.h"

int cache_line_bits = 6;
int cache_set_bits  = 12;
int cache_way_count = (1<<3);

int tlb_page_bits = 12;
int tlb_set_bits  = 8;
int tlb_way_count = (1<<2);

#include "tracer/lru_algorithm.h"

typedef struct {
    target_ulong ptag;
    uint32_t lookup_count;
    uint32_t read_count;
    uint32_t write_count;
} tlb_data_t;

typedef struct {
    target_ulong vtag;
    uint32_t flags;
} cache_data_t;

static cache_t tlb;
static cache_t cache;

static inline void cache_create(void)
{
    lru_create(&tlb, tlb_page_bits, tlb_set_bits, tlb_way_count, sizeof(tlb_data_t));
    lru_create(&cache, cache_line_bits, cache_set_bits, cache_way_count, sizeof(cache_data_t));
}

static inline void cache_access(const request_t *request, uint64_t icount)
{
    line_t *cache_line, *tlb_entry;
    tlb_data_t *tlb_data;
    cache_data_t *cache_data;
    
    // make sure there's nothing funny going on around here.
    // otherwise, there're probablity something wrong in code marker or memory tracer.
    assert(!((request->vaddr ^ request->paddr) & ~tlb.tag_mask));
    
    // tlb
    {
        // lru_access don't handle unaligned requests, so make them aligned here.
        target_ulong vaddr = request->vaddr & tlb.tag_mask;
        target_ulong paddr = request->paddr & tlb.tag_mask;
        //target_ulong end = (request->paddr+request->type.size-1) & tlb.tag_mask;
        
        tlb_entry = lru_select(&tlb, vaddr);
        tlb_data = (tlb_data_t *)tlb_entry->data;
        if (unlikely(tlb_entry->tag != vaddr)) {
            if (tlb_entry->tag != -1) {
                // evict
                trace_file_log(tlb_entry->tag, tlb_data->ptag, TRACER_TYPE_TLB_EVICT, icount);
            }
            // tlb walk
            trace_file_log(vaddr, paddr, request->type.flags | TRACER_TYPE_TLB_WALK, icount);
            // reset entry
            tlb_entry->tag = vaddr;
            tlb_data->ptag = paddr;
            tlb_data->lookup_count = 0;
            tlb_data->read_count   = 0;
            tlb_data->write_count  = 0;
        }
        tlb_data->lookup_count++;
    }
    
    // cache
    {
        // lru_access don't handle unaligned requests, so make them aligned here.
        target_ulong vaddr = request->vaddr & cache.tag_mask;
        target_ulong paddr = request->paddr & cache.tag_mask;
        target_ulong end = (request->vaddr+request->type.size-1) & cache.tag_mask;
        
        for (;;) {
            cache_line = lru_select(&cache, paddr);
            cache_data = (cache_data_t *)cache_line->data;
            if (unlikely(cache_line->tag != paddr)) {
                if (cache_data->flags & TRACER_TYPE_WRITE) {
                    // write back
                    trace_file_log(cache_data->vtag, cache_line->tag, cache_data->flags | 
                                   TRACER_TYPE_MEM_WRITE, icount);
                    line_t *wb_tlb_entry = lru_probe(&tlb, cache_data->vtag & tlb.tag_mask);
                    if (wb_tlb_entry) {
                        tlb_data_t *wb_tlb_data = (tlb_data_t *)wb_tlb_entry->data;
                        wb_tlb_data->write_count++;
                    }
                }
                // miss
                trace_file_log(vaddr, paddr, request->type.flags | TRACER_TYPE_MEM_READ, icount);
                tlb_data->read_count++;
                // reset entry
                cache_line->tag = paddr;
                cache_data->vtag = vaddr;
                cache_data->flags = 0;
            }
            cache_data->flags |= request->type.flags;
            
            // chop up requests if they cross cachelines
            if (likely(vaddr == end)) break;
            vaddr += __size(cache.line_bits);
            paddr += __size(cache.line_bits);
        }
    }
}

/*
 * concurreny fifo
 */

static QemuThread cache_filter_thread;
static QemuMutex cache_filter_mutex;
static QemuCond cache_filter_ready;
static void *cache_filter_main(void *args);

void cache_filter_init(void)
{
    qemu_mutex_init(&cache_filter_mutex);
    qemu_cond_init(&cache_filter_ready);
    qemu_thread_create(&cache_filter_thread, cache_filter_main, NULL);
    qemu_cond_wait(&cache_filter_ready, &cache_filter_mutex);
}

static void *cache_filter_main(void *args)
{
    const batch_t *batch = NULL;
    const request_t *request;
    uint64_t icount = 0;
    
    const request_t *ifetch_table = NULL;
    const request_t *ifetch_ptr;
    int ifetch_count = 0;

    // trace_file_init() must be called before cache_create(), 
    // because it also reads in the cache configuratino.
    trace_file_init();
    
    cache_create();
    
    qemu_cond_signal(&cache_filter_ready);
    
    for (;;) {
        batch = sync_queue_get(1);
        request = (request_t *)batch->head;
        
        for(; request < (request_t *)batch->tail; ++request) {
            // iblock
            if (!request->type.flags) {
                if (unlikely(request->pointer == NULL)) {
                    // iblock with a NULL pointer is used to begin a new trace
                    trace_file_begin();
                    lru_reset(&cache);
                    icount = 0;
                    continue;
                } else if (unlikely(request->pointer == (void *)-1)) {
                    // iblock with a -1 pointer is used to end current trace
                    trace_file_end();
                    continue;
                } else {
                    ifetch_table = request->pointer;
                    ifetch_count = 0;
                }
            }
            
            // FIXME iblock of the first block may be missing. skip all dfetch & istep!
            if (unlikely(!ifetch_table)) {
                continue;
            }
            
            // dfetch & ifetch
            if (request->type.flags) {
                cache_access(request, icount);
            }
            
            // istep
            for (; ifetch_count < request->type.count; ++ifetch_count) {
                ifetch_ptr = &ifetch_table[ifetch_count];
                icount += ifetch_ptr->type.count;
                cache_access(ifetch_ptr, icount);
            }
        }
        
        sync_queue_put(1);
    }
    
    return NULL;
}
