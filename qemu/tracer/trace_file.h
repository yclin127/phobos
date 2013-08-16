#ifndef __TRACE_FILE_H__
#define __TRACE_FILE_H__

#include "qemu-common.h"

void trace_file_init(void);
void trace_file_begin(void);
void trace_file_end(void);
void trace_file_log(target_ulong vaddr, target_ulong paddr, uint64_t flags, uint64_t icount);

#endif // __STATISTIC_FILE_H__