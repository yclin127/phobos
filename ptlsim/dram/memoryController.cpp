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
#include "memoryModule.h"

using namespace DRAM;

MemoryController::MemoryController(Config &config, AddressMapping &mapping, Policy &policy) :
    mapping(mapping), policy(policy)
{
    asym_mat_group = config.asym_mat_group;
    asym_mat_ratio = config.asym_mat_ratio;
    
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
            bank.remapping.init(groupcount, indexcount, asym_mat_ratio);
        }
    }
}

MemoryController::~MemoryController()
{
    delete channel;
}

void MemoryController::translate(W64 address, Coordinates &coordinates)
{
    /** Address mapping scheme goes here. */
    
    coordinates.channel = mapping.channel.value(address);
    coordinates.rank    = mapping.rank.value(address);
    coordinates.bank    = mapping.bank.value(address);
    coordinates.row     = mapping.row.value(address);
    coordinates.column  = mapping.column.value(address);
    coordinates.offset  = mapping.offset.value(address);
    
    coordinates.group   = mapping.group.value(address);
    coordinates.index   = mapping.index.value(address);
}

bool MemoryController::addTransaction(long clock, RequestEntry *request)
{
    TransactionEntry *queueEntry = pendingTransactions_.alloc();

    /* if queue is full return false to indicate failure */
    if(queueEntry == NULL) {
        //memdebug("Transaction queue is full\n");
        return false;
    }

    queueEntry->request = request;
    
    W64 address = request->request->get_physical_address();
    Coordinates &coordinates = queueEntry->coordinates;
    
    translate(address, coordinates);
    
    RankData &rank = channel->getRankData(coordinates);
    BankData &bank = channel->getBankData(coordinates);
    
    if (request->request->get_type() == MEMORY_OP_MIGRATE) {
        coordinates.column = 0;
        coordinates.offset = request->request->get_virtual_address();
        
        total_migs_committed += 1;
    } else {
        total_accs_committed += 1;
        if (bank.remapping.cached(coordinates)) {
            total_caps_committed += 1;
        }
    }
    
    rank.demandCount += 1;
    bank.demandCount += 1;
    if (coordinates.row == bank.rowBuffer) {
        bank.supplyCount += 1;
    }

    if (asym_mat_ratio > 0 && request->type == COMMAND_migrate) {
        bank.remapping.swap(coordinates.group, coordinates.index, coordinates.offset);
    }
    
    return true;
}

bool MemoryController::addCommand(long clock, CommandType type, Coordinates *coordinates, RequestEntry *request)
{
    int64_t readyTime, issueTime, finishTime;
    
    readyTime = channel->getReadyTime(type, *coordinates);
    issueTime = clock;
    if (readyTime > issueTime) return false;
    
    CommandEntry *queueEntry = pendingCommands_.alloc();

    /* if queue is full return false to indicate failure */
    if(queueEntry == NULL) {
        //memdebug("Command queue is full\n");
        return false;
    }
    
    finishTime = channel->getFinishTime(issueTime, type, *coordinates);
    
    queueEntry->request     = request;
    queueEntry->type        = type;
    queueEntry->coordinates = *coordinates;
    queueEntry->issueTime   = issueTime;
    queueEntry->finishTime  = finishTime;
    
    return true;
}

void MemoryController::doScheduling(long clock, Signal &accessCompleted_)
{
    /** Request to Transaction */
    
    {
        RequestEntry *request;
        foreach_list_mutable(pendingRequests_.list(), request, entry, nextentry) {
            if (!request->issued) {
                if (!addTransaction(clock, request)) break; // in-order
                request->issued = true;
            }
        }
    }
    
    /** Transaction to Command */
    
    {
        // Refresh policy
        Coordinates coordinates = {0};
        for (coordinates.rank = 0; coordinates.rank < rankcount; ++coordinates.rank) {
            RankData &rank = channel->getRankData(coordinates);
            
            if (clock < rank.refreshTime) continue;
            
            // Power up
            if (rank.is_sleeping) {
                if (!addCommand(clock, COMMAND_powerup, &coordinates, NULL)) continue;
                rank.is_sleeping = false;
            }
            
            // Precharge
            for (coordinates.bank = 0; coordinates.bank < bankcount; ++coordinates.bank) {
                BankData &bank = channel->getBankData(coordinates);
                
                if (bank.rowBuffer != -1) {
                    if (!addCommand(clock, COMMAND_precharge, &coordinates, NULL)) continue;
                    rank.activeCount -= 1;
                    bank.rowBuffer = -1;
                }
            }
            if (rank.activeCount > 0) continue;
            
            // Refresh
            if (!addCommand(clock, COMMAND_refresh, &coordinates, NULL)) continue;
            rank.refreshTime += refresh_interval;
        }
    }
    
    // Schedule policy
    {
        TransactionEntry *transaction;
        foreach_list_mutable(pendingTransactions_.list(), transaction, entry, nextentry) {
            Coordinates *coordinates = &transaction->coordinates;
            RankData &rank = channel->getRankData(*coordinates);
            BankData &bank = channel->getBankData(*coordinates);
            
            // make way for Refresh
            if (clock >= rank.refreshTime) continue;
            
            // Power up
            if (rank.is_sleeping) {
                if (!addCommand(clock, COMMAND_powerup, coordinates, NULL)) continue;
                rank.is_sleeping = false;
            }
            
            // Precharge
            if (bank.rowBuffer != -1 && (bank.rowBuffer != coordinates->row || 
                bank.hitCount >= policy.max_row_hits)) {
                if (bank.rowBuffer != coordinates->row && bank.supplyCount > 0) continue;
                if (!addCommand(clock, COMMAND_precharge, coordinates, NULL)) continue;
                rank.activeCount -= 1;
                bank.rowBuffer = -1;
            }
            
            // Activate
            if (bank.rowBuffer == -1) {
                if (!addCommand(clock, COMMAND_activate, coordinates, NULL)) continue;
                rank.activeCount += 1;
                bank.rowBuffer = coordinates->row;
                bank.hitCount = 0;
                bank.supplyCount = 0;
                
                TransactionEntry *transaction2;
                foreach_list_mutable(pendingTransactions_.list(), transaction2, entry2, nextentry2) {
                    Coordinates *coordinates2 = &transaction2->coordinates;
                    if (coordinates2->rank == coordinates->rank && 
                        coordinates2->bank == coordinates->bank && 
                        coordinates2->row  == coordinates->row) {
                        bank.supplyCount += 1;
                    }
                }
            }
            
            // Read / Write / Migrate
            assert(bank.rowBuffer == coordinates->row);
            assert(bank.supplyCount > 0);
            if (!addCommand(clock, transaction->request->type, coordinates, transaction->request)) continue;
            rank.demandCount -= 1;
            bank.demandCount -= 1;
            bank.supplyCount -= 1;
            bank.hitCount += 1;
            
            pendingTransactions_.free(transaction);
        }
    }

    // Precharge policy
    {
        Coordinates coordinates = {0};
        for (coordinates.rank = 0; coordinates.rank < rankcount; ++coordinates.rank) {
            RankData &rank = channel->getRankData(coordinates);
            for (coordinates.bank = 0; coordinates.bank < bankcount; ++coordinates.bank) {
                BankData &bank = channel->getBankData(coordinates);
                
                if (bank.rowBuffer == -1 || bank.demandCount > 0) continue;
                
                int64_t idleTime = clock - policy.max_row_idle;
                if (!addCommand(idleTime, COMMAND_precharge, &coordinates, NULL)) continue;
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
            if (!addCommand(clock, COMMAND_powerdown, &coordinates, NULL)) continue;
            rank.is_sleeping = true;
        }
    }
    
    /** Command & Request retirement */
    {
        CommandEntry *command;
        foreach_list_mutable(pendingCommands_.list(), command, entry, nextentry) {
            
            if (clock < command->issueTime) continue; // in-order
            
            switch (command->type) {
                case COMMAND_read:
                case COMMAND_read_precharge:
                case COMMAND_write:
                case COMMAND_write_precharge:
                case COMMAND_migrate:
                    marss_add_event(&accessCompleted_, command->finishTime-clock, command->request);
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
    memoryHierarchy_->add_mem_controller(this);
    
    int asym_mat_group, asym_mat_ratio;
    int asym_mat_row_speedup, asym_mat_col_speedup;
    int asym_threshold;
    
    {
        BaseMachine &machine = memoryHierarchy_->get_machine();
#define option(var,opt,val) machine.get_option(name, opt, var) || (var = val)
        option(channelcount, "channel", 1);
        option(policy.max_row_hits, "max_row_hits", 4);
        option(policy.max_row_idle, "max_row_idle", 0);
        option(asym_mat_group, "asym_mat_group", 1);
        option(asym_mat_ratio, "asym_mat_ratio", 0);
        option(asym_mat_row_speedup, "asym_mat_row_speedup", 0);
        option(asym_mat_col_speedup, "asym_mat_col_speedup", 0);
        option(asym_threshold, "asym_threshold", 4);
#undef option
    }
    
    {
        dramconfig = *get_dram_config(type);
        
        dramconfig.channelcount = channelcount;
        dramconfig.rankcount = ram_size/dramconfig.ranksize/dramconfig.channelcount;
        dramconfig.rowcount = dramconfig.ranksize/dramconfig.bankcount/dramconfig.columncount/dramconfig.offsetcount;
        dramconfig.groupcount = dramconfig.rowcount/asym_mat_group;
        dramconfig.indexcount = asym_mat_group;
        
        dramconfig.asym_mat_group = asym_mat_group;
        dramconfig.asym_mat_ratio = asym_mat_ratio;
        dramconfig.cache_setup(asym_mat_row_speedup, asym_mat_col_speedup);
    }
    
    {
        clock_num = 1000000000L;
        clock_den = dramconfig.clock*config.core_freq_hz;
        clock_rem = 0;
        clock_mem = 0;
    
        int shift = 0;
        mapping.offset.shift  = shift; shift +=
        mapping.offset.width  = log_2(dramconfig.offsetcount);
        mapping.column.shift  = shift; shift +=
        mapping.column.width  = log_2(dramconfig.columncount);
        mapping.channel.shift = shift; shift +=
        mapping.channel.width = log_2(dramconfig.channelcount);
        mapping.bank.shift    = shift; shift +=
        mapping.bank.width    = log_2(dramconfig.bankcount);
        mapping.rank.shift    = shift; shift +=
        mapping.rank.width    = log_2(dramconfig.rankcount);
        mapping.row.shift     = shift; shift +=
        mapping.row.width     = log_2(dramconfig.rowcount);
        
        shift = mapping.row.shift;
        mapping.group.shift   = shift; shift +=
        mapping.group.width   = log_2(dramconfig.groupcount);
        mapping.index.shift   = shift; shift +=
        mapping.index.width   = log_2(dramconfig.indexcount);
    }
    
    controller = new MemoryController*[channelcount];
    for (int channel=0; channel<channelcount; ++channel) {
        controller[channel] = new MemoryController(dramconfig, mapping, policy);
    }
    
    SET_SIGNAL_CB(name, "_Access_Completed", accessCompleted_,
            &MemoryControllerHub::access_completed_cb);
    
    SET_SIGNAL_CB(name, "_Wait_Interconnect", waitInterconnect_,
            &MemoryControllerHub::wait_interconnect_cb);
    
    threshold = asym_threshold;
    victim = 0;
}

MemoryControllerHub::~MemoryControllerHub()
{
    for (int channel=0; channel<channelcount; ++channel) {
        delete controller[channel];
    }
    delete [] controller;
}

bool MemoryControllerHub::is_movable(W64 address)
{
    if (dramconfig.asym_mat_ratio == 0)
        return false;
    
    Coordinates coordinates;
    int channel = mapping.channel.value(address);
    controller[channel]->translate(address, coordinates);
    BankData &bank = controller[channel]->channel->getBankData(coordinates);
    
    return !bank.remapping.cached(coordinates);
}

int MemoryControllerHub::next_victim(W64 address)
{
    int result = victim;
    victim = mapping.index.clamp(victim + dramconfig.asym_mat_ratio);
    return result;
}

int MemoryControllerHub::get_threshold()
{
    return threshold;
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
    
    int channel = mapping.channel.value(message->request->get_physical_address());

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
        foreach_list_mutable_backwards(controller[channel]->pendingRequests_.list(),
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

    RequestEntry *queueEntry = controller[channel]->pendingRequests_.alloc();

    /* if queue is full return false to indicate failure */
    if(queueEntry == NULL) {
        //memdebug("Memory queue is full\n");
        return false;
    }

    if(controller[channel]->pendingRequests_.isFull()) {
        memoryHierarchy_->set_controller_full(this, true);
    }

    queueEntry->request = message->request;
    queueEntry->source = (Controller*)message->origin;

    queueEntry->request->incRefCounter();
    ADD_HISTORY_ADD(queueEntry->request);
    
    /* yclin */
    switch (message->request->get_type()) {
        case MEMORY_OP_UPDATE:
            queueEntry->type = COMMAND_write;
            break;
        case MEMORY_OP_READ:
        case MEMORY_OP_WRITE:
            queueEntry->type = COMMAND_read;
            if (message->request->get_coreSignal2()) {
                message->request->get_coreSignal2()->emit((void*)message->request);
            }
            break;
        case MEMORY_OP_MIGRATE:
            queueEntry->type = COMMAND_migrate;
            break;
        default:
            assert(0);
    }
    /* yclin */
    
    return true;
}

void MemoryControllerHub::print(ostream& os) const
{
    os << "---Memory-Controller: ", get_name(), endl;
    for (int channel=0; channel<channelcount; ++channel) {
        os << "Queue ", channel, ": ", controller[channel]->pendingRequests_, endl;
    }
    os << "---End Memory-Controller: ", get_name(), endl;
}

bool MemoryControllerHub::access_completed_cb(void *arg)
{
    RequestEntry *queueEntry = (RequestEntry*)arg;
    
    int channel = mapping.channel.value(queueEntry->request->get_physical_address());

    if(!queueEntry->annuled) {

        /* Send response back to cache */
        //memdebug("Memory access done for Request: ", *queueEntry->request, endl);

        wait_interconnect_cb(queueEntry);
    } else {
        queueEntry->request->decRefCounter();
        ADD_HISTORY_REM(queueEntry->request);
        controller[channel]->pendingRequests_.free(queueEntry);
    }

    return true;
}

bool MemoryControllerHub::wait_interconnect_cb(void *arg)
{
    RequestEntry *queueEntry = (RequestEntry*)arg;
    
    int channel = mapping.channel.value(queueEntry->request->get_physical_address());

    bool success = false;

    /* Don't send response if its a memory update request */
    switch (queueEntry->request->get_type()) {
        case MEMORY_OP_UPDATE:
        case MEMORY_OP_MIGRATE: /* yclin */
            queueEntry->request->decRefCounter();
            ADD_HISTORY_REM(queueEntry->request);
            controller[channel]->pendingRequests_.free(queueEntry);
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
        controller[channel]->pendingRequests_.free(queueEntry);

        if(!controller[channel]->pendingRequests_.isFull()) {
            memoryHierarchy_->set_controller_full(this, false);
        }
    }
    return true;
}

void MemoryControllerHub::clock()
{
    clock_rem += clock_num;
    if (clock_rem >= clock_den) {
        for (int channel=0; channel<channelcount; ++channel) {
            controller[channel]->channel->cycle(clock_mem);
            controller[channel]->doScheduling(clock_mem, accessCompleted_);
        }
        clock_mem += 1;
        clock_rem -= clock_den;
    }
}

void MemoryControllerHub::annul_request(MemoryRequest *request)
{
    int channel = mapping.channel.value(request->get_physical_address());
    
    RequestEntry *queueEntry;
    foreach_list_mutable(controller[channel]->pendingRequests_.list(), queueEntry,
            entry, nextentry) {
        if(queueEntry->request->is_same(request)) {
            queueEntry->annuled = true;
            if(!queueEntry->issued) {
                queueEntry->request->decRefCounter();
                ADD_HISTORY_REM(queueEntry->request);
                controller[channel]->pendingRequests_.free(queueEntry);
            }
        }
    }
}

int MemoryControllerHub::get_no_pending_request(W8 coreid)
{
    int count = 0;
    RequestEntry *queueEntry;
    for (int channel=0; channel<channelcount; ++channel) {
        foreach_list_mutable(controller[channel]->pendingRequests_.list(), queueEntry,
                entry, nextentry) {
            if(queueEntry->request->get_coreid() == coreid)
                count++;
        }
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

MemoryControllerHubBuilder memControllerBuilder("dram_module");
