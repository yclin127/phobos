#include <statsBuilder.h>

namespace DRAM {

struct MemoryStatable : public Statable
{
    StatEquation<W64, double, StatObjFormulaDiv> api;
    StatEquation<W64, double, StatObjFormulaDiv> cpa;
    StatEquation<W64, double, StatObjFormulaDiv> kpc;
    StatEquation<W64, double, StatObjFormulaDiv> mpt;
    StatEquation<W64, double, StatObjFormulaDiv> mpa;
    StatObj<W64> accesses;
    StatObj<W64> captures;
    StatObj<W64> kills;
    StatObj<W64> touches;
    StatObj<W64> migrations;
    
    MemoryStatable(Statable *parent, StatObj<W64> &insns)
        : Statable("memory", parent)
            , api("api", this)
            , cpa("cpa", this)
            , kpc("kpc", this)
            , mpt("mpt", this)
            , mpa("mpa", this)
            , accesses("accesses", this)
            , captures("captures", this)
            , kills("kills", this)
            , touches("touches", this)
            , migrations("migrations", this)
    {
        api.enable_summary();
        cpa.enable_summary();
        kpc.enable_summary();
        mpt.enable_summary();
        mpa.enable_summary();
        
        api.add_elem(&accesses);
        api.add_elem(&insns);

        cpa.add_elem(&captures);
        cpa.add_elem(&accesses);

        kpc.add_elem(&kills);
        kpc.add_elem(&captures);

        mpt.add_elem(&migrations);
        mpt.add_elem(&touches);

        mpa.add_elem(&migrations);
        mpa.add_elem(&accesses);
    }
};

};