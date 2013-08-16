#ifndef __CODE_MARKER_H__
#define __CODE_MARKER_H__

#include "tracer/common.h"

extern request_t ifetch_table[];
extern uint32_t  ifetch_count;

void code_marker_begin(void);
void code_marker_end(void);
void code_marker_insn_begin(void);
void code_marker_insn_end(void);

#endif /* __CODE_MARKER_H__ */