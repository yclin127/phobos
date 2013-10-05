#ifndef __MEMORY_COMMON_H__
#define __MEMORY_COMMON_H__

#include <ostream>
#include <cassert>

namespace DRAM {

enum CommandType {
    COMMAND_activate,
    COMMAND_precharge,
    COMMAND_read,
    COMMAND_write,
    COMMAND_read_precharge,
    COMMAND_write_precharge,
    COMMAND_refresh,
    COMMAND_migrate,
    COMMAND_powerup,
    COMMAND_powerdown,
};
    
inline const char* toString(CommandType type) {
    static const char* name[] = {
        "COMMAND_activate",
        "COMMAND_precharge",
        "COMMAND_read",
        "COMMAND_write",
        "COMMAND_read_precharge",
        "COMMAND_write_precharge",
        "COMMAND_refresh",
        "COMMAND_migrate",
        "COMMAND_powerup",
        "COMMAND_powerdown",
    };
    return name[type];
}
    
inline std::ostream &operator <<(std::ostream &os, CommandType type) {
    os << toString(type);
    return os;
}

struct Coordinates {
    int channel;
    int rank;
    int bank;
    int row;
    int column;
    int offset;
    
    int group;
    int index;
    
    friend inline std::ostream &operator <<(std::ostream &os, Coordinates &coordinates) {
        os << "{"
           << "channel: " << coordinates.channel 
           << ", rank: " << coordinates.rank 
           << ", bank: " << coordinates.bank 
           << ", row: " << coordinates.row 
           << ", column: " << coordinates.column
           << ", offset: " << coordinates.offset
           << ", group: " << coordinates.group
           << ", index: " << coordinates.index
           << "}";
        return os;
    }
};
    
struct BitField {
    unsigned width;
    unsigned shift;
    
    long value(long address) {
        return (address >> shift) & ((1 << width) - 1);
    }
    
    long clamp(long address) {
        return (address) & ((1 << width) - 1);
    }
    
    long pack(long value) {
        return value << shift;
    }
    
    long mask(long address) {
        return address & (((1 << width) - 1) << shift);
    }
    
    friend inline std::ostream &operator <<(std::ostream &os, BitField &bitfield) {
        os << "{"
           << "shift: " << bitfield.shift 
           << ", width: " << bitfield.width 
           << "}";
        return os;
    }
};

inline int log_2(int value) {
    int result;
    for (result=0; (1<<result)<value; result+=1);
    return result;
}

struct BitMapping {
    BitField channel;
    BitField rank;
    BitField bank;
    BitField row;
    BitField column;
    BitField offset;
    
    BitField group;
    BitField index;
};

struct BankData {
    int demandCount;
    int supplyCount;
    int rowBuffer;
    int hitCount;
};

struct RankData {
    int demandCount;
    int activeCount;
    int refreshTime;
    bool is_sleeping;
};

};

#endif // __MEMORY_COMMON_H__
