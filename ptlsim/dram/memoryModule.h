#ifndef __MEMORY_MODUlE_H__
#define __MEMORY_MODUlE_H__

#include <memoryCommon.h>
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
    int act_to_mig;
    int read_to_pre;
    int read_to_mig;
    int write_to_pre;
    int write_to_mig;
    int pre_to_act;
    
    int read_latency;
    int write_latency;
    int mig_latency;
};

struct ChannelEnergy {
    int command;
    int row;
    int column;
    int data;

    int clock_per_cycle;
};

struct RankEnergy {
    int actPre_fast;
    int actPre_slow;
    int read;
    int write;
    int refresh;
    int migrate;
    
    int active_standby_per_cycle;
    int active_powerdown_per_cycle;
    int precharge_standby_per_cycle;
    int precharge_powerdown_per_cycle;
};

struct Config {
    long ranksize;
    
    int devicecount;
    int channelcount;
    int rankcount;
    int bankcount;
    int rowcount;
    int columncount;
    int offsetcount;
    
    int clustercount;
    int groupcount;
    int indexcount;
  
    int max_row_idle;
    int max_row_hits;

    int asym_det_cache_size;
    int asym_det_threshold;

    int asym_map_profiling;
    int asym_map_on_chip;
    int asym_map_cache_size;
    int asym_map_cache_compact;

    int asym_mat_ratio;
    int asym_mat_group;
    int asym_rep_order;
    int asym_rep_last;
    
    float clock;
    
    ChannelTiming channel_timing;
    RankTiming rank_timing;
    BankTiming fast_bank_timing;
    BankTiming  slow_bank_timing;
    
    ChannelEnergy channel_energy;
    RankEnergy rank_energy;
    
    Config() {}
    Config(
      int DEVICE, int BANK, int COLUMN, int SIZE, 
      float tCK, int tCMD, 
      int tCL, int tCWL, int tBL, 
      int tRAS, int tRCD, int tRP, 
      int tRRD, int tCCD, int tFAW, 
      int tRTP, int tWTR, int tWR, int tRTRS, 
      int tRFC, int tREFI,
      int tCKE, int tXP,
      float VDD, float VDDQ,
      int IDD0, int IDD1,
      int IDD2P0, int IDD2P1,
      int IDD2N, int IDD2NT, int IDD2Q,
      int IDD3P, int IDD3N,
      int IDD4R, int IDD4W,
      int IDD5B, int IDD6,
      int IDD7, int IDD8
    ) {
        ranksize = (long)SIZE<<20;
        
        assert(is_pow_of_2(ranksize));
        assert(is_pow_of_2(BANK));
        assert(ranksize % BANK == 0);
        
        devicecount = DEVICE;
        channelcount = -1;
        rankcount = -1;
        bankcount = BANK;
        rowcount = ((ranksize/BANK)>>13);
        columncount = 1<<7;
        offsetcount = 1<<6;
        
        groupcount = -1;
        indexcount = -1;
        
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
        
        slow_bank_timing.act_to_read   = tRCD;
        slow_bank_timing.act_to_write  = tRCD;
        slow_bank_timing.act_to_pre    = tRAS;
        slow_bank_timing.read_to_pre   = tBL+std::max(tRTP, tCCD)-tCCD;
        slow_bank_timing.write_to_pre  = tCWL+tBL+tWR;
        slow_bank_timing.pre_to_act    = tRP;
        slow_bank_timing.read_latency  = tCL;
        slow_bank_timing.write_latency = tCWL;
        
        slow_bank_timing.act_to_mig    = slow_bank_timing.act_to_pre;
        slow_bank_timing.read_to_mig   = slow_bank_timing.read_to_pre;
        slow_bank_timing.write_to_mig  = slow_bank_timing.write_to_pre;
        slow_bank_timing.mig_latency   = tRAS+tRP;

        fast_bank_timing = slow_bank_timing;

        channel_energy.command = 0;
        channel_energy.row     = 0;
        channel_energy.column  = 0;
        channel_energy.data    = 0;

        channel_energy.clock_per_cycle = 0;

        rank_energy.actPre_slow = tCK*VDD*((IDD0-IDD3N)*tRAS+(IDD0-IDD2N)*tRP)*DEVICE;
        rank_energy.actPre_fast = rank_energy.actPre_slow;
        rank_energy.read        = tCK*VDD*(IDD4R-IDD3N)*tBL*DEVICE;
        rank_energy.write       = tCK*VDD*(IDD4W-IDD3N)*tBL*DEVICE;
        rank_energy.refresh     = tCK*VDD*(IDD5B-IDD3N)*tRFC*DEVICE;
        rank_energy.migrate     = rank_energy.actPre_slow;
        
        rank_energy.active_standby_per_cycle      = tCK*VDD*IDD3N*DEVICE;
        rank_energy.active_powerdown_per_cycle    = tCK*VDD*IDD3P*DEVICE;
        rank_energy.precharge_standby_per_cycle   = tCK*VDD*IDD2N*DEVICE;
        rank_energy.precharge_powerdown_per_cycle = tCK*VDD*IDD2P1*DEVICE;
    }

    void cache_setup(float mig_percent, float ap_percent, 
        float rcd_percent, float ras_percent, float rp_percent, float wr_percent, float cl_percent,
        float rcd_percent2, float ras_percent2, float rp_percent2, float wr_percent2, float cl_percent2)
    {
        #define scale(value, percent) (value) = ((value)*(percent)+50)/100

        // migrate latency
        scale(slow_bank_timing.mig_latency,   mig_percent);
        scale(fast_bank_timing.mig_latency,   mig_percent);

        // actPre power for fast region
        scale(rank_energy.actPre_fast,        ap_percent);

        // timing for fast region
        scale(fast_bank_timing.act_to_read,   rcd_percent);
        scale(fast_bank_timing.act_to_write,  rcd_percent);
        scale(fast_bank_timing.act_to_pre,    ras_percent);
        scale(fast_bank_timing.pre_to_act,    rp_percent);
        scale(fast_bank_timing.write_to_pre,  wr_percent);
        scale(fast_bank_timing.read_latency,  cl_percent);
        scale(fast_bank_timing.write_latency, cl_percent);

        // timing for slow region
        scale(slow_bank_timing.act_to_read,   rcd_percent2);
        scale(slow_bank_timing.act_to_write,  rcd_percent2);
        scale(slow_bank_timing.act_to_pre,    ras_percent2);
        scale(slow_bank_timing.pre_to_act,    rp_percent2);
        scale(slow_bank_timing.write_to_pre,  wr_percent2);
        scale(slow_bank_timing.read_latency,  cl_percent2);
        scale(slow_bank_timing.write_latency, cl_percent2);

        #undef scale
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
    long migReadyTime;
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
    int opencount;
    Bank** banks;
    
    RankData data;
    
    int asym_mat_group;
    int asym_mat_ratio;
    
    long actReadyTime;
    long fawReadyTime[4];
    long readReadyTime;
    long writeReadyTime;
    long powerupReadyTime;
    
    long actPreEnergy;
    long readEnergy;
    long writeEnergy;
    long refreshEnergy;
    long migrateEnergy;
    long backgroundEnergy;
    
public:
    Rank(Config *config);
    virtual ~Rank();
    
    BankData &getBankData(Coordinates &coordinates);
    RankData &getRankData(Coordinates &coordinates);
    long getReadyTime(CommandType type, Coordinates &coordinates);
    long getFinishTime(long clock, CommandType type, Coordinates &coordinates);
    long getEnergy(EnergyType type);
    
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
    long getEnergy(EnergyType type);
    
    void cycle(long clock);
};

};

#endif // __MEMORY_MODUlE_H__