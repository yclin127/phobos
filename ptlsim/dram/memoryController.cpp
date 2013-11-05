#ifdef MEM_TEST
#include <test.h>
#else
#include <ptlsim.h>
#define PTLSIM_PUBLIC_ONLY
#include <ptlhwdef.h>
#endif

#include <memoryController.h>
#include <memoryHierarchy.h>

#include <machine.h>

#include <memoryModule.h>
#include <memoryStatistics.h>

namespace DRAM {
    MemoryCounter memoryCounter = {0};
};

using namespace DRAM;

MemoryMapping::MemoryMapping(Config &config) : 
    det_counter(config.asym_det_cache_size/4, 4, MAPPING_TAG_SHIFT),
    map_cache(config.asym_map_cache_size/4, 4, log_2(config.offsetcount)),
    mapping_forward(config.clustercount, config.groupcount, config.indexcount, -1),
    mapping_backward(config.clustercount, config.groupcount, config.indexcount, -1),
    mapping_footprint(config.clustercount, config.groupcount, config.indexcount, 0),
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

    row ^= (row >> bitfields.group.shift) << bitfields.index.shift;

    coordinates.cluster = bitfields.cluster.value(row);
    coordinates.group   = bitfields.group.value(row);
    coordinates.index   = bitfields.index.value(row);
    coordinates.place   = -1;
}

bool MemoryMapping::translate(Coordinates &coordinates)
{
    W64 tag = make_forward_tag(coordinates);
    if (!map_cache.probe(tag)) return false;

    coordinates.place = mapping_forward.item(
        coordinates.cluster, coordinates.group, coordinates.index);

    return true;
}

void MemoryMapping::update(W64 tag)
{
    W64 oldtag;
    assert(tag != (W64)-1);
    map_cache.access(tag, oldtag);
}

bool MemoryMapping::allocate(Coordinates &coordinates)
{
    char &flags = mapping_footprint.item(
        coordinates.cluster, coordinates.group, coordinates.index);
    
    bool result = (flags & 1);
    flags |= 1;

    return result;
}

bool MemoryMapping::detect(Coordinates &coordinates)
{
    W64 tag, oldtag;
    tag = make_forward_tag(coordinates);
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
    
    mapping_timestamp.item(cluster, group, index) = clock;
    mapping_timestamp.item(cluster, group, indexP) = clock;

    rep_serial = (rep_serial + mat_ratio) % mat_group;
    

    char &flags = mapping_footprint.item(
        coordinates.cluster, coordinates.group, coordinates.index);
    
    bool result = (flags & 2);
    flags |= 2;

    return result;
}

MemoryController::MemoryController(Config &config, MemoryMapping &mapping, int chid)
{
    max_row_hits = config.max_row_hits;
    max_row_idle = config.max_row_idle;
        
    asym_mat_group = config.asym_mat_group;
    asym_mat_ratio = config.asym_mat_ratio;

    channel_id = chid;
    
    rankcount = config.rankcount;
    bankcount = config.bankcount;
    groupcount = config.groupcount;
    indexcount = config.indexcount;
    refresh_interval = config.rank_timing.refresh_interval;
    channel = new Channel(&config);
    
    Coordinates coordinates = {0};
    int refresh_step = refresh_interval/rankcount;
    
    for (coordinates.rank=0; coordinates.rank<rankcount; ++coordinates.rank) {
        // initialize rank
        RankData &rank = channel->getRankData(coordinates);
        rank.demandCount = 0;
        rank.activeCount = 0;
        rank.refreshTime = refresh_step*(coordinates.rank+1);
        rank.is_sleeping = false;
        
        for (coordinates.bank=0; coordinates.bank<bankcount; ++coordinates.bank) {
            // initialize bank
            BankData &bank = channel->getBankData(coordinates);
            bank.demandCount = 0;
            bank.rowBuffer = -1;
        }
    }
}

MemoryController::~MemoryController()
{
    delete channel;
}

bool MemoryController::addTransaction(long clock, CommandType type, Coordinates &coordinates, RequestEntry *request)
{
    TransactionEntry *queueEntry = pendingTransactions_.alloc();

    /* if queue is full return false to indicate failure */
    if(queueEntry == NULL) {
        //memdebug("Transaction queue is full\n");
        return false;
    }

    queueEntry->type = type;
    queueEntry->coordinates = coordinates;
    queueEntry->request = request;
    
    // update dram status
    RankData &rank = channel->getRankData(coordinates);
    BankData &bank = channel->getBankData(coordinates);
    
    rank.demandCount += 1;
    bank.demandCount += 1;
    if (coordinates.row == bank.rowBuffer) {
        bank.supplyCount += 1;
    }
    
    return true;
}

bool MemoryController::addCommand(long clock, CommandType type, Coordinates &coordinates, RequestEntry *request)
{
    int64_t readyTime, issueTime, finishTime;
    
    readyTime = channel->getReadyTime(type, coordinates);
    issueTime = clock;
    if (readyTime > issueTime) return false;
    
    CommandEntry *queueEntry = pendingCommands_.alloc();

    /* if queue is full return false to indicate failure */
    if(queueEntry == NULL) {
        //memdebug("Command queue is full\n");
        return false;
    }
    
    finishTime = channel->getFinishTime(issueTime, type, coordinates);
    
    queueEntry->request     = request;
    queueEntry->type        = type;
    queueEntry->coordinates = coordinates;
    queueEntry->issueTime   = issueTime;
    queueEntry->finishTime  = finishTime;
    
    return true;
}

void MemoryController::schedule(long clock, Signal &accessCompleted_, Signal &missCompleted_)
{
    /** Transaction to Command */
    
    // Refresh policy
    {
        Coordinates coordinates = {0};
        for (coordinates.rank = 0; coordinates.rank < rankcount; ++coordinates.rank) {
            RankData &rank = channel->getRankData(coordinates);
            
            if (clock < rank.refreshTime) continue;
            
            // Power up
            if (rank.is_sleeping) {
                if (!addCommand(clock, COMMAND_powerup, coordinates, NULL)) continue;
                rank.is_sleeping = false;
            }
            
            // Precharge
            for (coordinates.bank = 0; coordinates.bank < bankcount; ++coordinates.bank) {
                BankData &bank = channel->getBankData(coordinates);
                
                if (bank.rowBuffer != -1) {
                    if (!addCommand(clock, COMMAND_precharge, coordinates, NULL)) continue;
                    rank.activeCount -= 1;
                    bank.rowBuffer = -1;
                }
            }
            if (rank.activeCount > 0) continue;
            
            // Refresh
            if (!addCommand(clock, COMMAND_refresh, coordinates, NULL)) continue;
            rank.refreshTime += refresh_interval;
        }
    }
    
    // Schedule policy
    {
        TransactionEntry *transaction;
        foreach_list_mutable(pendingTransactions_.list(), transaction, entry, nextentry) {
            Coordinates &coordinates = transaction->coordinates;
            RankData &rank = channel->getRankData(coordinates);
            BankData &bank = channel->getBankData(coordinates);
            
            // make way for Refresh
            if (clock >= rank.refreshTime) continue;
            
            // Power up
            if (rank.is_sleeping) {
                if (!addCommand(clock, COMMAND_powerup, coordinates, NULL)) continue;
                rank.is_sleeping = false;
            }
            
            // Precharge
            if (bank.rowBuffer != -1 && (bank.rowBuffer != coordinates.row || 
                bank.hitCount >= max_row_hits)) {
                if (bank.rowBuffer != coordinates.row && bank.supplyCount > 0) continue;
                if (!addCommand(clock, COMMAND_precharge, coordinates, NULL)) continue;
                rank.activeCount -= 1;
                bank.rowBuffer = -1;
            }
            
            // Activate
            if (bank.rowBuffer == -1) {
                if (!addCommand(clock, COMMAND_activate, coordinates, NULL)) continue;
                rank.activeCount += 1;
                bank.rowBuffer = coordinates.row;
                bank.hitCount = 0;
                bank.supplyCount = 0;
                
                TransactionEntry *transaction2;
                foreach_list_mutable(pendingTransactions_.list(), transaction2, entry2, nextentry2) {
                    Coordinates &coordinates2 = transaction2->coordinates;
                    if (coordinates2.rank == coordinates.rank && 
                        coordinates2.bank == coordinates.bank && 
                        coordinates2.row  == coordinates.row) {
                        bank.supplyCount += 1;
                    }
                }

                if (transaction->request) {
                    transaction->request->missed = true;
                }
            }
            
            // Read / Write / Migrate
            assert(bank.rowBuffer == coordinates.row);
            assert(bank.supplyCount > 0);
            if (!addCommand(clock, transaction->type, coordinates, transaction->request)) continue;
            rank.demandCount -= 1;
            bank.demandCount -= 1;
            bank.supplyCount -= 1;
            bank.hitCount += 1;

            if (transaction->request) {
                if (transaction->request->missed) {
                    if (coordinates.place % asym_mat_ratio == 0) {
                        memoryCounter.accessCounter.fastSegment += 1;
                    } else {
                        memoryCounter.accessCounter.slowSegment += 1;
                    }
                } else {
                    memoryCounter.accessCounter.rowBuffer += 1;
                }
            }
            
            pendingTransactions_.free(transaction);
        }
    }

    // Precharge policy
    {
        Coordinates coordinates = {0};
        coordinates.channel = channel_id;
        for (coordinates.rank = 0; coordinates.rank < rankcount; ++coordinates.rank) {
            RankData &rank = channel->getRankData(coordinates);
            for (coordinates.bank = 0; coordinates.bank < bankcount; ++coordinates.bank) {
                BankData &bank = channel->getBankData(coordinates);
                
                if (bank.rowBuffer == -1 || bank.demandCount > 0) continue;
                
                int64_t idleTime = clock - max_row_idle;
                if (!addCommand(idleTime, COMMAND_precharge, coordinates, NULL)) continue;
                rank.activeCount -= 1;
                bank.rowBuffer = -1;
            }
        }
    }
    
    // Power down policy
    {
        Coordinates coordinates = {0};
        for (coordinates.rank = 0; coordinates.rank < rankcount; ++coordinates.rank) {
            RankData &rank = channel->getRankData(coordinates);
            
            if (rank.is_sleeping || 
                rank.demandCount > 0 || rank.activeCount > 0 || // rank is serving requests
                clock >= rank.refreshTime // rank is under refreshing
            ) continue;
            
            // Power down
            if (!addCommand(clock, COMMAND_powerdown, coordinates, NULL)) continue;
            rank.is_sleeping = true;
        }
    }
    
    /** Command & Request retirement */
    {
        CommandEntry *command;
        foreach_list_mutable(pendingCommands_.list(), command, entry, nextentry) {
            
            if (clock < command->finishTime) continue; // in-order
            
            switch (command->type) {
                case COMMAND_read:
                case COMMAND_read_precharge:
                case COMMAND_write:
                case COMMAND_write_precharge:
                    if (command->request) {
                        accessCompleted_.emit(command->request);
                    } else if (command->type == COMMAND_read) {
                        missCompleted_.emit(&command->coordinates);
                    }
                    break;
                    
                default:
                    break;
            }
            
            pendingCommands_.free(command);
        }
    }
}

extern ConfigurationParser<PTLsimConfig> config;

MemoryControllerHub::MemoryControllerHub(W8 coreid, const char *name,
        MemoryHierarchy *memoryHierarchy, int type) :
    Controller(coreid, name, memoryHierarchy)
{
    memoryHierarchy_->add_cache_mem_controller(this, true);
    
    int max_row_hits, max_row_idle;
    int asym_det_threshold, asym_det_cache_size;
    int asym_map_cache_size;
    int asym_mat_ratio, asym_mat_group;
    int asym_mat_rcd_ratio, asym_mat_ras_ratio, 
        asym_mat_rp_ratio, asym_mat_wr_ratio, 
        asym_mat_cl_ratio, asym_mat_mig_ratio;
    
    {
        BaseMachine &machine = memoryHierarchy_->get_machine();
#define option(var,opt,val) machine.get_option(name, opt, var) || (var = val)
        option(channelcount, "channel", 1);
        
        option(max_row_hits, "max_row_hits", 4);
        option(max_row_idle, "max_row_idle", 0);

        option(asym_det_threshold, "asym_det_threshold", 4);
        option(asym_det_cache_size, "asym_det_cache_size", 1024);

        option(asym_map_cache_size, "asym_map_cache_size", 4096);

        option(asym_mat_ratio, "asym_mat_ratio", 1);
        option(asym_mat_group, "asym_mat_group", 1);

        option(asym_mat_rcd_ratio, "asym_mat_rcd_ratio", 0);
        option(asym_mat_ras_ratio, "asym_mat_ras_ratio", 0);
        option(asym_mat_rp_ratio, "asym_mat_rp_ratio", 0);
        option(asym_mat_wr_ratio, "asym_mat_wr_ratio", 0);
        option(asym_mat_cl_ratio, "asym_mat_cl_ratio", 0);
        option(asym_mat_mig_ratio, "asym_mat_mig_ratio", 2);
#undef option
    }
    
    {
        assert(asym_det_threshold > 0);
        assert(asym_det_cache_size > 0 && asym_det_cache_size % 4 == 0);
        assert(is_pow_2(asym_mat_ratio));
        assert(is_pow_2(asym_mat_group));
        assert(asym_mat_group % asym_mat_ratio == 0);

        dramconfig = *get_dram_config(type);

        assert(is_pow_2(ram_size));
        assert(is_pow_2(channelcount));

        dramconfig.channelcount = channelcount;
        dramconfig.rankcount    = ram_size / 
                                  dramconfig.ranksize / 
                                  dramconfig.channelcount; // total ranks per channel
        dramconfig.rowcount     = dramconfig.ranksize /
                                  dramconfig.columncount / 
                                  dramconfig.offsetcount / 
                                  dramconfig.bankcount; // total rows per bank
        dramconfig.clustercount = dramconfig.channelcount *
                                  dramconfig.rankcount *
                                  dramconfig.bankcount;
        dramconfig.groupcount   = dramconfig.rowcount / asym_mat_group;
        dramconfig.indexcount   = asym_mat_group;

        //assert(is_pow_2(dramconfig.channelcount));
        assert(is_pow_2(dramconfig.rankcount));
        assert(is_pow_2(dramconfig.rowcount));
        assert(is_pow_2(dramconfig.clustercount));
        assert(is_pow_2(dramconfig.groupcount));
        //assert(is_pow_2(dramconfig.indexcount));
        
        dramconfig.max_row_hits = max_row_hits;
        dramconfig.max_row_idle = max_row_idle;
        
        dramconfig.asym_det_threshold = asym_det_threshold;
        dramconfig.asym_det_cache_size = asym_det_cache_size;
        dramconfig.asym_map_cache_size = asym_map_cache_size;
        dramconfig.asym_mat_ratio = asym_mat_ratio;
        dramconfig.asym_mat_group = asym_mat_group;
        dramconfig.cache_setup(asym_mat_rcd_ratio, asym_mat_ras_ratio, asym_mat_rp_ratio,
            asym_mat_wr_ratio, asym_mat_cl_ratio, asym_mat_mig_ratio);
    }
    
    {
        clock_num = 1000000000L;
        clock_den = dramconfig.clock*config.core_freq_hz;
        clock_rem = 0;
        clock_mem = 0;
    }
    
    mapping = new MemoryMapping(dramconfig);
    
    controller = new MemoryController*[channelcount];
    for (int channel=0; channel<channelcount; ++channel) {
        controller[channel] = new MemoryController(dramconfig, *mapping, channel);
    }
    
    SET_SIGNAL_CB(name, "_Miss_Completed", missCompleted_,
            &MemoryControllerHub::miss_completed_cb);

    SET_SIGNAL_CB(name, "_Access_Completed", accessCompleted_,
            &MemoryControllerHub::access_completed_cb);
    
    SET_SIGNAL_CB(name, "_Wait_Interconnect", waitInterconnect_,
            &MemoryControllerHub::wait_interconnect_cb);
}

MemoryControllerHub::~MemoryControllerHub()
{
    delete mapping;
    for (int channel=0; channel<channelcount; ++channel) {
        delete controller[channel];
    }
    delete [] controller;
}

void MemoryControllerHub::register_interconnect(Interconnect *interconnect, int type)
{
    switch(type) {
        case INTERCONN_TYPE_UPPER:
            cacheInterconnect_ = interconnect;
            break;
        default:
            assert(0);
    }
}

bool MemoryControllerHub::handle_interconnect_cb(void *arg)
{
    Message *message = (Message*)arg;
    
    //memdebug("Received message in Memory controller: ", *message, endl);

    if(message->hasData && message->request->get_type() !=
            MEMORY_OP_UPDATE)
        return true;

    if (message->request->get_type() == MEMORY_OP_EVICT) {
        /* We ignore all the evict messages */
        return true;
    }

    /*
     * if this request is a memory update request then
     * first check the pending queue and see if we have a
     * memory update request to same line and if we can merge
     * those requests then merge them into one request
     */
    if(message->request->get_type() == MEMORY_OP_UPDATE) {
        RequestEntry *entry;
        foreach_list_mutable_backwards(pendingRequests_.list(),
                entry, entry_t, nextentry_t) {
            if(entry->request->get_physical_address() ==
                    message->request->get_physical_address()) {
                /*
                 * found an request for same line, now if this
                 * request is memory update then merge else
                 * don't merge to maintain the serialization
                 * order
                 */
                if(!entry->issued && entry->request->get_type() ==
                        MEMORY_OP_UPDATE) {
                    /*
                     * We can merge the request, so in simulation
                     * we dont have data, so don't do anything
                     */
                    return true;
                }
                /*
                 * we can't merge the request, so do normal
                 * simuation by adding the entry to pending request
                 * queue.
                 */
                break;
            }
        }
    }

    RequestEntry *queueEntry = pendingRequests_.alloc();

    /* if queue is full return false to indicate failure */
    if(queueEntry == NULL) {
        //memdebug("Memory queue is full\n");
        return false;
    }

    if(pendingRequests_.isFull()) {
        memoryHierarchy_->set_controller_full(this, true);
    }

    switch (message->request->get_type()) {
        case MEMORY_OP_UPDATE:
            queueEntry->type = COMMAND_write;
            break;
        case MEMORY_OP_READ:
        case MEMORY_OP_WRITE:
            queueEntry->type = COMMAND_read;
            break;
        default:
            assert(0);
    }
    
    // translate address to dram coordinates
    W64 address = message->request->get_physical_address();
    mapping->extract(address, queueEntry->coordinates);

    queueEntry->request = message->request;
    queueEntry->source = (Controller*)message->origin;

    queueEntry->request->incRefCounter();
    ADD_HISTORY_ADD(queueEntry->request);

    memoryCounter.accessCounter.queueLength += pendingRequests_.count()-1;
    
    return true;
}

void MemoryControllerHub::print(ostream& os) const
{
    os << "---Memory-Controller: ", get_name(), endl;
    os << "Queue: ", pendingRequests_, endl;
    os << "---End Memory-Controller: ", get_name(), endl;
}

bool MemoryControllerHub::access_completed_cb(void *arg)
{
    RequestEntry *queueEntry = (RequestEntry*)arg;
    
    if(!queueEntry->annuled) {

        /* Send response back to cache */
        //memdebug("Memory access done for Request: ", *queueEntry->request, endl);

        wait_interconnect_cb(queueEntry);
    } else {
        queueEntry->request->decRefCounter();
        ADD_HISTORY_REM(queueEntry->request);
        pendingRequests_.free(queueEntry);
    }

    return true;
}

bool MemoryControllerHub::wait_interconnect_cb(void *arg)
{
    RequestEntry *queueEntry = (RequestEntry*)arg;
    
    bool success = false;

    /* Don't send response if its a memory update request */
    switch (queueEntry->request->get_type()) {
        case MEMORY_OP_UPDATE:
            queueEntry->request->decRefCounter();
            ADD_HISTORY_REM(queueEntry->request);
            pendingRequests_.free(queueEntry);
            return true;
        default:
            break;
    }

    /* First send response of the current request */
    Message& message = *memoryHierarchy_->get_message();
    message.sender = this;
    message.dest = queueEntry->source;
    message.request = queueEntry->request;
    message.hasData = true;

    //memdebug("Memory sending message: ", message);
    success = cacheInterconnect_->get_controller_request_signal()->
        emit(&message);
    /* Free the message */
    memoryHierarchy_->free_message(&message);

    if(!success) {
        /* Failed to response to cache, retry after 1 cycle */
        marss_add_event(&waitInterconnect_, 1, queueEntry);
    } else {
        queueEntry->request->decRefCounter();
        ADD_HISTORY_REM(queueEntry->request);
        pendingRequests_.free(queueEntry);

        if(!pendingRequests_.isFull()) {
            memoryHierarchy_->set_controller_full(this, false);
        }
    }
    
    return true;
}

bool MemoryControllerHub::miss_completed_cb(void *arg)
{
    Coordinates *coordinates = (Coordinates*)arg;
    
    W64 tag = coordinates->group;
    assert(mapping_misses.find(tag) != mapping_misses.end());
    mapping_misses.erase(tag);
	mapping->update(tag);

    return true;
}

void MemoryControllerHub::dispatch(long clock)
{
    RequestEntry *request;
    MemoryStatable *mem_stat;

    foreach_list_mutable(pendingRequests_.list(), request, entry, nextentry) {
        mem_stat = request->request->get_memoryStat();

        if (!request->translated) {
            if (!mapping->translate(request->coordinates)) {
                W64 tag = mapping->make_forward_tag(request->coordinates);
                if (mapping_misses.find(tag) == mapping_misses.end() &&
                    mapping_misses.size() < 8) {
                    Coordinates tag_coordinates;
                    mapping->extract(tag, tag_coordinates);
                    tag_coordinates.row += dramconfig.rowcount;
                    tag_coordinates.group = tag;
                    tag_coordinates.place = 0;

                    MemoryController *mc = controller[tag_coordinates.channel];
                    if (!mc->addTransaction(clock, COMMAND_read, tag_coordinates, NULL)) continue;
					mapping_misses[tag] = 0;
                }
                continue;
            }

            request->detected = mapping->detect(request->coordinates);

            memoryCounter.accessCounter.count += 1;
            if (!mapping->allocate(request->coordinates)) {
                memoryCounter.rowCounter.count += 1;
            }

            request->translated = true;
        }
        
        if (!request->issued) {
            MemoryController *mc = controller[request->coordinates.channel];
            if (!mc->addTransaction(clock, request->type, request->coordinates, request)) continue;

            request->issued = true;
        }
        
        if (request->detected) {
            if (!request->updated) {
                W64 tag = mapping->make_forward_tag(request->coordinates);
                Coordinates tag_coordinates;
                mapping->extract(tag, tag_coordinates);
                tag_coordinates.row += dramconfig.rowcount;
                tag_coordinates.place = 0;

                MemoryController *mc = controller[tag_coordinates.channel];
                if (!mc->addTransaction(clock, COMMAND_write, tag_coordinates, NULL)) continue;

                request->updated = true;
            }

            MemoryController *mc = controller[request->coordinates.channel];
            if (!mc->addTransaction(clock, COMMAND_migrate, request->coordinates, NULL)) continue;

            memoryCounter.rowCounter.migration += 1;
            if (mapping->promote(clock, request->coordinates)) {
                memoryCounter.rowCounter.remigration += 1;
            }

            request->detected = false;
        }
    }
}

void MemoryControllerHub::cycle()
{
    clock_rem += clock_num;
    while (clock_rem >= clock_den) {
        dispatch(clock_mem);
        for (int channel=0; channel<channelcount; ++channel) {
            controller[channel]->channel->cycle(clock_mem);
            controller[channel]->schedule(clock_mem, accessCompleted_, missCompleted_);
        }
        clock_mem += 1;
        clock_rem -= clock_den;
    }
}

void MemoryControllerHub::annul_request(MemoryRequest *request)
{
    RequestEntry *queueEntry;
    foreach_list_mutable(pendingRequests_.list(), queueEntry,
            entry, nextentry) {
        if(queueEntry->request->is_same(request)) {
            queueEntry->annuled = true;
            if(!queueEntry->issued) {
                queueEntry->request->decRefCounter();
                ADD_HISTORY_REM(queueEntry->request);
                pendingRequests_.free(queueEntry);
            }
        }
    }
}

int MemoryControllerHub::get_no_pending_request(W8 coreid)
{
    int count = 0;
    RequestEntry *queueEntry;
    foreach_list_mutable(pendingRequests_.list(), queueEntry,
            entry, nextentry) {
        if(queueEntry->request->get_coreid() == coreid)
            count++;
    }
    return count;
}

/**
 * @brief Dump Memory Controller in YAML Format
 *
 * @param out YAML Object
 */
void MemoryControllerHub::dump_configuration(YAML::Emitter &out) const
{
    out << YAML::Key << get_name() << YAML::Value << YAML::BeginMap;

    YAML_KEY_VAL(out, "type", "dram_module");
    YAML_KEY_VAL(out, "RAM_size", ram_size); /* ram_size is from QEMU */
    //YAML_KEY_VAL(out, "pending_queue_size", pendingRequests_.size());

    out << YAML::EndMap;
}

/* Memory Controller Builder */
struct MemoryControllerHubBuilder : public ControllerBuilder
{
    MemoryControllerHubBuilder(const char* name) :
        ControllerBuilder(name)
    {}

    Controller* get_new_controller(W8 coreid, W8 type,
            MemoryHierarchy& mem, const char *name) {
        return new MemoryControllerHub(coreid, name, &mem, type);
    }
};

MemoryControllerHubBuilder memoryControllerHubBuilder("dram_module");
