#ifndef __MEMORY_TRACER_H__
#define __MEMORY_TRACER_H__

void memory_tracer_init(void);
void memory_tracer_toggle(void);
void memory_tracer_flush(void);

#ifdef TCGv
void memory_tracer_dstore(TCGContext *s, const int address, const int tlb_entry);
void memory_tracer_dfetch(TCGContext *s, const uint64_t request_type);
void memory_tracer_iblock(TCGContext *s, const TCGArg *args);
void memory_tracer_istep(TCGContext *s, const TCGArg *args);
void memory_tracer_next_batch_helper(TCGContext *s);
#endif

#endif // __MEMORY_TRACER_H__