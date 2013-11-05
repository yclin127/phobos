#ifndef MEMORY_MAPPING_H
#define MEMORY_MAPPING_H

#include <ptlsim.h>

#include <memoryCommon.h>
#include <memoryModule.h>

namespace DRAM {

#define MAPPING_TAG_SHIFT 0
#define MAPPING_TAG_SIZE (1<<(MAPPING_TAG_SHIFT))

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

        W64 make_forward_tag(int cluster, int group, int index) {
            W64 tag = (W64)cluster;
            tag = (tag << bitfields.group.width) | group;
            tag = (tag << 1) | 0;
            tag = (tag << bitfields.index.width) | index;
            return tag << MAPPING_TAG_SIZE;
        }
        W64 make_backward_tag(int cluster, int group, int place) {
            W64 tag = (W64)cluster;
            tag = (tag << bitfields.group.width) | group;
            tag = (tag << 1) | 1;
            tag = (tag << bitfields.index.width) | place;
            return tag << MAPPING_TAG_SIZE;
        }
        
    public:
        MemoryMapping(Config &config);
        virtual ~MemoryMapping();
        
        void extract(W64 address, Coordinates &coordinates);
        bool translate(Coordinates &coordinates);

        bool allocate(Coordinates &coordinates);
        bool detect(Coordinates &coordinates);
        bool promote(long clock, Coordinates &coordinates);
};

};

#endif //MEMORY_MAPPING_H