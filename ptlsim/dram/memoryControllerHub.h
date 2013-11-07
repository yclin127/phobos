#ifndef MEMORY_CONTROLLER_HUB_H
#define MEMORY_CONTROLLER_HUB_H

#include <memoryController.h>

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
    bool detected;
    bool updated;

    void init() {
        request = NULL;
        annuled = false;
        issued = false;
        translated = false;
        detected = false;
        updated = false;
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



class MemoryControllerHub : public Controller
{
    private:
        Interconnect *cacheInterconnect_;
        
        Signal missCompleted_;
        Signal accessCompleted_;
        Signal waitInterconnect_;
        
        Config dramconfig;
        int channelcount;
        
        MemoryMapping *mapping;
        map<W64,int> mapping_misses;

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
        bool miss_completed_cb(void *arg);
        
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