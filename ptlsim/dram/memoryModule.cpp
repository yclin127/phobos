#include "memoryModule.h"
#include <cassert>

using namespace DRAM;

Channel::Channel(Config *config)
{
    rankcount = config->rankcount;
    ranks = new Rank*[rankcount];
    for (int i=0; i<rankcount; ++i) {
        ranks[i] = new Rank(config);
    }
    
    timing = &config->channel_timing;
    energy = &config->channel_energy;
    
    rankSelect = -1;
    
    anyReadyTime   = 0;
    readReadyTime  = 0;
    writeReadyTime = 0;
    
    clockEnergy      = 0;
    commandBusEnergy = 0;
    addressBusEnergy = 0;
    dataBusEnergy    = 0;
}

Channel::~Channel()
{
    for (int i=0; i<rankcount; ++i) {
        delete ranks[i];
    }
    delete [] ranks;
}

BankData &Channel::getBankData(Coordinates &coordinates)
{
    return ranks[coordinates.rank]->getBankData(coordinates);
}

RankData &Channel::getRankData(Coordinates &coordinates)
{
    return ranks[coordinates.rank]->getRankData(coordinates);
}

long Channel::getReadyTime(CommandType type, Coordinates &coordinates)
{
    long clock;
    
    switch (type) {
        case COMMAND_activate:
        case COMMAND_precharge:
        case COMMAND_refresh:
        case COMMAND_migrate:
            clock = ranks[coordinates.rank]->getReadyTime(type, coordinates);
            clock = std::max(clock, anyReadyTime);
            
            return clock;
            
        case COMMAND_read:
        case COMMAND_read_precharge:
            clock = ranks[coordinates.rank]->getReadyTime(type, coordinates);
            clock = std::max(clock, anyReadyTime);
            if (rankSelect != coordinates.rank) {
                clock = std::max(clock, readReadyTime);
            }
            
            return clock;
            
        case COMMAND_write:
        case COMMAND_write_precharge:
            clock = ranks[coordinates.rank]->getReadyTime(type, coordinates);
            clock = std::max(clock, anyReadyTime);
            if (rankSelect != coordinates.rank) {
                clock = std::max(clock, writeReadyTime);
            }
            
            return clock;
            
        case COMMAND_powerup:
        case COMMAND_powerdown:
            clock = ranks[coordinates.rank]->getReadyTime(type, coordinates);
            
            return clock;
            
        default:
            assert(0);
            return -1;
    }
}

long Channel::getFinishTime(long clock, CommandType type, Coordinates &coordinates)
{
    switch (type) {
        case COMMAND_activate:
        case COMMAND_precharge:
        case COMMAND_refresh:
        case COMMAND_migrate:
            anyReadyTime = clock + timing->any_to_any;
            if (type == COMMAND_activate) {
                anyReadyTime = std::max(anyReadyTime, clock + timing->act_to_any);
            }
            
            commandBusEnergy += energy->command;
            if (type == COMMAND_activate) {
                addressBusEnergy += energy->row;
            }
            return ranks[coordinates.rank]->getFinishTime(clock, type, coordinates);
            
        case COMMAND_read:
        case COMMAND_read_precharge:
            anyReadyTime   = clock + timing->any_to_any;
            readReadyTime  = clock + timing->read_to_read;
            writeReadyTime = clock + timing->read_to_write;
            
            commandBusEnergy += energy->command;
            addressBusEnergy += energy->column;
            dataBusEnergy    += energy->data;
            
            rankSelect = coordinates.rank;
            
            return ranks[coordinates.rank]->getFinishTime(clock, type, coordinates);
            
        case COMMAND_write:
        case COMMAND_write_precharge:
            anyReadyTime   = clock + timing->any_to_any;
            readReadyTime  = clock + timing->write_to_read;
            writeReadyTime = clock + timing->write_to_write;
            
            commandBusEnergy += energy->command;
            addressBusEnergy += energy->column;
            dataBusEnergy    += energy->data;
            
            rankSelect = coordinates.rank;
            
            return ranks[coordinates.rank]->getFinishTime(clock, type, coordinates);
            
        case COMMAND_powerup:
        case COMMAND_powerdown:
            return ranks[coordinates.rank]->getFinishTime(clock, type, coordinates);
            
        default:
            assert(0);
            return -1;
    }
}

void Channel::cycle(long clock)
{
    clockEnergy += energy->clock_per_cycle;
    
    for (int rank=0; rank<rankcount; ++rank) {
        ranks[rank]->cycle(clock);
    }
}

Rank::Rank(Config *config)
{
    bankcount = config->bankcount;
    banks = new Bank*[bankcount];
    for (int i=0; i<bankcount; ++i) {
        banks[i] = new Bank(config);
    }
    
    timing = &config->rank_timing;
    energy = &config->rank_energy;
    
    actReadyTime     = 0;
    fawReadyTime[0]  = 0;
    fawReadyTime[1]  = 0;
    fawReadyTime[2]  = 0;
    fawReadyTime[3]  = 0;
    readReadyTime    = 0;
    writeReadyTime   = 0;
    powerupReadyTime = -1;
    
    actEnergy        = 0;
    preEnergy        = 0;
    readEnergy       = 0;
    writeEnergy      = 0;
    refreshEnergy    = 0;
    backgroundEnergy = 0;
}

Rank::~Rank()
{
    for (int i=0; i<bankcount; ++i) {
        delete banks[i];
    }
    delete [] banks;
}

BankData &Rank::getBankData(Coordinates &coordinates)
{
    return banks[coordinates.bank]->getBankData(coordinates);
}

RankData &Rank::getRankData(Coordinates &coordinates)
{
    return data;
}

long Rank::getReadyTime(CommandType type, Coordinates &coordinates)
{
    long clock;
    
    switch (type) {
        case COMMAND_activate:
            clock = banks[coordinates.bank]->getReadyTime(type, coordinates);
            clock = std::max(clock, actReadyTime);
            clock = std::max(clock, fawReadyTime[0]);
            
            return clock;
            
        case COMMAND_precharge:
            clock = banks[coordinates.bank]->getReadyTime(type, coordinates);
            
            return clock;
            
        case COMMAND_read:
        case COMMAND_read_precharge:
            clock = banks[coordinates.bank]->getReadyTime(type, coordinates);
            clock = std::max(clock, readReadyTime);
            
            return clock;
            
        case COMMAND_write:
        case COMMAND_write_precharge:
            clock = banks[coordinates.bank]->getReadyTime(type, coordinates);
            clock = std::max(clock, writeReadyTime);
            
            return clock;
            
        case COMMAND_refresh:
            clock = actReadyTime;
            for (int i=0; i<bankcount; ++i) {
                clock = std::max(clock, banks[i]->getReadyTime(COMMAND_activate, coordinates));
            }
            
            return clock;
            
        case COMMAND_migrate:
            clock = banks[coordinates.bank]->getReadyTime(type, coordinates);
            
            return clock;
            
        case COMMAND_powerup:
            return powerupReadyTime;
            
        case COMMAND_powerdown:
            return 0;
            
        default:
            assert(0);
            return -1;
    }
}

long Rank::getFinishTime(long clock, CommandType type, Coordinates &coordinates)
{
    switch (type) {
        case COMMAND_activate:
            actReadyTime = clock + timing->act_to_act;
            
            fawReadyTime[0] = fawReadyTime[1];
            fawReadyTime[1] = fawReadyTime[2];
            fawReadyTime[2] = fawReadyTime[3];
            fawReadyTime[3] = clock + timing->act_to_faw;
            
            actEnergy += energy->activate;
            
            return banks[coordinates.bank]->getFinishTime(clock, type, coordinates);
            
        case COMMAND_precharge:
            actEnergy += energy->precharge;
            
            return banks[coordinates.bank]->getFinishTime(clock, type, coordinates);
            
        case COMMAND_read:
        case COMMAND_read_precharge:
            readReadyTime  = clock + timing->read_to_read;
            writeReadyTime = clock + timing->read_to_write;
            
            readEnergy += energy->read;
            
            return banks[coordinates.bank]->getFinishTime(clock, type, coordinates);
            
        case COMMAND_write:
        case COMMAND_write_precharge:
            readReadyTime  = clock + timing->write_to_read;
            writeReadyTime = clock + timing->write_to_write;
            
            writeEnergy += energy->write;
            
            return banks[coordinates.bank]->getFinishTime(clock, type, coordinates);
        
        case COMMAND_refresh:
            actReadyTime = clock + timing->refresh_latency;
            
            fawReadyTime[0] = actReadyTime;
            fawReadyTime[1] = actReadyTime;
            fawReadyTime[2] = actReadyTime;
            fawReadyTime[3] = actReadyTime;
            
            refreshEnergy += energy->refresh;
            
            return clock;
            
        case COMMAND_migrate:
            actReadyTime = clock + timing->act_to_act; /** */
            
            fawReadyTime[0] = fawReadyTime[1];
            fawReadyTime[1] = fawReadyTime[2];
            fawReadyTime[2] = fawReadyTime[3];
            fawReadyTime[3] = clock + timing->act_to_faw; /** */
            
            actEnergy += energy->migrate;
            
            return banks[coordinates.bank]->getFinishTime(clock, type, coordinates);
            
        case COMMAND_powerup:
            actReadyTime = clock + timing->powerup_latency;
            
            fawReadyTime[0] = actReadyTime;
            fawReadyTime[1] = actReadyTime;
            fawReadyTime[2] = actReadyTime;
            fawReadyTime[3] = actReadyTime;
            
            powerupReadyTime = -1;
            
            return clock;
            
        case COMMAND_powerdown:
            actReadyTime = -1;
            
            fawReadyTime[0] = actReadyTime;
            fawReadyTime[1] = actReadyTime;
            fawReadyTime[2] = actReadyTime;
            fawReadyTime[3] = actReadyTime;
            
            powerupReadyTime = clock + timing->powerdown_latency;
            
            return clock;
            
        default:
            assert(0);
            return -1;
    }
}

void Rank::cycle(long clock)
{
    if (powerupReadyTime == -1) {
        backgroundEnergy += energy->powerup_per_cycle;
    } else {
        backgroundEnergy += energy->powerdown_per_cycle;
    }
}

Bank::Bank(Config *config)
{
    fast_timing = &config->fast_bank_timing;
    slow_timing = &config->slow_bank_timing;
    
    asym_mat_group = config->asym_mat_group;
    asym_mat_ratio = config->asym_mat_ratio;
    
    actReadyTime   = 0;
    preReadyTime   = -1;
    migReadyTime   = -1;
    readReadyTime  = -1;
    writeReadyTime = -1;
}

Bank::~Bank()
{
}

BankData &Bank::getBankData(Coordinates &coordinates)
{
    return data;
}

long Bank::getReadyTime(CommandType type, Coordinates &coordinates)
{
    switch (type) {
        case COMMAND_activate:
            assert(actReadyTime != -1);
            
            return actReadyTime;
            
        case COMMAND_precharge:
            assert(preReadyTime != -1);
            
            return preReadyTime;
            
        case COMMAND_read:
        case COMMAND_read_precharge:
            assert(readReadyTime != -1);
            
            return readReadyTime;
            
        case COMMAND_write:
        case COMMAND_write_precharge:
            assert(writeReadyTime != -1);
            
            return writeReadyTime;
            
        case COMMAND_migrate:
            assert(migReadyTime != -1);
            
            return migReadyTime;
            
        //case COMMAND_refresh:
        //case COMMAND_powerup:
        //case COMMAND_powerdonw:
            
        default:
            assert(0);
            return -1;
    }
}

long Bank::getFinishTime(long clock, CommandType type, Coordinates &coordinates)
{
    BankTiming *timing;
    
    if ((coordinates.row%asym_mat_group)*asym_mat_ratio < asym_mat_group && asym_mat_ratio > 0) {
        timing = fast_timing;
    } else {
        timing = slow_timing;
    }
    
    switch (type) {
        case COMMAND_activate:
            assert(actReadyTime != -1);
            assert(clock >= actReadyTime);
            
            actReadyTime   = -1;
            preReadyTime   = clock + timing->act_to_pre;
            migReadyTime   = clock + timing->act_to_mig;
            readReadyTime  = clock + timing->act_to_read;
            writeReadyTime = clock + timing->act_to_write;
            
            return clock;
            
        case COMMAND_precharge:
            assert(preReadyTime != -1);
            assert(clock >= preReadyTime);
            
            actReadyTime   = clock + timing->pre_to_act;
            preReadyTime   = -1;
            migReadyTime   = -1;
            readReadyTime  = -1;
            writeReadyTime = -1;
            
            return clock;
            
        case COMMAND_read:
        case COMMAND_read_precharge:
            assert(readReadyTime != -1);
            assert(clock >= readReadyTime);
            
            if (type == COMMAND_read) {
                actReadyTime   = -1;
                preReadyTime   = std::max(preReadyTime, clock + timing->read_to_pre);
                migReadyTime   = std::max(migReadyTime, clock + timing->read_to_mig);
                // see rank for readReadyTime
                // see rank for writeReadyTime
            } else {
                actReadyTime   = clock + timing->read_to_pre + timing->pre_to_act;
                preReadyTime   = -1;
                migReadyTime   = -1;
                readReadyTime  = -1;
                writeReadyTime = -1;
            }
            
            return clock + timing->read_latency;
            
        case COMMAND_write:
        case COMMAND_write_precharge:
            assert(writeReadyTime != -1);
            assert(clock >= writeReadyTime);
            
            if (type == COMMAND_write) {
                actReadyTime   = -1;
                preReadyTime   = std::max(preReadyTime, clock + timing->write_to_pre);
                migReadyTime   = std::max(migReadyTime, clock + timing->write_to_mig);
                // see rank for readReadyTime
                // see rank for writeReadyTime
            } else {
                actReadyTime   = clock + timing->write_to_pre + timing->pre_to_act;
                preReadyTime   = -1;
                migReadyTime   = -1;
                readReadyTime  = -1;
                writeReadyTime = -1;
            }
            
            return clock + timing->write_latency;
            
        case COMMAND_migrate:
            assert(migReadyTime != -1);
            assert(clock >= migReadyTime);
            
            actReadyTime   = -1;
            preReadyTime   = clock + timing->mig_latency + timing->act_to_pre;
            migReadyTime   = clock + timing->mig_latency + timing->act_to_mig;
            readReadyTime  = clock + timing->mig_latency + timing->act_to_read;
            writeReadyTime = clock + timing->mig_latency + timing->act_to_write;
            
            return clock;
        
        //case COMMAND_refresh:
        //case COMMAND_powerup:
        //case COMMAND_powerdonw:
            
        default:
            assert(0);
            return -1;
    }
}
