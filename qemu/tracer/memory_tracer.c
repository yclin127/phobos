#include "tracer/memory_tracer.h"
#include "tracer/code_marker.h"
#include "tracer/cache_filter.h"
#include "tracer/sync_queue.h"

static int memory_tracer_enabled = 0;
static batch_t *batch = NULL;

void memory_tracer_init(void)
{
    sync_queue_init();
    cache_filter_init();
    
    batch = sync_queue_get(0);
    batch->tail = batch->head;
    
    if (memory_tracer_enabled) {
        memory_tracer_enabled = 0;
        memory_tracer_toggle();
    }
}

static batch_t *memory_tracer_next_batch(void)
{
    batch_t *next_batch;
    
    sync_queue_put(0);
    next_batch = sync_queue_get(0);
    next_batch->tail = next_batch->head;
    
    return next_batch;
}

void memory_tracer_toggle(void)
{
    if (!memory_tracer_enabled) {
        // begin a new trace by a batch beginning with a NULL-pointer iblock
        request_t *request = (request_t *)batch->tail;
        request->pointer = NULL;
        request->type.value = 0;
        batch->tail = (void *)(request+1);
    } else {
        // end current trace by a batch beginning with a (-1)-pointer iblock
        request_t *request = (request_t *)batch->tail;
        request->pointer = (void *)-1;
        request->type.value = 0;
        batch->tail = (void *)(request+1);
        // flush current trace
        memory_tracer_flush();
    }
    memory_tracer_enabled = !memory_tracer_enabled;
}

void memory_tracer_flush(void)
{
    if (memory_tracer_enabled) {
        batch = memory_tracer_next_batch();
        sync_queue_flush();
    }
}

/*
 * Memory tracer (helper hack, tcg target hack)
 */

void memory_tracer_access(target_ulong vaddr, target_ulong paddr, uint64_t type)
{
    if (!memory_tracer_enabled) return;
    
    request_t *request = (request_t *)batch->tail;
    if ((void *)request >= batch->head+BATCH_SIZE-sizeof(request_t)) {
        batch = memory_tracer_next_batch();
        request = (request_t *)batch->tail;
    }
    batch->tail = (void *)request+sizeof(request_t);
    
    request->vaddr = vaddr;
    request->paddr = paddr;
    request->type.value = type | TRACER_TYPE_DHELPER;
}

#define r0_tmp tcg_target_call_iarg_regs[0]
#define r1_req tcg_target_call_iarg_regs[1]
#define r2_ptr tcg_target_call_iarg_regs[2]
#define r3_dmy tcg_target_call_iarg_regs[3]
#define ret_val tcg_target_call_oarg_regs[0]

static TCGType type = TCG_TYPE_I64;
static int rexw = P_REXW;

#if (!(TCG_TARGET_REG_BITS == 64 && TARGET_LONG_BITS == 64))
#error memory_tracer doesnt support 32-bit host machine
#endif

inline void memory_tracer_batch_prepare(TCGContext *s);
inline void memory_tracer_batch_allocate(TCGContext *s);

void memory_tracer_dstore(TCGContext *s, const int address, const int tlb_entry)
{
    if (!memory_tracer_enabled) return;
    
    tcg_out_push(s, tlb_entry);
    tcg_out_push(s, address);
}

void memory_tracer_dfetch(TCGContext *s, const uint64_t request_type)
{
    if (!memory_tracer_enabled) return;
    
    memory_tracer_batch_prepare(s);
    memory_tracer_batch_allocate(s);
    
    /* r1_req->vaddr = stack[0];
     * r1_req->paddr = stack[0]+stack[1]->phys_addend;
     * r1_req->type  = request_type;
     */ {
        // pop r0_tmp
        // mov r0_tmp, r1_req->vaddr
        tcg_out_pop(s, r0_tmp);
        tcg_out_modrm_offset(s, OPC_MOVL_EvGv + rexw, r0_tmp, 
                             r1_req, offsetof(request_t, vaddr));
        // pop r2_ptr
        // add r2_ptr->phys_addend, r0_tmp
        // mov r0_tmp, r1_req->paddr
        tcg_out_pop(s, r2_ptr);
        tcg_out_modrm_offset(s, OPC_ADD_GvEv + rexw, r0_tmp, 
                             r2_ptr, offsetof(CPUTLBEntry, phys_addend));
        tcg_out_modrm_offset(s, OPC_MOVL_EvGv + rexw, r0_tmp, 
                             r1_req, offsetof(request_t, paddr));
        // mov $request_type, r0_tmp
        // mov r0_tmp, r1_req->type
        tcg_out_movi(s, TCG_TYPE_I64, r0_tmp, request_type | TRACER_TYPE_DFETCH);
        tcg_out_modrm_offset(s, OPC_MOVL_EvGv + P_REXW, r0_tmp, 
                             r1_req, offsetof(request_t,type));
    }
}

void memory_tracer_iblock(TCGContext *s, const TCGArg *args)
{
    if (!memory_tracer_enabled) return;
    
    uint8_t *label_ptr;
    uint8_t *ifetch_table_ptr;
    
    tcg_out_push(s, r0_tmp);
    tcg_out_push(s, r1_req);
    tcg_out_push(s, r2_ptr);
    
    memory_tracer_batch_prepare(s);
    memory_tracer_batch_allocate(s);
    
    tcg_out_pop(s, r2_ptr);
    
    /* jmp label
     * itable here
     * label:
     */ {
        // jmp label
        tcg_out8(s, OPC_JMP_long);
        label_ptr = s->code_ptr;
        s->code_ptr += sizeof(uint32_t);
        ifetch_table_ptr = s->code_ptr;
        // itable here
        memcpy(s->code_ptr, ifetch_table, ifetch_count*sizeof(request_t));
        s->code_ptr += ifetch_count*sizeof(request_t);
        // label:
        *(uint32_t *)label_ptr = s->code_ptr - ifetch_table_ptr;
    }
    
    /* r1_req->vaddr = TRACER_RAW_TYPE(args[0], 0, 0);
     * r1_req->type.value = 0;
     */ {
        // mov args[0], r0_tmp
        // mov r0_tmp, r1_req->pointer
        tcg_out_movi(s, type, r0_tmp, (size_t)ifetch_table_ptr);
        tcg_out_modrm_offset(s, OPC_MOVL_EvGv + P_REXW, r0_tmp, 
                             r1_req, offsetof(request_t, pointer));
        // mov TRACER_RAW_TYPE(args[0], 0, 0), r0_tmp
        // mov r0_tmp, r1_req->type
        tcg_out_movi(s, TCG_TYPE_I64, r0_tmp, TRACER_MAKE_TYPE(args[0], 0, 0));
        tcg_out_modrm_offset(s, OPC_MOVL_EvGv + P_REXW, r0_tmp, 
                             r1_req, offsetof(request_t, type));
    }
    
    tcg_out_pop(s, r1_req);
    tcg_out_pop(s, r0_tmp);
}

void memory_tracer_istep(TCGContext *s, const TCGArg *args)
{
    if (!memory_tracer_enabled) return;
    
    tcg_out_push(s, r0_tmp);
    tcg_out_push(s, r1_req);
    tcg_out_push(s, r2_ptr);
    
    memory_tracer_batch_prepare(s);
    
    tcg_out_pop(s, r2_ptr);
    
    /* r1_req[-1]->type.count = args[0];
     */ {
        // mov $args[0], r0_tmp
        // mov r0_tmp, r1_req[-1]->type.count
        tcg_out_movi(s, TCG_TYPE_I32, r0_tmp, args[0]);
        tcg_out_modrm_offset(s, OPC_MOVL_EvGv + P_DATA16, r0_tmp, 
                             r1_req, -sizeof(request_t)+offsetof(request_t, type.count));
    }
    
    tcg_out_pop(s, r1_req);
    tcg_out_pop(s, r0_tmp);
}

/* 
 * request_t *r1_req = (request_t *)batch->tail;
 */
inline void memory_tracer_batch_prepare(TCGContext *s)
{    
    /* r2_ptr = *(&batch);
     * r1_req = r2_ptr->tail;
     */ {
        // mov $&batch, r2_ptr
        // mov *r2_ptr, r2_ptr
        // mov r2_ptr->tail, r1_req
        tcg_out_movi(s, type, r2_ptr, (size_t)&batch);
        tcg_out_modrm_offset(s, OPC_MOVL_GvEv + rexw, r2_ptr, r2_ptr, 0);    
        tcg_out_modrm_offset(s, OPC_MOVL_GvEv + rexw, r1_req, 
                             r2_ptr, offsetof(batch_t, tail));
    }
}

static void *next_batch_helper_ptr = NULL;

void memory_tracer_next_batch_helper(TCGContext *s)
{
    next_batch_helper_ptr = s->code_ptr;
    
    /* r2_ptr = batch = memory_tracer_next_batch();
     * r1_req = r2_ptr->tail;
     * return;
     */ {
        // call $memory_tracer_next_batch
        // mov $&batch, r2_ptr
        // mov ret_val, *r2_ptr
        // mov ret_val, r2_ptr
        tcg_out_push(s, ret_val);
        tcg_out_push(s, r3_dmy);
        tcg_out_calli(s, (tcg_target_long)memory_tracer_next_batch);
        tcg_out_pop(s, r3_dmy);
        tcg_out_movi(s, type, r2_ptr, (size_t)&batch);
        tcg_out_modrm_offset(s, OPC_MOVL_EvGv + rexw, ret_val, r2_ptr, 0);
        tcg_out_mov(s, type, r2_ptr, ret_val);
        tcg_out_pop(s, ret_val);
        // mov r2_ptr->tail, r1_req
        tcg_out_modrm_offset(s, OPC_MOVL_GvEv + rexw, r1_req, 
                             r2_ptr, offsetof(batch_t, tail));
        // ret
        tcg_out_opc(s, OPC_RET, 0, 0, 0);
    }
}
 
/*
 * if ((void *)r1_req > r2_ptr->head+BATCH_SIZE-sizeof(request_t)) {
 *     next_batch_helper_ptr();
 * }
 * r2_ptr->tail = (void *)requset+sizeof(request_t);
 */
inline void memory_tracer_batch_allocate(TCGContext *s)
{
    uint8_t *label_ptr;
    
    /* if (r1_req <= r2_ptr->head+BATCH_SIZE-sizeof(request_t))
     *     goto label;
     * next_batch_helper_ptr();
     * label:
     */ {
        // mov r2_ptr->head, r0_tmp
        // add $BATCH_SIZE-$sizeof(request_t), r0_tmp
        tcg_out_modrm_offset(s, OPC_MOVL_GvEv + rexw, r0_tmp, 
                             r2_ptr, offsetof(batch_t, head));
        tgen_arithi(s, ARITH_ADD + rexw, r0_tmp, BATCH_SIZE-sizeof(request_t), 0);
        // cmp r0_tmp, requset
        // jbe label # below
        tgen_arithr(s, ARITH_CMP + rexw, r1_req, r0_tmp);
        tcg_out8(s, 0x3E); // hint taken
        tcg_out8(s, OPC_JCC_short + JCC_JBE);
        label_ptr = s->code_ptr++;
        // call $next_batch_helper_ptr
        tcg_out_calli(s, (tcg_target_long)next_batch_helper_ptr);
        // label:
        *label_ptr = s->code_ptr - label_ptr - 1;
    }
    
    /* 
     * r2_ptr->tail = requset+sizeof(request_t);
     */ {
        // mov r1_req, r0_tmp
        // add $sizeof(request_t), r0_tmp
        tcg_out_mov(s, type, r0_tmp, r1_req);
        tgen_arithi(s, ARITH_ADD + rexw, r0_tmp, sizeof(request_t), 0);
        // mov r0_tmp, r2_ptr->tail
        tcg_out_modrm_offset(s, OPC_MOVL_EvGv + rexw, r0_tmp, 
                             r2_ptr, offsetof(batch_t, tail));
    }
}
