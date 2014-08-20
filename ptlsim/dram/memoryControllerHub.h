#ifndef MEMORY_CONTROLLER_HUB_H
#define MEMORY_CONTROLLER_HUB_H

#include <memoryController.h>

namespace DRAM {

enum RequestStage {
    STAGE_unknown,
    STAGE_translate,
    STAGE_issue,
    STAGE_update,
    STAGE_migrate,
    STAGE_finish,
    STAGE_annul,
};

struct RequestEntry : public FixStateListObject
{
    CommandType type;
    Coordinates coordinates;

    MemoryRequest *request;
    Controller *source;

    bool respounded;
    RequestStage stage;

    void init() {
        request = NULL;
        respounded = false; 
        stage = STAGE_unknown;
    }

    ostream& print(ostream &os) const {
        if(request)
            os << "Request{", *request, "} ";
        if (source)
            os << "source[", source->get_name(), "] ";
        //os << "annuled[", annuled, "] ";
        //os << "issued[", issued, "] ";
        os << endl;
        return os;
    }
};



class MemoryControllerHub : public Controller
{
    private:
        Interconnect *cacheInterconnect_;
        
        Signal lookupCompleted_;
        Signal accessCompleted_;
        Signal waitInterconnect_;
        
        Config dramconfig;
        int channelcount;
        
        MemoryMapping *mapping;
        map<W64,int> lookup_queue;

        MemoryController **controller;
        
        long clock_num, clock_den;
        long clock_rem, clock_mem;
        
        FixStateList<RequestEntry, MEM_REQ_NUM> pendingRequests_;
        void issue(RequestEntry *request, int cycle, W64 address);
        void dispatch(long clock);
        void retire(RequestEntry *request);
        
    public:
        MemoryControllerHub(W8 coreid, const char *name, MemoryHierarchy *memoryHierarchy, int type);
        virtual ~MemoryControllerHub();
        
        void register_interconnect(Interconnect *interconnect, int type);
        
        bool handle_interconnect_cb(void *arg);
        bool access_completed_cb(void *arg);
        bool wait_interconnect_cb(void *arg);
        
        void cycle();
        bool lookup_completed_cb(void *arg);
        
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