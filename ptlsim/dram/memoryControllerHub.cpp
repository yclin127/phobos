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
    MemoryCounter memoryCounter;
    //MemoryDistribution memoryDistribution;
};

using namespace DRAM;

extern ConfigurationParser<PTLsimConfig> config;

MemoryControllerHub::MemoryControllerHub(W8 coreid, const char *name,
        MemoryHierarchy *memoryHierarchy, int type) :
    Controller(coreid, name, memoryHierarchy)
{
    memoryHierarchy_->add_cache_mem_controller(this, true);
    
    int max_row_hits, max_row_idle;
    int asym_det_cache_size, asym_det_threshold;
    int asym_map_profiling, asym_map_on_chip;
    int asym_map_cache_size, asym_map_cache_compact;
    int asym_mat_ratio, asym_mat_group, asym_rep_order, asym_rep_last;

    int asym_mat_mig_percent, asym_mat_ap_percent;
    int asym_mat_rcd_percent, asym_mat_ras_percent, 
        asym_mat_rp_percent, asym_mat_wr_percent, 
        asym_mat_cl_percent;
    int asym_mat_rcd_percent2, asym_mat_ras_percent2, 
        asym_mat_rp_percent2, asym_mat_wr_percent2, 
        asym_mat_cl_percent2;
    
    {
        BaseMachine &machine = memoryHierarchy_->get_machine();
#define option(var,opt,val) machine.get_option(name, opt, var) || (var = val)
        option(channelcount, "channel", 1);

        option(max_row_idle, "max_row_idle", 0);
        option(max_row_hits, "max_row_hits", 4);

        option(asym_det_cache_size, "asym_det_cache_size", 1024);
        option(asym_det_threshold, "asym_det_threshold", 1);

        option(asym_map_profiling, "asym_map_profiling", 0);
        option(asym_map_on_chip, "asym_map_on_chip", 0);
        option(asym_map_cache_size, "asym_map_cache_size", 256);
        asym_map_cache_size *= 1024;
        option(asym_map_cache_compact, "asym_map_cache_compact", 1);

        option(asym_mat_ratio, "asym_mat_ratio", 1);
        option(asym_mat_group, "asym_mat_group", 64);
        option(asym_rep_order, "asym_rep_order", 0);
        option(asym_rep_last, "asym_rep_last", 0);

        option(asym_mat_mig_percent, "asym_mat_mig_percent", 300);
        option(asym_mat_ap_percent, "asym_mat_ap_percent", 50);

        option(asym_mat_rcd_percent, "asym_mat_rcd_percent", 100);
        option(asym_mat_ras_percent, "asym_mat_ras_percent", 100);
        option(asym_mat_rp_percent, "asym_mat_rp_percent", 100);
        option(asym_mat_wr_percent, "asym_mat_wr_percent", 100);
        option(asym_mat_cl_percent, "asym_mat_cl_percent", 100);

        option(asym_mat_rcd_percent2, "asym_mat_rcd_percent2", 100);
        option(asym_mat_ras_percent2, "asym_mat_ras_percent2", 100);
        option(asym_mat_rp_percent2, "asym_mat_rp_percent2", 100);
        option(asym_mat_wr_percent2, "asym_mat_wr_percent2", 100);
        option(asym_mat_cl_percent2, "asym_mat_cl_percent2", 100);
#undef option
    }
    
    {
        assert(asym_det_threshold > 0);
        assert(asym_det_cache_size > 0 && asym_det_cache_size % 4 == 0);
        assert(is_pow_of_2(asym_mat_ratio));
        assert(is_pow_of_2(asym_mat_group));
        assert(asym_mat_group % asym_mat_ratio == 0);

        dramconfig = *get_dram_config(type);

        assert(is_pow_of_2(ram_size));
        assert(is_pow_of_2(channelcount));

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

        //assert(is_pow_of_2(dramconfig.channelcount));
        assert(is_pow_of_2(dramconfig.rankcount));
        assert(is_pow_of_2(dramconfig.rowcount));
        assert(is_pow_of_2(dramconfig.clustercount));
        assert(is_pow_of_2(dramconfig.groupcount));
        //assert(is_pow_of_2(dramconfig.indexcount));

        dramconfig.max_row_idle = max_row_idle;
        dramconfig.max_row_hits = max_row_hits;

        dramconfig.asym_det_cache_size = asym_det_cache_size;
        dramconfig.asym_det_threshold = asym_det_threshold;

        dramconfig.asym_map_profiling = asym_map_profiling;
        dramconfig.asym_map_on_chip = asym_map_on_chip;
        dramconfig.asym_map_cache_size = asym_map_cache_size;
        dramconfig.asym_map_cache_compact = asym_map_cache_compact;

        dramconfig.asym_mat_ratio = asym_mat_ratio;
        dramconfig.asym_mat_group = asym_mat_group;
        dramconfig.asym_rep_order = asym_rep_order;
        dramconfig.asym_rep_last = asym_rep_last;

        dramconfig.cache_setup(asym_mat_mig_percent, asym_mat_ap_percent, 
            asym_mat_rcd_percent, asym_mat_ras_percent, asym_mat_rp_percent, 
            asym_mat_wr_percent, asym_mat_cl_percent,
            asym_mat_rcd_percent2, asym_mat_ras_percent2, asym_mat_rp_percent2, 
            asym_mat_wr_percent2, asym_mat_cl_percent2);
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
    
    SET_SIGNAL_CB(name, "_Lookup_Completed", lookupCompleted_,
            &MemoryControllerHub::lookup_completed_cb);
    
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
                if(!entry->stage <= STAGE_issue && entry->request->get_type() ==
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

    issue(queueEntry, message->request->get_init_insns(), message->request->get_physical_address());

    queueEntry->request = message->request;
    queueEntry->source = (Controller*)message->origin;

    queueEntry->request->incRefCounter();
    ADD_HISTORY_ADD(queueEntry->request);
    
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
    
    if(queueEntry->stage != STAGE_annul) {

        /* Send response back to cache */
        //memdebug("Memory access done for Request: ", *queueEntry->request, endl);

        wait_interconnect_cb(queueEntry);
    } else {
        //retire(queueEntry);
        queueEntry->respounded = true;
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
            //retire(queueEntry);
            queueEntry->respounded = true;
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
        //retire(queueEntry);
        queueEntry->respounded = true;
    }
    
    return true;
}

void MemoryControllerHub::issue(RequestEntry *request, int cycle, W64 address)
{
    // address mapping
    mapping->translate(address, request->coordinates);
    // update statistics
    if (!mapping->allocate(cycle, request->coordinates)) {
        memoryCounter.rowCounter.count += 1;
    }
    // bypass translation if there's no fast level
    if (dramconfig.asym_mat_ratio == 1) {
        request->stage = STAGE_issue;
    } else {
        request->stage = STAGE_translate;
    }
}

void MemoryControllerHub::retire(RequestEntry *request)
{
    request->request->decRefCounter();
    ADD_HISTORY_REM(request->request);
    pendingRequests_.free(request);
    
    if(!pendingRequests_.isFull()) {
        memoryHierarchy_->set_controller_full(this, false);
    }
}

void MemoryControllerHub::dispatch(long clock)
{
    RequestEntry *queueEntry;

    foreach_list_mutable(pendingRequests_.list(), queueEntry, entry, nextentry) {
        switch (queueEntry->stage) {
            case STAGE_translate:
            {
                if (!mapping->probe(queueEntry->coordinates)) {
                    W64 tag = queueEntry->coordinates.tag;
                    if (lookup_queue.find(tag) == lookup_queue.end()) {
                        Coordinates tag_coordinates;

                        mapping->translate(tag, tag_coordinates);
                        tag_coordinates.row += dramconfig.rowcount;
                        tag_coordinates.place = 0;
                        tag_coordinates.tag = tag;

                        MemoryController *mc = controller[tag_coordinates.channel];
                        if (!mc->addTransaction(clock, COMMAND_read, tag_coordinates, NULL)) continue;

                        lookup_queue[tag] = clock;
                        memoryCounter.rowCounter.query += 1;
                    }
                    continue;
                }

                queueEntry->stage = STAGE_issue;
            }
            break;

            case STAGE_issue:
            {
                MemoryController *mc = controller[queueEntry->coordinates.channel];
                if (!mc->addTransaction(clock, queueEntry->type, queueEntry->coordinates, queueEntry)) continue;
                
                if (mapping->detect(queueEntry->coordinates)) {
                    queueEntry->stage = STAGE_update;
                } else {
                    queueEntry->stage = STAGE_finish;
                }
            }
            break;

            case STAGE_update:
            {
                W64 tag = queueEntry->coordinates.tag;
                Coordinates tag_coordinates;

                mapping->translate(tag, tag_coordinates);
                tag_coordinates.row += dramconfig.rowcount;
                tag_coordinates.place = 0;

                MemoryController *mc = controller[tag_coordinates.channel];
                if (!mc->addTransaction(clock, COMMAND_write, tag_coordinates, NULL)) continue;

                queueEntry->stage = STAGE_migrate;
            }
            break;

            case STAGE_migrate:
            {
                MemoryController *mc = controller[queueEntry->coordinates.channel];
                if (!mc->addTransaction(clock, COMMAND_migrate, queueEntry->coordinates, NULL)) continue;

                memoryCounter.rowCounter.migration += 1;
                if (mapping->promote(clock, queueEntry->coordinates)) {
                    memoryCounter.rowCounter.remigration += 1;
                }

                queueEntry->stage = STAGE_finish;
            }
            break;

            case STAGE_annul:
            case STAGE_finish:
            {
                if (queueEntry->respounded) {
                    retire(queueEntry);
                }
            }
            break;

            case STAGE_unknown:
            {
                assert(queueEntry->stage != STAGE_unknown);
            }
            break;
        }
    }
}

bool MemoryControllerHub::lookup_completed_cb(void *arg)
{
    Coordinates *coordinates = (Coordinates*)arg;

    W64 tag = coordinates->tag;

    assert(lookup_queue.find(tag) != lookup_queue.end());
    lookup_queue.erase(tag);

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
            controller[channel]->schedule(clock_mem, accessCompleted_, lookupCompleted_);
        }
        clock_mem += 1;
        clock_rem -= clock_den;

        /*memoryCounter.energyCounter.actPre = 0;
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
        }*/
    }
}

void MemoryControllerHub::annul_request(MemoryRequest *request)
{
    RequestEntry *queueEntry;
    foreach_list_mutable(pendingRequests_.list(), queueEntry,
            entry, nextentry) {
        if(queueEntry->request->is_same(request)) {
            queueEntry->stage = STAGE_annul;
            if(queueEntry->stage <= STAGE_issue) {
                retire(queueEntry);
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
