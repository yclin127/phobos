#ifdef MEM_TEST
#include <test.h>
#else
#include <ptlsim.h>
#define PTLSIM_PUBLIC_ONLY
#include <ptlhwdef.h>
#endif

#include <memoryControllerHub.h>
#include <memoryStatistics.h>

#include <memoryHierarchy.h>
#include <machine.h>


namespace DRAM {
    MemoryCounter memoryCounter = {
        .accessCounter = {0},
        .rowCounter = {0},
    };
};

using namespace DRAM;

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
    int asym_mat_ap_ratio;
    
    {
        BaseMachine &machine = memoryHierarchy_->get_machine();
#define option(var,opt,val) machine.get_option(name, opt, var) || (var = val)
        option(channelcount, "channel", 1);
        
        option(max_row_hits, "max_row_hits", 4);
        option(max_row_idle, "max_row_idle", 0);

        option(asym_det_threshold, "asym_det_threshold", 4);
        option(asym_det_cache_size, "asym_det_cache_size", 1024);

        option(asym_map_cache_size, "asym_map_cache_size", 256);
        asym_map_cache_size *= 1024;

        option(asym_mat_ratio, "asym_mat_ratio", 1);
        option(asym_mat_group, "asym_mat_group", 1);

        option(asym_mat_rcd_ratio, "asym_mat_rcd_ratio", 0);
        option(asym_mat_ras_ratio, "asym_mat_ras_ratio", 0);
        option(asym_mat_rp_ratio, "asym_mat_rp_ratio", 0);
        option(asym_mat_wr_ratio, "asym_mat_wr_ratio", 0);
        option(asym_mat_cl_ratio, "asym_mat_cl_ratio", 0);
        option(asym_mat_mig_ratio, "asym_mat_mig_ratio", 3);
        option(asym_mat_ap_ratio, "asym_mat_ap_ratio", 2);
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
            asym_mat_wr_ratio, asym_mat_cl_ratio, asym_mat_mig_ratio, asym_mat_ap_ratio);
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

void MemoryControllerHub::dispatch(long clock)
{
    RequestEntry *request;
    MemoryStatable *mem_stat;

    foreach_list_mutable(pendingRequests_.list(), request, entry, nextentry) {
        mem_stat = request->request->get_memoryStat();

        if (!request->translated) {
            if (!mapping->translate(request->coordinates)) {
                W64 tag = mapping->make_index_tag(request->coordinates);
                if (mapping_misses.find(tag) == mapping_misses.end() &&
                    mapping_misses.size() < 8) {
                    Coordinates tag_coordinates;
                    mapping->extract(tag, tag_coordinates);
                    tag_coordinates.row += dramconfig.rowcount;
                    tag_coordinates.group = tag;
                    tag_coordinates.place = 0;

                    MemoryController *mc = controller[tag_coordinates.channel];
                    if (!mc->addTransaction(clock, COMMAND_read, tag_coordinates, NULL)) continue;
                    mapping_misses[tag] = clock;
                    memoryCounter.rowCounter.query += 1;
                }
                continue;
            }

            request->detected = mapping->detect(request->coordinates);
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
                W64 tag = mapping->make_index_tag(request->coordinates);
                Coordinates tag_coordinates;
                mapping->extract(tag, tag_coordinates);
                tag_coordinates.row += dramconfig.rowcount;
                tag_coordinates.group = tag;
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

bool MemoryControllerHub::miss_completed_cb(void *arg)
{
    Coordinates *coordinates = (Coordinates*)arg;
    
    W64 tag = coordinates->group;
    assert(mapping_misses.find(tag) != mapping_misses.end());
    mapping_misses.erase(tag);
    mapping->update(tag);

    return true;
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

        memoryCounter.energyCounter.actPre = 0;
        memoryCounter.energyCounter.read = 0;
        memoryCounter.energyCounter.write = 0;
        memoryCounter.energyCounter.refresh = 0;
        memoryCounter.energyCounter.migrate = 0;
        memoryCounter.energyCounter.background = 0;
        memoryCounter.energyCounter.total = 0;
        for (int channel=0; channel<channelcount; ++channel) {
            memoryCounter.energyCounter.actPre += 
                controller[channel]->channel->getEnergy(ENERGY_actPre);
            memoryCounter.energyCounter.read += 
                controller[channel]->channel->getEnergy(ENERGY_read);
            memoryCounter.energyCounter.write += 
                controller[channel]->channel->getEnergy(ENERGY_write);
            memoryCounter.energyCounter.refresh += 
                controller[channel]->channel->getEnergy(ENERGY_refresh);
            memoryCounter.energyCounter.migrate += 
                controller[channel]->channel->getEnergy(ENERGY_migrate);
            memoryCounter.energyCounter.background += 
                controller[channel]->channel->getEnergy(ENERGY_background);
            memoryCounter.energyCounter.total += 
                controller[channel]->channel->getEnergy(ENERGY_total);
        }
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
