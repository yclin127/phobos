#include <memoryController.h>
#include <memoryStatistics.h>

using namespace DRAM;

MemoryController::MemoryController(Config &config, MemoryMapping &mapping, int chid) :
    dramconfig(config),
    channel_id(chid)
{
    channel = new Channel(&config);
    
    Coordinates coordinates = {0};
    int refresh_step = dramconfig.rank_timing.refresh_interval/dramconfig.rankcount;
    
    for (coordinates.rank=0; coordinates.rank<dramconfig.rankcount; ++coordinates.rank) {
        // initialize rank
        RankData &rank = channel->getRankData(coordinates);
        rank.demandCount = 0;
        rank.activeCount = 0;
        rank.refreshTime = refresh_step*(coordinates.rank+1);
        rank.is_sleeping = false;
        
        for (coordinates.bank=0; coordinates.bank<dramconfig.bankcount; ++coordinates.bank) {
            // initialize bank
            BankData &bank = channel->getBankData(coordinates);
            bank.totalTransaction.reset();
            bank.rowBuffer = -1;
        }
    }
}

MemoryController::~MemoryController()
{
    delete channel;
}

bool MemoryController::addTransaction(long clock, CommandType type, Coordinates &coordinates, void *request)
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
    bank.totalTransaction.add(type);
    if (coordinates.row == bank.rowBuffer) {
        bank.readyTransaction.add(type);
    }
    
    return true;
}

bool MemoryController::addCommand(long clock, CommandType type, Coordinates &coordinates, void *request)
{
    int64_t readyTime, issueTime, finishTime;
    
    readyTime = channel->getReadyTime(type, coordinates);
    if (clock != -1) {
        issueTime = clock;
    } else {
        issueTime = readyTime;
    }
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

void MemoryController::schedule(long clock, Signal &accessCompleted_, Signal &lookupCompleted_)
{
    /** Transaction to Command */
    
    // Refresh policy
    {
        Coordinates coordinates = {0};
        for (coordinates.rank = 0; coordinates.rank < dramconfig.rankcount; ++coordinates.rank) {
            RankData &rank = channel->getRankData(coordinates);
            
            if (clock < rank.refreshTime) continue;
            
            // Power up
            if (rank.is_sleeping) {
                if (!addCommand(clock, COMMAND_powerup, coordinates, NULL)) continue;
                rank.is_sleeping = false;
            }
            
            // Precharge
            for (coordinates.bank = 0; coordinates.bank < dramconfig.bankcount; ++coordinates.bank) {
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
            rank.refreshTime += dramconfig.rank_timing.refresh_interval;
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
                bank.hitCount >= dramconfig.max_row_hits)) {
                if (bank.rowBuffer != coordinates.row && bank.readyTransaction.totalCount > 0) continue;
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

                bank.readyTransaction.reset();
                TransactionEntry *transaction2;
                foreach_list_mutable(pendingTransactions_.list(), transaction2, entry2, nextentry2) {
                    Coordinates &coordinates2 = transaction2->coordinates;
                    if (coordinates2.rank == coordinates.rank && 
                        coordinates2.bank == coordinates.bank && 
                        coordinates2.row  == coordinates.row) {
                        bank.readyTransaction.add(transaction2->type);
                    }
                }

                if (transaction->request) {
                    transaction->missed = true;
                }
            }
            
            switch (transaction->type) {
                case COMMAND_migrate:
                    if (bank.readyTransaction.writeCount > 0) continue;
                case COMMAND_write:
                    if (bank.readyTransaction.readCount > 0) continue;
                case COMMAND_read:
                    break;
                default: assert(0);
            }

            // Read / Write / Migrate
            if (!addCommand(clock, transaction->type, coordinates, transaction->request)) continue;
            rank.demandCount -= 1;
            bank.totalTransaction.remove(transaction->type);
            bank.readyTransaction.remove(transaction->type);
            bank.hitCount += 1;

            if (transaction->request) {
                memoryCounter.accessCounter.count += 1;
                if (transaction->missed) {
                    if (coordinates.place % dramconfig.asym_mat_ratio == 0) {
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
        for (coordinates.rank = 0; coordinates.rank < dramconfig.rankcount; ++coordinates.rank) {
            RankData &rank = channel->getRankData(coordinates);
            for (coordinates.bank = 0; coordinates.bank < dramconfig.bankcount; ++coordinates.bank) {
                BankData &bank = channel->getBankData(coordinates);
                
                if (bank.rowBuffer == -1 || bank.totalTransaction.totalCount > 0) continue;
                
                int64_t idleTime = clock - dramconfig.max_row_idle;
                if (!addCommand(idleTime, COMMAND_precharge, coordinates, NULL)) continue;
                rank.activeCount -= 1;
                bank.rowBuffer = -1;
            }
        }
    }
    
    // Power down policy
    {
        Coordinates coordinates = {0};
        for (coordinates.rank = 0; coordinates.rank < dramconfig.rankcount; ++coordinates.rank) {
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
                case COMMAND_readPre:
                case COMMAND_write:
                case COMMAND_writePre:
                    if (command->request) {
                        accessCompleted_.emit(command->request);
                    } else if (command->type == COMMAND_read) {
                        lookupCompleted_.emit(&command->coordinates);
                    }
                    break;
                    
                default:
                    break;
            }
            
            pendingCommands_.free(command);
        }
    }
}
