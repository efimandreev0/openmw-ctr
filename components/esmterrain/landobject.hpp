#ifndef OPENMW_COMPONENTS_ESMTERRAIN_LANDOBJECT_H
#define OPENMW_COMPONENTS_ESMTERRAIN_LANDOBJECT_H

#include <cstdint>
#include <utility>

#include <components/esm/esmterrain.hpp>
#include <components/terrain/defs.hpp>

namespace ESM
{
    struct Land;
}

namespace ESM4
{
    struct Land;
}

namespace ESMTerrain
{
    class LandObject
    {
    public:
        LandObject() = default;
        LandObject(const ESM::Land& land, int loadFlags);
        LandObject(const ESM4::Land& land, int loadFlags);

        const ESM::LandData* getData(int flags) const
        {
            if ((mData.getLoadFlags() & flags) != flags)
                return nullptr;

            return &mData;
        }

        int getPlugin() const { return mData.getPlugin(); }

        const Terrain::LayerInfo& getEsm4DefaultLayerInfo() const { return mEsm4DefaultLayerInfo; }

    private:
        ESM::LandData mData;

        Terrain::LayerInfo mEsm4DefaultLayerInfo;
    };

    using UniqueTextureId = std::pair<std::uint16_t, int>;
}

#endif
