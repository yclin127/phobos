#ifndef MEMORY_CONTROLLER_H
#define MEMORY_CONTROLLER_H

#include <controller.h>
#include <interconnect.h>
#include <superstl.h>

#include <memoryModule.h>

using namespace Memory;

namespace DRAM {

struct RequestEntry : public FixStateListObject
{
    CommandType type;
    Coordinates coordinates;

    MemoryRequest *request;
    Controller *source;

    bool annuled;
    bool issued;
    bool translated;
    bool allocated;
    bool detected;
    bool missed;

    void init() {
        request = NULL;
        annuled = false;
        issued = false;
        translated = false;
        allocated = false;
        detected = false;
        missed = false;
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
    CommandType type;
    Coordinates coordinates;

    RequestEntry *request;
    
    long issueTime;
    long finishTime;

    void init() {
        request = NULL;
    }
};



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
        Tier3<char> mapping_footprint;
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



class MemoryController
{
    private:
        int max_row_hits;
        int max_row_idle;
    
        int asym_mat_group;
        int asym_mat_ratio;

        int channel_id;
    
        int rankcount;
        int bankcount;
        int groupcount;
        int indexcount;
        int refresh_interval;

    public:
        MemoryController(Config &config, MemoryMapping &mapping, int chid);
        virtual ~MemoryController();
        
        Channel *channel;
        
        FixStateList<TransactionEntry, MEM_TRANS_NUM> pendingTransactions_;
        FixStateList<CommandEntry, MEM_CMD_NUM> pendingCommands_;
        
        bool addTransaction(long clock, CommandType type, Coordinates &coordinates, RequestEntry *request);
        bool addCommand(long clock, CommandType type, Coordinates &coordinates, RequestEntry *request);
        
        void schedule(long clock, Signal &accessCompleted_);
};

class MemoryControllerHub : public Controller
{
    private:
        Interconnect *cacheInterconnect_;
        
        Signal accessCompleted_;
        Signal waitInterconnect_;
        
        Config dramconfig;
        int channelcount;
        
        MemoryMapping *mapping;
        MemoryController **controller;
        long clock_num, clock_den;
        long clock_rem, clock_mem;
        
        FixStateList<RequestEntry, MEM_REQ_NUM> pendingRequests_;
        void dispatch(long clock);
        
    public:
        MemoryControllerHub(W8 coreid, const char *name, MemoryHierarchy *memoryHierarchy, int type);
        virtual ~MemoryControllerHub();
        
        void register_interconnect(Interconnect *interconnect, int type);
        
        bool handle_interconnect_cb(void *arg);
        bool access_completed_cb(void *arg);
        bool wait_interconnect_cb(void *arg);
        
        void cycle();
        
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