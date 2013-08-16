#ifndef __CACHE_FILTER_H__
#define __CACHE_FILTER_H__

extern int cache_line_bits;
extern int cache_set_bits;
extern int cache_way_count;

extern int tlb_page_bits;
extern int tlb_set_bits;
extern int tlb_way_count;

void cache_filter_init(void);

#endif // __CACHE_FILTER_H__
