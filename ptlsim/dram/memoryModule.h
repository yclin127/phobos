#ifndef __MEMORY_MODUlE_H__
#define __MEMORY_MODUlE_H__

#include "memoryCommon.h"
#include <cassert>

namespace DRAM {

struct ChannelTiming {
    int any_to_any;
    int act_to_any;
    int read_to_read;
    int read_to_write;
    int write_to_read;
    int write_to_write;
};

struct RankTiming {
    int act_to_act; /* */
    int act_to_faw; /* */
    int read_to_read;
    int read_to_write;
    int write_to_read;
    int write_to_write;
    
    int refresh_latency;
    int refresh_interval;
    
    int powerdown_latency;
    int powerup_latency;
};

struct BankTiming {
    int act_to_read;
    int act_to_write;
    int act_to_pre;
    int read_to_pre;
    int write_to_pre;
    int pre_to_act;
    
    int read_to_data;
    int write_to_data;
};

struct ChannelEnergy {
    int cmd;
    int row;
    int col;
    int data;
    
    int clock_per_cycle;
};

struct RankEnergy {
    int act;
    int read;
    int write;
    int refresh;
    
    int powerup_per_cycle;
    int powerdown_per_cycle;
};

struct Config {
    long ranksize;
    
    int devicecount;
    int channelcount;
    int rankcount;
    int bankcount;
    int rowcount;
    int columncount;
    
    int asym_mat_group;
    int asym_mat_ratio;
    
    float clock;
    
    ChannelTiming channel_timing;
    RankTiming    rank_timing;
    BankTiming    fast_bank_timing;
    BankTiming    slow_bank_timing;
    
    ChannelEnergy channel_energy;
    RankEnergy    rank_energy;
    
    Config() {}
    Config(
      int DEVICE, int BANK, int COLUMN, int SIZE, 
      float tCK, int tCMD, 
      int tCL, int tCWL, int tAL, int tBL, 
      int tRAS, int tRCD, int tRP, 
      int tRRD, int tCCD, int tFAW, 
      int tRTP, int tWTR, int tWR, int tRTRS, 
      int tRFC, int tREFI,
      int tCKE, int tXP
    ) {
        ranksize = SIZE<<20;
        
        assert(ranksize%BANK == 0);
        devicecount = DEVICE;
        channelcount = -1;
        rankcount = -1;
        bankcount = BANK;
        rowcount = ((ranksize/BANK)>>13);
        columncount = 1<<13;
        
        asym_mat_group = -1;
        asym_mat_ratio = -1;
        
        clock = tCK;
        
        channel_timing.any_to_any = tCMD;
        channel_timing.act_to_any = tCMD;
        channel_timing.read_to_read = tBL+tRTRS;
        channel_timing.read_to_write = tCL+tBL+tRTRS-tCWL;
        channel_timing.write_to_read = tCWL+tBL+tRTRS-tCL;
        channel_timing.write_to_write = tBL+tRTRS;
        
        rank_timing.act_to_act = tRRD;
        rank_timing.act_to_faw = tFAW;
        rank_timing.read_to_read = std::max(tBL, tCCD);
        rank_timing.read_to_write = tCL+tBL+tRTRS-tCWL;
        rank_timing.write_to_read = tCWL+tBL+tWTR;
        rank_timing.write_to_write = std::max(tBL, tCCD);
        rank_timing.refresh_latency = tRFC;
        rank_timing.refresh_interval = tREFI;
        rank_timing.powerdown_latency = tCKE;
        rank_timing.powerup_latency = tXP;
        
        int tRCDf = tRCD/2;
        int tRASf = tRAS/2;
        int tRPf = tRP/2;
        int tWRf = tWR/2;
        fast_bank_timing.act_to_read = tRCDf-tAL; /** */
        fast_bank_timing.act_to_write = tRCDf-tAL; /** */
        fast_bank_timing.act_to_pre = tRASf; /** */
        fast_bank_timing.read_to_pre = tAL+tBL+std::max(tRTP, tCCD)-tCCD; /* */
        fast_bank_timing.write_to_pre = tAL+tCWL+tBL+tWRf; /* */
        fast_bank_timing.pre_to_act = tRPf; /** */
        fast_bank_timing.read_to_data = tAL+tCL;
        fast_bank_timing.write_to_data = tAL+tCWL;
        
        slow_bank_timing.act_to_read = tRCD-tAL;
        slow_bank_timing.act_to_write = tRCD-tAL;
        slow_bank_timing.act_to_pre = tRAS;
        slow_bank_timing.read_to_pre = tAL+tBL+std::max(tRTP, tCCD)-tCCD;
        slow_bank_timing.write_to_pre = tAL+tCWL+tBL+tWR;
        slow_bank_timing.pre_to_act = tRP;
        slow_bank_timing.read_to_data = tAL+tCL;
        slow_bank_timing.write_to_data = tAL+tCWL;
    }
};



class Bank
{
protected:
    BankTiming *fast_timing;
    BankTiming *slow_timing;
    
    BankData data;
    
    int asym_mat_group;
    int asym_mat_ratio;
    
    long actReadyTime;
    long preReadyTime;
    long readReadyTime;
    long writeReadyTime;
    
public:
    Bank(Config *config);    
    virtual ~Bank();
    
    BankData &getBankData(Coordinates &coordinates);
    long getReadyTime(CommandType type, Coordinates &coordinates);
    long getFinishTime(long clock, CommandType type, Coordinates &coordinates);
};

class Rank
{
protected:
    RankTiming *timing;
    RankEnergy *energy;
    
    int bankcount;
    Bank** banks;
    
    RankData data;
    
    long actReadyTime;
    long fawReadyTime[4];
    long readReadyTime;
    long writeReadyTime;
    long powerupReadyTime;
    
    long actEnergy;
    long preEnergy;
    long readEnergy;
    long writeEnergy;
    long refreshEnergy;
    long backgroundEnergy;
    
public:
    Rank(Config *config);
    virtual ~Rank();
    
    BankData &getBankData(Coordinates &coordinates);
    RankData &getRankData(Coordinates &coordinates);
    long getReadyTime(CommandType type, Coordinates &coordinates);
    long getFinishTime(long clock, CommandType type, Coordinates &coordinates);
    
    void cycle(long clock);
};

class Channel
{
protected:
    ChannelTiming *timing;
    ChannelEnergy *energy;
    
    int rankcount;
    Rank** ranks;
    
    int rankSelect;
    
    long anyReadyTime;
    long readReadyTime;
    long writeReadyTime;
    
    long clockEnergy;
    long commandBusEnergy;
    long addressBusEnergy;
    long dataBusEnergy;
    
public:
    Channel(Config *config);
    virtual ~Channel();
    
    BankData &getBankData(Coordinates &coordinates);
    RankData &getRankData(Coordinates &coordinates);
    long getReadyTime(CommandType type, Coordinates &coordinates);
    long getFinishTime(long clock, CommandType type, Coordinates &coordinates);
    
    void cycle(long clock);
};

};

#endif // __MEMORY_MODUlE_H__