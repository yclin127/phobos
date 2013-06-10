#ifndef __MEMORY_COMMON_H__
#define __MEMORY_COMMON_H__

#include <ostream>

namespace DRAM {

enum CommandType {
    COMMAND_activate,
    COMMAND_precharge,
    COMMAND_read,
    COMMAND_write,
    COMMAND_read_precharge,
    COMMAND_write_precharge,
    COMMAND_refresh,
    COMMAND_powerup,
    COMMAND_powerdown,
};

struct Coordinates {
    int channel;
    int rank;
    int bank;
    int row;
    int column;
    
    friend std::ostream &operator <<(std::ostream &os, Coordinates &coordinates) {
        os << "{"
           << "channel: " << (int)coordinates.channel 
           << ", rank: " << (int)coordinates.rank 
           << ", bank: " << (int)coordinates.bank 
           << ", row: " << (int)coordinates.row 
           << ", column: " << (int)coordinates.column
           << "}";
        return os;
    }
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
