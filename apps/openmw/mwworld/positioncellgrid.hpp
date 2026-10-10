#ifndef OPENMW_APPS_OPENMW_MWWORLD_POSITIONCELLGRID_H
#define OPENMW_APPS_OPENMW_MWWORLD_POSITIONCELLGRID_H

#include <vector>

#include <osg/Vec3f>
#include <osg/Vec4i>

#include <components/esm/refid.hpp>

namespace MWWorld
{
    struct PositionCellGrid
    {
        osg::Vec3f mPosition;
        osg::Vec4i mCellBounds;
    };

    constexpr float terrainPreloadMergeDistance = 128.f;

    std::vector<PositionCellGrid> terrainPreloadPositions(const osg::Vec3f& primary, const osg::Vec3f& playerPos,
        const osg::Vec3f& cameraPos, bool cameraDetached, const osg::Vec4i& grid, ESM::RefId worldspace);
}

#endif
