#ifndef OPENMW_MWRENDER_LANDMANAGER_H
#define OPENMW_MWRENDER_LANDMANAGER_H


#include <components/esm/exteriorcelllocation.hpp>
#ifdef OPENMW_CTR_NATIVE
#include <components/esmterrain/landobject.hpp>
#else
#include <components/esmterrain/storage.hpp>
#endif
#include <components/resource/resourcemanager.hpp>

namespace ESM
{
    struct Land;
}

namespace MWRender
{

    class LandManager : public Resource::GenericResourceManager<ESM::ExteriorCellLocation,
                            std::shared_ptr<const ESMTerrain::LandObject>>
    {
    public:
        LandManager(int loadFlags);

        /// @note Will return nullptr if not found.
        std::shared_ptr<const ESMTerrain::LandObject> getLand(ESM::ExteriorCellLocation cellIndex);

        void reportStats(unsigned int frameNumber, osg::Stats* stats) const override;

    private:
        int mLoadFlags;
    };

}

#endif
