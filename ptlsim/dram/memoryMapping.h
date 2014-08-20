#ifndef MEMORY_MAPPING_H
#define MEMORY_MAPPING_H

#include <ptlsim.h>

#include <memoryCommon.h>
#include <memoryModule.h>

namespace DRAM {

class MemoryMapping
{
    private:
        const Config &dramconfig;

        BitFields bitfields;
        
        AssociativeTags<W64, int> det_counter;
        AssociativeTags<W64, int> map_cache;
        Tier3<short> mapping_forward;
        Tier3<short> mapping_backward;
        Tier3<int>   mapping_accesses;
        Tier3<int>   mapping_migrations;
        Tier3<long>  mapping_timestamp;
        long last_access_time;

        void profile_read();
        void profile_write();
        
    public:
        MemoryMapping(Config &config);
        virtual ~MemoryMapping();
        
        void translate(W64 address, Coordinates &coordinates);
        bool probe(Coordinates &coordinates);
        void update(W64 tag);

        bool allocate(long cycle, Coordinates &coordinates);
        bool detect(Coordinates &coordinates);
        bool promote(long clock, Coordinates &coordinates);
};

};

#endif //MEMORY_MAPPING_H