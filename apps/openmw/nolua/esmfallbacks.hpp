#ifndef OPENMW_NOLUA_ESMFALLBACKS_H
#define OPENMW_NOLUA_ESMFALLBACKS_H

namespace MWWorld
{
    class ESMStore;
}

namespace MWNoLua
{
    void applyEsmFallbacks(MWWorld::ESMStore& store);
}

#endif
