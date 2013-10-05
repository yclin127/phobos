#ifndef MEMORY_CONTROLLER_H
#define MEMORY_CONTROLLER_H

#include <controller.h>
#include <interconnect.h>
#include <superstl.h>

#include <memoryModule.h>

using namespace Memory;

namespace DRAM {

struct Policy {
    int max_row_idle;
    int max_row_hits;
};



struct RequestEntry : public FixStateListObject
{
    MemoryRequest *request;
    Controller *source;
    bool annuled;
    bool issued;

    void init() {
        request = NULL;
        annuled = false;
        issued = false;
    }

    ostream& print(ostream &os) const {
        if(request)
            os << "Request{", *request, "} ";
        if (source)
            os << "source[", source->get_name(), "] ";
        os << "annuled[", annuled, "] ";
        os << "issued[", issued, "] ";
        os << endl;
        return os;
    }
};

struct TransactionEntry : public FixStateListObject
{
    CommandType type; 
    Coordinates coordinates;
    RequestEntry *request;

    void init() {
        request = NULL;
    }
};

struct CommandEntry : public FixStateListObject
{
    RequestEntry *request;
    CommandType type;
    Coordinates coordinates;
    
    long issueTime;
    long finishTime;

    void init() {
        request = NULL;
    }
};



class MemoryMapping
{
    private:
        struct Entry {
            W64 tag;
            Entry* next;
            int count;
        };
        
        int set_count, way_count;
        Entry *entries;
        Entry **sets;
        
        BitMapping mapping;
        
    public:
        MemoryMapping(Config &config);
        virtual ~MemoryMapping();
        
        int channel(W64 address);
        void translate(W64 address, Coordinates &coordinates);
        bool migrate(Coordinates &coordinates);
};



class MemoryController
{
    private:
        MemoryMapping &mapping;
        Policy &policy;
    
        int asym_mat_group;
        int asym_mat_ratio;
    
        int rankcount;
        int bankcount;
        int groupcount;
        int indexcount;
        int refresh_interval;

    public:
        MemoryController(Config &config, MemoryMapping &mapping, Policy &policy);
        virtual ~MemoryController();
        
        Channel *channel;
        
        FixStateList<RequestEntry, MEM_REQ_NUM> pendingRequests_;
        FixStateList<TransactionEntry, MEM_TRANS_NUM> pendingTransactions_;
        FixStateList<CommandEntry, MEM_CMD_NUM> pendingCommands_;
        
        bool addTransaction(long clock, RequestEntry *request);
        bool addMigration(long clock, Coordinates *coordinates);
        bool addCommand(long clock, CommandType type, Coordinates *coordinates, RequestEntry *request);
        
        void schedule(long clock, Signal &accessCompleted_);
};

class MemoryControllerHub : public Controller
{
    private:
        Interconnect *cacheInterconnect_;
        
        Signal accessCompleted_;
        Signal waitInterconnect_;
        
        Policy policy;
        
        Config dramconfig;
        int channelcount;
        
        MemoryMapping *mapping;
        MemoryController **controller;
        long clock_num, clock_den;
        long clock_rem, clock_mem;
        
    public:
        MemoryControllerHub(W8 coreid, const char *name, MemoryHierarchy *memoryHierarchy, int type);
        virtual ~MemoryControllerHub();
        
        void register_interconnect(Interconnect *interconnect, int type);
        
        bool handle_interconnect_cb(void *arg);
        bool access_completed_cb(void *arg);
        bool wait_interconnect_cb(void *arg);
        
        void clock();
        
        void annul_request(MemoryRequest *request);
        void dump_configuration(YAML::Emitter &out) const;
        
        int get_no_pending_request(W8 coreid);
        bool is_full(bool fromInterconnect = false) const {
            return false; // check
        }
        
        void print(ostream& os) const;
        void print_map(ostream& os)
        {
            os << "Memory Controller: ", get_name(), endl;
            os << "\tconnected to:", endl;
        }
};

};

#endif //MEMORY_CONTROLLER_HUB_H