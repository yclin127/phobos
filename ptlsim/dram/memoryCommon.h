#ifndef __MEMORY_COMMON_H__
#define __MEMORY_COMMON_H__

#include <ostream>
#include <cassert>
#include <ptlsim.h>

namespace DRAM {

enum CommandType {
    COMMAND_activate,
    COMMAND_precharge,
    COMMAND_read,
    COMMAND_write,
    COMMAND_read_precharge,
    COMMAND_write_precharge,
    COMMAND_migrate,
    COMMAND_refresh,
    COMMAND_powerup,
    COMMAND_powerdown,
};

inline const char* toString(CommandType type) {
    static const char* name[] = {
        "act",
        "pre",
        "rd ",
        "wr ",
        "rdp",
        "wrp",
        "mgr",
        "ref",
        "pup",
        "pdn",
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
    
    int cluster;
    int group;
    int index;
    int place;
    
    friend inline std::ostream &operator <<(std::ostream &os, Coordinates &coordinates) {
        /*os << "{"
           << "channel: " << coordinates.channel 
           << ", rank: " << coordinates.rank 
           << ", bank: " << coordinates.bank 
           << ", row: " << coordinates.row 
           << ", column: " << coordinates.column
           << ", offset: " << coordinates.offset
           << ", group: " << coordinates.group
           << ", index: " << coordinates.index
           << "}";*/
        os << std::hex
           << coordinates.channel 
           << "_" << coordinates.rank 
           << "_" << coordinates.bank 
           << "_" << coordinates.row 
           << "_" << coordinates.column
           << "_" << coordinates.offset
           << std::dec;
        return os;
    }
};
    
struct BitField {
    unsigned width;
    unsigned shift;
    
    long value(long address) {
        return (address >> shift) & ((1 << width) - 1);
    }
    
    friend inline std::ostream &operator <<(std::ostream &os, BitField &bitfield) {
        os << "{"
           << "shift: " << bitfield.shift 
           << ", width: " << bitfield.width 
           << "}";
        return os;
    }
};

inline int log_2(long value) {
    int result;
    for (result=0; (1L<<result)<value; result+=1);
    return result;
}
inline bool is_pow_2(long x) {
    return x > 0 && (x & (x-1)) == 0;
}

struct BitFields {
    BitField channel;
    BitField rank;
    BitField bank;
    BitField row;
    BitField column;
    BitField offset;
    
    BitField cluster;
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

template<class DataType>
class AssociativeTags
{
    private:
        struct Entry {
            W64 tag;
            Entry* next;
            DataType data;
        };
        
        int set_count, way_count, line_shift;
        Entry *entries;
        Entry **sets;
        
    public:
        AssociativeTags(int nset, int nway, int shift) {
            set_count = nset;
            way_count = nway;
            line_shift = shift;
            entries = new Entry[set_count*way_count];
            sets    = new Entry*[set_count];
            
            for (int i=0; i<set_count; i+=1) {
                sets[i] = &entries[i*way_count];
                for (int j=0; j<way_count; j+=1) {
                    sets[i][j].tag = -1;
                    sets[i][j].next = &sets[i][j+1];
                }
                sets[i][way_count-1].next = NULL;
            }
        }
        
        virtual ~AssociativeTags() {
            delete [] entries;
            delete [] sets;
        }
        
        bool probe(W64 tag) {
            int index = (tag >> line_shift) % set_count;
            Entry *current = sets[index];
            while (current != NULL) {
                if (current->tag == tag) {
                    return true;
                }
                current = current->next;
            }
            return false;
        }
        
        DataType& access(W64 tag, W64 &oldtag) {
            int index = tag % set_count;
            Entry *previous = NULL;
            Entry *current = sets[index];
            while (current->tag != tag && current->next != NULL) {
                previous = current;
                current = current->next;
            }
            if (previous != NULL) {
                previous->next = current->next;
                current->next = sets[index];
                sets[index] = current;
            }
            oldtag = current->tag;
            if (current->tag != tag) {
                current->tag = tag;
            }
            return current->data;
        }
        
        void invalid(W64 tag) {
            int index = tag % set_count;
            Entry *previous = NULL;
            Entry *current = sets[index];
            Entry *last = sets[index];
            while (last->next != NULL) {
                if (current->tag != tag) {
                    previous = last;
                    current = last->next;
                }
                last = last->next;
            }
            if (current->tag == tag) {
                current->tag = -1;
                if (current != last) {
                    if (previous == NULL) {
                        sets[index] = current->next;
                        current->next = NULL;
                        last->next = current;
                    } else {
                        previous->next = current->next;
                        current->next = NULL;
                        last->next = current;
                    }
                }
            }
        }
};

template<class DataType>
class Tier3 {
    private:
        DataType ***data;
        int count[3];

    public:
        Tier3(int tier1, int tier2, int tier3, DataType value) {
            count[0] = tier1;
            count[1] = tier2;
            count[2] = tier3;

            data = new DataType**[tier1];
            for (int i=0; i<tier1; i+=1) {
                data[i] = new DataType*[tier2];
                for (int j=0; j<tier2; j+=1) {
                    data[i][j] = new DataType[tier3];
                    for (int k=0; k<tier3; k+=1) {
                        data[i][j][k] = value;
                    }
                }
            }
        }

        virtual ~Tier3() {
            for (int i=0; i<count[0]; i+=1) {
                for (int j=0; j<count[1]; j+=1) {
                    delete [] data[i][j];
                }
                delete [] data[i];
            }
            delete [] data;
        }

        DataType& item(int tier1, int tier2, int tier3) {
            return data[tier1][tier2][tier3];
        }

        void swap(int tier1, int tier2, int tier3, int tier3P) {
            DataType *last_tier = data[tier1][tier2];
            DataType temp = last_tier[tier3];
            last_tier[tier3] = last_tier[tier3P];
            last_tier[tier3P] = temp;
        }
};

};

#endif // __MEMORY_COMMON_H__
