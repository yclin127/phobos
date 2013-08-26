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
    
inline std::ostream &operator <<(std::ostream &os, CommandType type) {
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
    os << name[type];
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
           << "channel: " << (int)coordinates.channel 
           << ", rank: " << (int)coordinates.rank 
           << ", bank: " << (int)coordinates.bank 
           << ", row: " << (int)coordinates.row 
           << ", column: " << (int)coordinates.column
           << ", offset: " << (int)coordinates.offset
           << ", group: " << (int)coordinates.group
           << ", index: " << (int)coordinates.index
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
};

inline int log_2(int value) {
    int result;
    for (result=0; (1<<result)<value; result+=1);
    return result;
}

struct AddressMapping {
    BitField channel;
    BitField rank;
    BitField bank;
    BitField row;
    BitField column;
    BitField offset;
    
    BitField group;
    BitField index;
};

struct AddressRemapping {
    int step;
    int **forward;
    int **backward;
    
    void init(int groups, int indices, int ratio) {
        step = ratio;
        forward = new int*[groups];
        backward = new int*[groups];
        for (int i=0; i<groups; i+=1) {
            forward[i] = new int[indices];
            backward[i] = new int[indices];
            for (int j=0; j<indices; j+=1) {
                forward[i][j] = j;
                backward[i][j] = j;
            }
        }
    }
    
    bool cached(Coordinates &coordinates) {
        return forward[coordinates.group][coordinates.index] % step == 0;
    }
    
    void swap(int group, int index, int victim) {
        int temp1 = forward[group][index];
        int temp2 = forward[group][victim];
        
        forward[group][index] = temp2;
        forward[group][victim] = temp1;
        
        int temp = backward[group][temp1];
        backward[group][temp1] = backward[group][temp2];
        backward[group][temp2] = temp;
        
        assert(backward[group][forward[group][index]] == index);
        assert(backward[group][forward[group][victim]] == victim);
    }
};

struct BankData {
    int demandCount;
    int supplyCount;
    int rowBuffer;
    int hitCount;
    AddressRemapping remapping;
};

struct RankData {
    int demandCount;
    int activeCount;
    int refreshTime;
    bool is_sleeping;
};

};

#endif // __MEMORY_COMMON_H__
