#ifndef __MEMORY_STATISTICS_H__
#define __MEMORY_STATISTICS_H__

#include <statsBuilder.h>

namespace DRAM {

struct MemoryCounter
{
    struct AccessCounter {
        long count;
        long queueLength;
        long rowBuffer;
        long fastSegment;
        long slowSegment;
    } accessCounter;

    struct RowCounter {
        long count;
        long query;
        long migration;
        long remigration;
    } rowCounter;

    struct EnergyCounter {
        long actPre;
        long read;
        long write;
        long refresh;
        long migrate;
        long background;
        long total;
    } energyCounter;
};

extern MemoryCounter memoryCounter;

struct MemoryStatable : public Statable
{
    /*StatEquation<W64, double, StatObjFormulaDiv> api;
    StatEquation<W64, double, StatObjFormulaDiv> cpa;
    StatEquation<W64, double, StatObjFormulaDiv> mpt;
    StatEquation<W64, double, StatObjFormulaDiv> mpa;
    StatObj<W64> accesses;
    StatObj<W64> captures;
    StatObj<W64> touches;
    StatObj<W64> migrations;*/
    
    MemoryStatable(Statable *parent, StatObj<W64> &insns)
        : Statable("memory", parent)
            /*, api("api", this)
            , cpa("cpa", this)
            , mpt("mpt", this)
            , mpa("mpa", this)
            , accesses("accesses", this)
            , captures("captures", this)
            , touches("touches", this)
            , migrations("migrations", this)*/
    {
        /*api.enable_summary();
        cpa.enable_summary();
        mpt.enable_summary();
        mpa.enable_summary();*/
        
        /*api.add_elem(&accesses);
        api.add_elem(&insns);

        cpa.add_elem(&captures);
        cpa.add_elem(&accesses);

        mpt.add_elem(&migrations);
        mpt.add_elem(&touches);

        mpa.add_elem(&migrations);
        mpa.add_elem(&accesses);*/
    }
};

};

#endif // __MEMORY_STATISTICS_H__