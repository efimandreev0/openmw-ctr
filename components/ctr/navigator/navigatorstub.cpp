
#include <components/detournavigator/agentbounds.hpp>
#include <components/detournavigator/debug.hpp>
#include <components/detournavigator/navigator.hpp>
#include <components/detournavigator/navigatorstub.hpp>
#include <components/detournavigator/navigatorutils.hpp>
#include <components/detournavigator/stats.hpp>

#include <DetourNavMeshQuery.h>

#include <ostream>

namespace DetourNavigator
{
    std::unique_ptr<Navigator> makeNavigator(const Settings&, const std::filesystem::path&)
    {
        return makeNavigatorStub();
    }

    std::unique_ptr<Navigator> makeNavigatorStub()
    {
        return std::make_unique<NavigatorStub>();
    }

    std::optional<osg::Vec3f> findRandomPointAroundCircle(
        const Navigator&, const AgentBounds&, const osg::Vec3f&, const float, const Flags, float (*)())
    {
        return std::nullopt;
    }

    std::optional<osg::Vec3f> raycast(
        const Navigator&, const AgentBounds&, const osg::Vec3f&, const osg::Vec3f&, const Flags)
    {
        return std::nullopt;
    }

    void reportStats(const Stats&, unsigned int, osg::Stats&) {}

    std::ostream& operator<<(std::ostream& s, const AgentBounds& v)
    {
        return s << "AgentBounds {" << static_cast<int>(v.mShapeType) << ", {" << v.mHalfExtents.x() << ", "
                 << v.mHalfExtents.y() << ", " << v.mHalfExtents.z() << "}}";
    }

    std::ostream& operator<<(std::ostream& stream, const WriteFlags& value)
    {
        return stream << "Flags(" << static_cast<unsigned>(value.mValue) << ")";
    }
}

dtQueryFilter::dtQueryFilter()
    : m_includeFlags(0xffff)
    , m_excludeFlags(0)
{
    for (int i = 0; i < DT_MAX_AREAS; ++i)
        m_areaCost[i] = 1.0f;
}

dtStatus dtNavMeshQuery::findPath(
    dtPolyRef, dtPolyRef, const float*, const float*, const dtQueryFilter*, dtPolyRef*, int* pathCount, const int) const
{
    *pathCount = 0;
    return DT_FAILURE;
}

dtStatus dtNavMeshQuery::findStraightPath(const float*, const float*, const dtPolyRef*, const int, float*,
    unsigned char*, dtPolyRef*, int* straightPathCount, const int, const int) const
{
    *straightPathCount = 0;
    return DT_FAILURE;
}

dtStatus dtNavMeshQuery::findNearestPoly(
    const float*, const float*, const dtQueryFilter*, dtPolyRef* nearestRef, float*) const
{
    *nearestRef = 0;
    return DT_FAILURE;
}

dtStatus dtNavMeshQuery::closestPointOnPoly(dtPolyRef, const float*, float*, bool*) const
{
    return DT_FAILURE;
}
