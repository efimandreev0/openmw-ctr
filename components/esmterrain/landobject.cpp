#include "landobject.hpp"

#include <components/esm3/loadland.hpp>
#include <components/esm4/loadland.hpp>

namespace ESMTerrain
{
    LandObject::LandObject(const ESM4::Land& land, int loadFlags)
        : mData(land, loadFlags)
    {
        mEsm4DefaultLayerInfo.mDiffuseMap = land.mDefaultDiffuseMap;
        mEsm4DefaultLayerInfo.mNormalMap = land.mDefaultNormalMap;
    }

    LandObject::LandObject(const ESM::Land& land, int loadFlags)
        : mData(land, loadFlags)
    {
    }
}
