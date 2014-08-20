#ifndef __MEMORY_STATISTICS_H__
#define __MEMORY_STATISTICS_H__

#include <statsBuilder.h>

namespace DRAM {

struct MemoryCounter
{
    struct AccessCounter {
        long count;
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

    /*struct EnergyCounter {
        long actPre;
        long read;
        long write;
        long refresh;
        long migrate;
        long background;
        long total;
    } energyCounter;*/

    MemoryCounter() {
        memset(&accessCounter, 0, sizeof(accessCounter));
        memset(&rowCounter, 0, sizeof(rowCounter));
    }

    friend superstl::stringbuf& operator<< (superstl::stringbuf& sb, 
        const MemoryCounter& mc) {
        sb
        << mc.accessCounter.count << " accesses, " 
        << mc.accessCounter.rowBuffer << " hits, " 
        << mc.accessCounter.fastSegment << " fast, " 
        << mc.accessCounter.slowSegment << " slow, " 
        << mc.rowCounter.count << " rows, " 
        << mc.rowCounter.migration << " moves, " 
        << mc.rowCounter.remigration << " redoes, "
        << mc.rowCounter.query << " queries, ";
        return sb;
    }
};

/*struct MemoryDistribution
{
    std::map<long, long> dist_accesses;
    std::map<long, long> dist_intervals;
    std::map<long, long> dist_histories;

    MemoryDistribution() {
    }

    friend std::ostream& operator<< (std::ostream& os, 
        const MemoryDistribution& md) {
        for (map<long, long>::const_iterator it = md.dist_accesses.begin(); 
            it != md.dist_accesses.end(); ++it)
            os << "access " << it->first << " " << it->second << endl;
        for (map<long, long>::const_iterator it = md.dist_intervals.begin();
            it != md.dist_intervals.end(); ++it)
            os << "interval " << it->first << " " << it->second << endl;
        for (map<long, long>::const_iterator it = md.dist_histories.begin();
            it != md.dist_histories.end(); ++it)
            os << "history " << it->first << " " << it->second << endl;
        return os;
    }
};*/

extern MemoryCounter memoryCounter;
//extern MemoryDistribution memoryDistribution;

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