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
    COMMAND_readPre,
    COMMAND_writePre,
    COMMAND_refresh,
    COMMAND_migrate,
    COMMAND_powerup,
    COMMAND_powerdown,
};

enum EnergyType {
    ENERGY_actPre,
    ENERGY_read,
    ENERGY_write,
    ENERGY_refresh,
    ENERGY_migrate,
    ENERGY_background,
    ENERGY_total,
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

    long tag;
    
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
inline bool is_pow_of_2(long x) {
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

struct TransactionCount {
    int readCount;
    int writeCount;
    int migrateCount;
    int totalCount;

    void reset() {
        readCount = 0;
        writeCount = 0;
        migrateCount = 0;
        totalCount = 0;
    }

    void add(CommandType type) {
        switch (type) {
            case COMMAND_read:    readCount    += 1; break;
            case COMMAND_write:   writeCount   += 1; break;
            case COMMAND_migrate: migrateCount += 1; break;
            default: assert(0);
        }
        totalCount += 1;
    }

    void remove(CommandType type) {
        switch (type) {
            case COMMAND_read:    readCount    -= 1; break;
            case COMMAND_write:   writeCount   -= 1; break;
            case COMMAND_migrate: migrateCount -= 1; break;
            default: assert(0);
        }
        totalCount -= 1;
    }
};

struct BankData {
    TransactionCount totalTransaction;
    TransactionCount readyTransaction;
    int rowBuffer;
    int hitCount;
};

struct RankData {
    int demandCount;
    int activeCount;
    int refreshTime;
    bool is_sleeping;
};

template<class TagType, class DataType>
class AssociativeTags
{
    private:
        struct Entry {
            TagType tag;
            Entry* next;
            DataType data;
        };
        
        int m_set_count, m_way_count, m_line_shift;
        Entry *m_entries;
        Entry **m_sets;
        
    public:
        AssociativeTags(int size, int way_count, int line_size) {
            m_set_count = size/way_count/line_size;
            m_way_count = way_count;
            m_line_shift = log_2(line_size);
            m_entries = new Entry[m_set_count*m_way_count];
            m_sets    = new Entry*[m_set_count];
            
            for (int i=0; i<m_set_count; i+=1) {
                m_sets[i] = &m_entries[i*m_way_count];
                for (int j=0; j<m_way_count-1; j+=1) {
                    m_sets[i][j].tag = -1;
                    m_sets[i][j].next = &m_sets[i][j+1];
                }
                m_sets[i][m_way_count-1].tag = -1;
                m_sets[i][m_way_count-1].next = NULL;
            }
        }
        
        virtual ~AssociativeTags() {
            delete [] m_entries;
            delete [] m_sets;
        }
        
        bool probe(TagType tag) {
            tag >>= m_line_shift;
            int index = tag % m_set_count;
            Entry *current = m_sets[index];
            while (current != NULL) {
                if (current->tag == tag) {
                    return true;
                }
                current = current->next;
            }
            return false;
        }
        
        DataType& access(TagType tag, TagType &oldtag) {
            tag >>= m_line_shift;
            int index = tag % m_set_count;
            Entry *previous = NULL;
            Entry *current = m_sets[index];
            while (current->tag != tag && current->next != NULL) {
                previous = current;
                current = current->next;
            }
            if (previous != NULL) {
                previous->next = current->next;
                current->next = m_sets[index];
                m_sets[index] = current;
            }
            oldtag = current->tag;
            if (current->tag != tag) {
                current->tag = tag;
            }
            return current->data;
        }
};

template<class DataType>
class Tier3 {
    private:
        DataType ***m_data;
        int m_count[3];

    public:
        Tier3(int tier1, int tier2, int tier3, DataType value) {
            m_count[0] = tier1;
            m_count[1] = tier2;
            m_count[2] = tier3;

            m_data = new DataType**[tier1];
            for (int i=0; i<tier1; i+=1) {
                m_data[i] = new DataType*[tier2];
                for (int j=0; j<tier2; j+=1) {
                    m_data[i][j] = new DataType[tier3];
                    for (int k=0; k<tier3; k+=1) {
                        m_data[i][j][k] = value;
                    }
                }
            }
        }

        virtual ~Tier3() {
            for (int i=0; i<m_count[0]; i+=1) {
                for (int j=0; j<m_count[1]; j+=1) {
                    delete [] m_data[i][j];
                }
                delete [] m_data[i];
            }
            delete [] m_data;
        }

        DataType& item(int tier1, int tier2, int tier3) {
            return m_data[tier1][tier2][tier3];
        }

        void swap(int tier1, int tier2, int tier3, int tier3P) {
            DataType *last_tier = m_data[tier1][tier2];
            DataType temp = last_tier[tier3];
            last_tier[tier3] = last_tier[tier3P];
            last_tier[tier3P] = temp;
        }
};

};

#endif // __MEMORY_COMMON_H__
