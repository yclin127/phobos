#ifndef MEMORY_MAPPING_H
#define MEMORY_MAPPING_H

#include <ptlsim.h>

#include <memoryCommon.h>
#include <memoryModule.h>

namespace DRAM {


class MemoryMapping
{
    private:
        BitFields bitfields;
        
        AssociativeTags<W64, int> det_counter;
        int det_threshold;
        int mat_group;
        int mat_ratio;
        int rep_serial;
        
        AssociativeTags<W64, int> map_cache;
        Tier3<short> mapping_forward;
        Tier3<short> mapping_backward;
        Tier3<short> mapping_accesses;
        Tier3<short> mapping_migrations;
        Tier3<long> mapping_timestamp;
        
    public:
        MemoryMapping(Config &config);
        virtual ~MemoryMapping();
        
        void extract(W64 address, Coordinates &coordinates);
        bool translate(Coordinates &coordinates);
        void update(W64 tag);

        bool allocate(Coordinates &coordinates);
        bool detect(Coordinates &coordinates);
        bool promote(long clock, Coordinates &coordinates);

        W64 make_index_tag(Coordinates &coordinates) {
            W64 tag = (W64)coordinates.cluster;
            tag = (tag << bitfields.group.width) | coordinates.group;
            tag = (tag << bitfields.index.width) | coordinates.index;
            return tag;
        }
        W64 make_group_tag(Coordinates &coordinates) {
            W64 tag = (W64)coordinates.cluster;
            tag = (tag << bitfields.group.width) | coordinates.group;
            return tag;
        }
};

};

#endif //MEMORY_MAPPING_H