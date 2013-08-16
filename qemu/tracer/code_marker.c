#include "tracer/code_marker.h"
#include "tracer/cache_filter.h"

#include "tcg/tcg-op.h"

static uint8_t translating;

static uint8_t break_before;
static uint8_t break_after;

static uint16_t *last_opc_ptr;
static TCGArg   *last_opparam_ptr;

request_t ifetch_table[IFETCH_TABLE_SIZE];
uint32_t  ifetch_count;

void code_marker_begin(void)
{
    translating = 1;
    
    break_before = 0;
    break_after  = 1;
    
    ifetch_count = 0;
}

void code_marker_end(void)
{
    translating = 0;
}

void code_marker_insn_begin(void)
{
    if (translating == 0) return;
    
    request_t *request = &(ifetch_table[ifetch_count]);
    
    // start a new instruction request
    request->type.flags = 0;
    break_before = break_after;
    break_after  = 0;
    
    // preserve space for ifetch instruction
    last_opc_ptr = gen_opc_ptr;
    last_opparam_ptr = gen_opparam_ptr;
    *gen_opc_ptr++ = INDEX_op_nop1;
    *gen_opparam_ptr++ = 0;
}

void code_marker_insn_end(void)
{
    if (translating == 0) return;
    
    request_t *request = &(ifetch_table[ifetch_count]);
    target_ulong fetch_end = request->vaddr + request->type.size;
    
    // if this instruction crosses cache lines, there must be a fetch before it
    if (!break_before) {
        if (unlikely(__cross(request->vaddr-1, fetch_end-1, cache_line_bits))) {
            break_before = 1;
        }
    }
    
    // put a new request in ifetch table
    if (unlikely(break_before)) {
        ifetch_count += 1;
        request += 1;
        assert(ifetch_count < IFETCH_TABLE_SIZE);
        last_opc_ptr[0] = ifetch_count == 1 ? INDEX_op_iblock : INDEX_op_istep;
        last_opparam_ptr[0] = ifetch_count;
    }
    
    // merge it into former request
    request[-1].type.size = fetch_end - request[-1].vaddr;
    request[-1].type.count += 1;
}

void code_marker_access(target_ulong vaddr, target_ulong paddr, uint64_t type)
{
    if (translating == 0) return;
    
    request_t *request = &(ifetch_table[ifetch_count]);
    if (request->type.flags == 0) {
        request->vaddr = vaddr;
        request->paddr = paddr;
        request->type.value = type;
    } else {
        request->type.size = (vaddr + ((type_t)type).size) - request->vaddr;
        //assert(request->type.size <= 16);
        assert(request->type.flags == ((type_t)type).flags);
    }
}
