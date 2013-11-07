#include <memoryMapping.h>

using namespace DRAM;

MemoryMapping::MemoryMapping(Config &config) : 
    det_counter(config.asym_det_cache_size, 4, 1),
    map_cache(config.asym_map_cache_size, 4, config.offsetcount),
    mapping_forward(config.clustercount, config.groupcount, config.indexcount, -1),
    mapping_backward(config.clustercount, config.groupcount, config.indexcount, -1),
    mapping_accesses(config.clustercount, config.groupcount, config.indexcount, 0),
    mapping_migrations(config.clustercount, config.groupcount, config.indexcount, 0),
    mapping_timestamp(config.clustercount, config.groupcount, config.indexcount, -1)
{
    det_threshold = config.asym_det_threshold;
    mat_group = config.asym_mat_group;
    mat_ratio = config.asym_mat_ratio;
    rep_serial = 0;
    
    int shift = 0;
    bitfields.offset.shift  = shift; shift +=
    bitfields.offset.width  = log_2(config.offsetcount);
    bitfields.channel.shift = shift; shift +=
    bitfields.channel.width = log_2(config.channelcount);
    bitfields.column.shift  = shift; shift +=
    bitfields.column.width  = log_2(config.columncount);
    bitfields.bank.shift    = shift; shift +=
    bitfields.bank.width    = log_2(config.bankcount);
    bitfields.rank.shift    = shift; shift +=
    bitfields.rank.width    = log_2(config.rankcount);
    bitfields.row.shift     = shift; shift +=
    bitfields.row.width     = log_2(config.rowcount);
    
    shift = 0;
    bitfields.group.shift   = shift; shift +=
    bitfields.group.width   = log_2(config.groupcount);
    bitfields.index.shift   = shift; shift +=
    bitfields.index.width   = log_2(config.indexcount);
    bitfields.cluster.shift = shift; shift +=
    bitfields.cluster.width = log_2(config.clustercount);

    for (int i=0; i<config.clustercount; i+=1) {
        for (int j=0; j<config.groupcount; j+=1) {
            for (int k=0; k<config.indexcount; k+=1) {
                mapping_forward.item(i,j,k) = k;
                mapping_backward.item(i,j,k) = k;
            }
        }
    }
}

MemoryMapping::~MemoryMapping()
{
}

void MemoryMapping::extract(W64 address, Coordinates &coordinates)
{
    /** Address mapping scheme goes here. */
    
    coordinates.channel = bitfields.channel.value(address);
    coordinates.rank    = bitfields.rank.value(address);
    coordinates.bank    = bitfields.bank.value(address);
    coordinates.row     = bitfields.row.value(address);
    coordinates.column  = bitfields.column.value(address);
    coordinates.offset  = bitfields.offset.value(address);
    
    W64 row = coordinates.channel;
    row = (row << bitfields.rank.width) | coordinates.rank;
    row = (row << bitfields.bank.width) | coordinates.bank;
    row = (row << bitfields.row.width) | coordinates.row;

    coordinates.cluster = bitfields.cluster.value(row);
    coordinates.group   = bitfields.group.value(row);
    coordinates.index   = bitfields.index.value(row);
    coordinates.place   = -1;
}

bool MemoryMapping::translate(Coordinates &coordinates)
{
    int place = mapping_forward.item(
        coordinates.cluster, coordinates.group, coordinates.index);

    W64 tag = make_index_tag(coordinates)/mat_ratio;
    if (!map_cache.probe(tag)) return false;

    coordinates.place = place;

    return true;
}

void MemoryMapping::update(W64 tag)
{
    W64 oldtag;
    assert(tag != (W64)-1);
    map_cache.access(tag/mat_ratio, oldtag);
}

bool MemoryMapping::allocate(Coordinates &coordinates)
{
    short &accesses = mapping_accesses.item(
        coordinates.cluster, coordinates.group, coordinates.index);
    
    accesses += 1;
    return accesses > 1;
}

bool MemoryMapping::detect(Coordinates &coordinates)
{
    W64 tag, oldtag;
    tag = make_index_tag(coordinates);
    int& count = det_counter.access(tag, oldtag);
    if (oldtag != tag) count = 0;
    count += 1;

    return count == det_threshold && coordinates.place % mat_ratio != 0;
}

bool MemoryMapping::promote(long clock, Coordinates &coordinates)
{
    int cluster = coordinates.cluster;
    int group = coordinates.group;
    int index = coordinates.index;
    int place = rep_serial;
    
    int indexP = mapping_backward.item(cluster, group, place);
    int placeP = mapping_forward.item(cluster, group, index);
    
    mapping_forward.swap(cluster, group, index, indexP);
    mapping_backward.swap(cluster, group, place, placeP);

    rep_serial = (rep_serial + mat_ratio) % mat_group;

    short &migrations = mapping_migrations.item(
        coordinates.cluster, coordinates.group, coordinates.index);
    
    migrations += 1;
    return migrations > 1;
}
