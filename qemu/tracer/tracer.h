#ifndef __TRACER_H__
#define __TRACER_H__

// request types
#define TRACER_TYPE_INSN  0x1
#define TRACER_TYPE_DATA  0x2
#define TRACER_TYPE_READ  0x4
#define TRACER_TYPE_WRITE 0x8
// memory types
#define TRACER_TYPE_MEM_READ  0x10
#define TRACER_TYPE_MEM_WRITE 0x20
#define TRACER_TYPE_TLB_WALK  0x40
#define TRACER_TYPE_TLB_EVICT 0x80
// source types
#define TRACER_TYPE_DHELPER 0x100
#define TRACER_TYPE_DFETCH  0x200
#define TRACER_TYPE_IFETCH  0x400

#define TRACER_MAKE_FLAGS(mmu, vcore, types) (1<<(vcore+24)|1<<(mmu+16)|(types))
#define TRACER_MAKE_TYPE(count, size, flags) ((uint64_t)(count)<<48|(uint64_t)(size)<<32|(uint64_t)(flags))

#define TRACER_CODE_TYPE(size, mmu, vcore) TRACER_MAKE_TYPE(0, size, \
    TRACER_MAKE_FLAGS(mmu, vcore, TRACER_TYPE_INSN | TRACER_TYPE_READ))
#define TRACER_READ_TYPE(size, mmu, vcore) TRACER_MAKE_TYPE(0, size, \
    TRACER_MAKE_FLAGS(mmu, vcore, TRACER_TYPE_DATA | TRACER_TYPE_READ))
#define TRACER_WRITE_TYPE(size, mmu, vcore) TRACER_MAKE_TYPE(0, size, \
    TRACER_MAKE_FLAGS(mmu, vcore, TRACER_TYPE_DATA | TRACER_TYPE_WRITE))

void code_marker_access(target_ulong vaddr, target_ulong paddr, uint64_t type);
void memory_tracer_access(target_ulong vaddr, target_ulong paddr, uint64_t type);

#endif // __TRACER_H__
