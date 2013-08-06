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

struct Coordinates {
    int channel;
    int rank;
    int bank;
    int row;
    int column;
    int offset;
    
    int group;
    int index;
    int place;
    
    friend std::ostream &operator <<(std::ostream &os, Coordinates &coordinates) {
        os << "{"
           << "channel: " << (int)coordinates.channel 
           << ", rank: " << (int)coordinates.rank 
           << ", bank: " << (int)coordinates.bank 
           << ", row: " << (int)coordinates.row 
           << ", column: " << (int)coordinates.column
           << ", offset: " << (int)coordinates.offset
           << ", group: " << (int)coordinates.group
           << ", index: " << (int)coordinates.index
           << ", place: " << (int)coordinates.place
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
    int **forward;
    int **backward;
    
    void init(int groups, int indices) {
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
    
    int map(int group, int index) {
        return forward[group][index];
    }
    
    int unmap(int group, int index) {
        return backward[group][index];
    }
    
    void swap(int group, int index1, int index2) {
        int temp1 = backward[group][index1];
        int temp2 = backward[group][index2];
        backward[group][index1] = temp2;
        backward[group][index2] = temp1;
        
        int temp = forward[group][temp1];
        forward[group][temp1] = forward[group][temp2];
        forward[group][temp2] = temp;
        
        assert(backward[group][forward[group][index1]] == index1);
        assert(backward[group][forward[group][index2]] == index2);
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
