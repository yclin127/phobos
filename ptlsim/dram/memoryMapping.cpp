#include <memoryMapping.h>
#include <memoryStatistics.h>

using namespace DRAM;

struct profile_item_t {
    int count;
    int cluster;
    int group;
    int index;
};

int profile_compare(const void *p1, const void *p2)
{
    const profile_item_t &i1 = *(profile_item_t *)p1;
    const profile_item_t &i2 = *(profile_item_t *)p2;
    if (i1.count != i2.count)
        return i2.count - i1.count;
    if (i1.cluster != i2.cluster)
        return i2.cluster - i1.cluster;
    if (i1.group != i2.group)
        return i2.group - i1.group;
    if (i1.index != i2.index)
        return i2.index - i1.index;
    return 0;
}

void MemoryMapping::profile_read()
{
    char path[256];
    FILE* file;

    sprintf(path, "%s.dat", config.log_filename.buf);

    file = fopen(path, "rb");
    if (file) {
        size_t profile_count_max = 1;
        profile_count_max <<= bitfields.cluster.width;
        profile_count_max <<= bitfields.group.width;
        profile_count_max <<= bitfields.index.width;
        size_t profile_count = 0;

        profile_item_t *profile_list = new profile_item_t[profile_count_max];
        for (int i=0; i<(1<<bitfields.cluster.width); i+=1) {
            for (int j=0; j<(1<<bitfields.group.width); j+=1) {
                int showthis = 0;
                for (int k=0; k<(1<<bitfields.index.width); k+=1) {
                    //mapping_forward.item(i,j,k) = 1;

                    int accesses;
                    fread(&accesses, sizeof(accesses), 1, file);
                    if (accesses) {
                        profile_item_t &profile_item = profile_list[profile_count];
                        profile_count += 1;

                        profile_item.count = accesses;
                        profile_item.cluster = i;
                        profile_item.group = j;
                        profile_item.index = k;
                    }
                }
            }
        }

        qsort(profile_list, profile_count, sizeof(profile_item_t), profile_compare);
        for (int i=0; i<profile_count/dramconfig.asym_mat_ratio; i+=1) {
            Coordinates coordinates;
            coordinates.cluster = profile_list[i].cluster;
            coordinates.group = profile_list[i].group;
            coordinates.index = profile_list[i].index;
            promote(0, coordinates);
            /*mapping_forward.item(
                profile_list[i].cluster, 
                profile_list[i].group, 
                profile_list[i].index) = 0;*/
        }

        delete [] profile_list;

        fclose(file);
    }
}

void MemoryMapping::profile_write()
{
    char path[256];
    FILE* file;
    int count_a, count_b, count_c;
    int footprint_a, footprint_b, footprint_c;

    sprintf(path, "%s.dat", config.log_filename.buf);
    
    count_a = 0;
    count_b = 0;
    count_c = 0;
    footprint_a = 0;
    footprint_b = 0;
    footprint_c = 0;

    file = fopen(path, "rb");
    if (file) {
        exit(0);
        for (int i=0; i<(1<<bitfields.cluster.width); i+=1) {
            for (int j=0; j<(1<<bitfields.group.width); j+=1) {
                for (int k=0; k<(1<<bitfields.index.width); k+=1) {
                    int accesse_a, accesse_b;
                    accesse_a = mapping_accesses.item(i,j,k);
                    fread(&accesse_b, sizeof(accesse_b), 1, file);
                    count_a += accesse_a;
                    count_b += accesse_b;
                    count_c += abs(accesse_a-accesse_b);
                    footprint_a += (accesse_a != 0);
                    footprint_b += (accesse_b != 0);
                    footprint_c += (accesse_a != accesse_b);
                }
            }
        }
        fclose(file);
    }
    
    file = fopen(path, "wb");
    if (file) {
        for (int i=0; i<(1<<bitfields.cluster.width); i+=1) {
            for (int j=0; j<(1<<bitfields.group.width); j+=1) {
                for (int k=0; k<(1<<bitfields.index.width); k+=1) {
                    int accesses = mapping_accesses.item(i,j,k);
                    fwrite(&accesses, sizeof(accesses), 1, file);
                }
            }
        }
        fclose(file);
    }

    sprintf(path, "%s.sta", config.log_filename.buf);
    file = fopen(path, "w");
    if (file) {
        fprintf(file, "dist_count %d %d %d\n", count_a, count_b, count_c);
        fprintf(file, "dist_footprint %d %d %d\n", footprint_a, footprint_b, footprint_c);
        fclose(file);
    }
}

MemoryMapping::MemoryMapping(Config &config) : 
    dramconfig(config),
    det_counter(config.asym_det_cache_size, 4, 1),
    map_cache(config.asym_map_cache_size, 4, config.offsetcount),
    mapping_forward(config.clustercount, config.groupcount, config.indexcount, -1),
    mapping_backward(config.clustercount, config.groupcount, config.indexcount, -1),
    mapping_accesses(config.clustercount, config.groupcount, config.indexcount, 0),
    mapping_migrations(config.clustercount, config.groupcount, config.indexcount, 0),
    mapping_timestamp(config.clustercount, config.groupcount, config.indexcount, -1)
{
    //rep_serial = 0;
    last_access_time = 0;
    
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


    if (dramconfig.asym_map_profiling) {
        profile_read();
    }
}

extern ConfigurationParser<PTLsimConfig> config;
MemoryMapping::~MemoryMapping()
{
    if (dramconfig.asym_map_profiling) {
        profile_write();
    }
}

void MemoryMapping::translate(W64 address, Coordinates &coordinates)
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
    coordinates.index   = bitfields.index.value(row ^ (row << bitfields.index.shift));
    coordinates.place   = mapping_forward.item(
        coordinates.cluster, coordinates.group, coordinates.index);

    W64 tag = (W64)coordinates.cluster;
    tag = (tag << bitfields.group.width) | coordinates.group;
    tag = (tag << bitfields.index.width) | coordinates.index;
    coordinates.tag = tag;
}

bool MemoryMapping::probe(Coordinates &coordinates)
{
    if (dramconfig.asym_mat_ratio == 1 || 
        dramconfig.asym_map_profiling == 1 || 
        dramconfig.asym_map_on_chip) {
        return true;
    }

    W64 oldtag;
    W64 tag = coordinates.tag;

    if (dramconfig.asym_map_cache_compact) {
        tag /= dramconfig.asym_mat_ratio;
    }

    if (map_cache.probe(tag)) {
        map_cache.access(tag, oldtag);
        return true;
    }
    return false;
}

void MemoryMapping::update(W64 tag)
{
    if (dramconfig.asym_mat_ratio == 1 ||
        dramconfig.asym_map_profiling == 1) {
        return;
    }

    W64 oldtag;

    if (dramconfig.asym_map_cache_compact) {
        tag /= dramconfig.asym_mat_ratio;
    }

    map_cache.access(tag, oldtag);
}

bool MemoryMapping::allocate(long current_time, Coordinates &coordinates)
{
    int &accesses = mapping_accesses.item(
        coordinates.cluster, coordinates.group, coordinates.index);
    long &timestamp = mapping_timestamp.item(
        coordinates.cluster, coordinates.group, coordinates.index);

    /*long count = accesses + 1;

    if (memoryDistribution.dist_accesses.find(count) == 
        memoryDistribution.dist_accesses.end())
        memoryDistribution.dist_accesses[count] = 0;
    memoryDistribution.dist_accesses[count] += 1;

    long interval = current_time - last_access_time;

    if (memoryDistribution.dist_intervals.find(interval) == 
        memoryDistribution.dist_intervals.end())
        memoryDistribution.dist_intervals[interval] = 0;
    memoryDistribution.dist_intervals[interval] += 1;

    long history = current_time - timestamp;

    if (memoryDistribution.dist_histories.find(history) == 
        memoryDistribution.dist_histories.end())
        memoryDistribution.dist_histories[history] = 0;
    memoryDistribution.dist_histories[history] += 1;*/

    accesses = accesses + 1;
    timestamp = current_time;
    last_access_time = current_time;
    
    return accesses > 1;
}

bool MemoryMapping::detect(Coordinates &coordinates)
{
    if (dramconfig.asym_mat_ratio == 1 ||
        dramconfig.asym_map_profiling == 1) {
        return false;
    }

    W64 oldtag;
    W64 tag = coordinates.tag;
    int& count = det_counter.access(tag, oldtag);
    if (oldtag != tag) count = 0;
    count += 1;

    return count == dramconfig.asym_det_threshold && coordinates.place % dramconfig.asym_mat_ratio != 0;
}

bool MemoryMapping::promote(long clock, Coordinates &coordinates)
{
    const int cluster = coordinates.cluster;
    const int group = coordinates.group;
    const int index = coordinates.index;
    int place = 0;

    if (dramconfig.asym_rep_order) {
        int victim_place = 0;
        long victim_timestamp = 0;
        for (int search_place=0; search_place<dramconfig.asym_mat_group; search_place+=dramconfig.asym_mat_ratio) {
            int search_index = mapping_backward.item(
                cluster, group, search_place);
            long timestamp = mapping_timestamp.item(
                cluster, group, search_index);
            if (victim_timestamp == 0 || victim_timestamp > timestamp) {
                victim_timestamp = timestamp;
                victim_place = search_place;
            }
        }
        place = victim_place;
    } else if (dramconfig.asym_rep_last) {
        int last_place = 0;
        long last_timestamp = 0;
        for (int search_place=0; search_place<dramconfig.asym_mat_group; search_place+=dramconfig.asym_mat_ratio) {
            int search_index = mapping_backward.item(
                cluster, group, search_place);
            long timestamp = mapping_timestamp.item(
                cluster, group, search_index);
            if (last_timestamp < timestamp) {
                last_timestamp = timestamp;
                last_place = search_place;
            }
        }
        place = (rand()*dramconfig.asym_mat_ratio)%dramconfig.asym_mat_group;
        if (place == last_place) {
            place = (place+dramconfig.asym_mat_ratio)%dramconfig.asym_mat_group;
        }
    } else {
        place = (unsigned int)(rand()*dramconfig.asym_mat_ratio)%dramconfig.asym_mat_group;
    }

    //int place = rep_serial;
    //rep_serial = (rep_serial + mat_ratio) % mat_group;

    int indexP = mapping_backward.item(cluster, group, place);
    int placeP = mapping_forward.item(cluster, group, index);
    mapping_forward.swap(cluster, group, index, indexP);
    mapping_backward.swap(cluster, group, place, placeP);
    coordinates.place = place;

    int &migrations = mapping_migrations.item(
        coordinates.cluster, coordinates.group, coordinates.index);
    migrations += 1;
    return migrations > 1;
}
