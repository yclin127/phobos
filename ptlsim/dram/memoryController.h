#ifndef MEMORY_CONTROLLER_H
#define MEMORY_CONTROLLER_H

#include <ptlsim.h>
#include <ptlhwdef.h>
#include <controller.h>
#include <interconnect.h>
#include <superstl.h>

using namespace Memory;

#include <memoryModule.h>
#include <memoryMapping.h>

namespace DRAM {

struct RequestEntry;

struct TransactionEntry : public FixStateListObject
{
    CommandType type; 
    Coordinates coordinates;

    bool missed;

    void *request;

    void init() {
        missed = false;
        request = NULL;
    }
};

struct CommandEntry : public FixStateListObject
{
    CommandType type;
    Coordinates coordinates;

    void *request;
    
    long issueTime;
    long finishTime;

    void init() {
        request = NULL;
    }
};



class MemoryController
{
    private:
        const Config &dramconfig;

        int channel_id;

    public:
        MemoryController(Config &config, MemoryMapping &mapping, int chid);
        virtual ~MemoryController();
        
        Channel *channel;
        
        FixStateList<TransactionEntry, MEM_TRANS_NUM> pendingTransactions_;
        FixStateList<CommandEntry, MEM_CMD_NUM> pendingCommands_;
        
        bool addTransaction(long clock, CommandType type, Coordinates &coordinates, void *request);
        bool addCommand(long clock, CommandType type, Coordinates &coordinates, void *request);
        
        void schedule(long clock, Signal &accessCompleted_, Signal &lookupCompleted_);
};

};

#endif //MEMORY_CONTROLLER_H