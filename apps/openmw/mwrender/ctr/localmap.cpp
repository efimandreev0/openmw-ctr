#include "localmap.hpp"

#include <algorithm>
#include <cmath>
#include <tuple>

#include <components/ctr/gpu/ctrgpu.hpp>
#include <components/debug/debuglog.hpp>
#include <components/esm3/fogstate.hpp>
#include <components/esm3/loadcell.hpp>
#include <components/misc/constants.hpp>

#include "../../mwworld/cellstore.hpp"

#include "renderingmanager.hpp"

namespace MWRender
{
    namespace
    {
        constexpr int sMapResolution = 128;
        constexpr int sRendersPerFrame = 1;
        constexpr float sMaxCommandBufferUsage = 0.3f;

        enum NeighbourCellFlag : std::uint8_t
        {
            NeighbourCellTopLeft = 1,
            NeighbourCellTopCenter = 1 << 1,
            NeighbourCellTopRight = 1 << 2,
            NeighbourCellMiddleLeft = 1 << 3,
            NeighbourCellMiddleRight = 1 << 4,
            NeighbourCellBottomLeft = 1 << 5,
            NeighbourCellBottomCenter = 1 << 6,
            NeighbourCellBottomRight = 1 << 7,
        };

        std::pair<int, int> divideIntoSegments(const osg::BoundingBox& bounds, int mapSize)
        {
            osg::Vec2f min(bounds.xMin(), bounds.yMin());
            osg::Vec2f max(bounds.xMax(), bounds.yMax());
            osg::Vec2f length = max - min;
            const int segsX = static_cast<int>(std::ceil(length.x() / mapSize));
            const int segsY = static_cast<int>(std::ceil(length.y() / mapSize));
            return { segsX, segsY };
        }

        osg::Vec2f getNorthVector(const MWWorld::CellStore* cell)
        {
            MWWorld::ConstPtr northmarker = cell->searchConst(ESM::RefId::stringRefId("northmarker"));

            if (northmarker.isEmpty())
                return osg::Vec2f(0, 1);

            osg::Quat orient(-northmarker.getRefData().getPosition().rot[2], osg::Vec3f(0, 0, 1));
            osg::Vec3f dir = orient * osg::Vec3f(0, 1, 0);
            return osg::Vec2f(dir.x(), dir.y());
        }
    }

    LocalMap* LocalMap::sInstance = nullptr;

    LocalMap::LocalMap(void* root)
        : mRoot(root)
        , mMapResolution(sMapResolution)
        , mMapWorldSize(Constants::CellSizeInUnits)
        , mCellDistance(Constants::CellGridRadius)
    {
        sInstance = this;
    }

    LocalMap::~LocalMap()
    {
        if (sInstance == this)
            sInstance = nullptr;
    }

    osg::Vec2f LocalMap::rotatePoint(const osg::Vec2f& point, const osg::Vec2f& center, float angle) const
    {
        return osg::Vec2f(
            std::cos(angle) * (point.x() - center.x()) - std::sin(angle) * (point.y() - center.y()) + center.x(),
            std::sin(angle) * (point.x() - center.x()) + std::cos(angle) * (point.y() - center.y()) + center.y());
    }

    void LocalMap::clear()
    {
        mExteriorSegments.clear();
        mInteriorSegments.clear();
        mRequests.clear();
    }

    void LocalMap::setupRenderToTexture(
        int segmentX, int segmentY, const osg::Vec2f& center, const osg::Vec2f& up, float zMin, float zMax)
    {
        MapSegment& segment = mInterior ? mInteriorSegments[std::make_pair(segmentX, segmentY)]
                                        : mExteriorSegments[std::make_pair(segmentX, segmentY)];
        if (segment.mMapTexture == nullptr)
        {
            auto texture = std::make_shared<CtrGpu::Texture>();
            if (!texture->create(mMapResolution, mMapResolution, CtrGpu::TextureFormat::RGB565, true))
            {
                Log(Debug::Warning) << "No memory for a local map texture";
                return;
            }
            texture->setLinearFilter(true);
            texture->setWrap(false);
            segment.mMapTexture = std::move(texture);
        }
        mRequests.push_back({ segment.mMapTexture, center, up, zMin, zMax });
    }

    void LocalMap::requestMap(const MWWorld::CellStore* cell)
    {
        if (!cell->isExterior())
        {
            requestInteriorMap(cell);
            return;
        }

        const int cellX = cell->getCell()->getGridX();
        const int cellY = cell->getCell()->getGridY();

        MapSegment& segment = mExteriorSegments[std::make_pair(cellX, cellY)];
        const std::uint8_t neighbourFlags = getExteriorNeighbourFlags(cellX, cellY);
        if (segment.mLastRenderNeighbourFlags != 0
            && (segment.mLastRenderNeighbourFlags & neighbourFlags) == neighbourFlags)
            return;
        requestExteriorMap(cell, segment);
        segment.mLastRenderNeighbourFlags = neighbourFlags;
    }

    void LocalMap::addCell(MWWorld::CellStore* cell)
    {
        if (cell->isExterior())
            mExteriorSegments.emplace(
                std::make_pair(cell->getCell()->getGridX(), cell->getCell()->getGridY()), MapSegment{});
    }

    void LocalMap::removeExteriorCell(int x, int y)
    {
        mExteriorSegments.erase({ x, y });
    }

    void LocalMap::removeCell(MWWorld::CellStore* cell)
    {
        if (!cell->isExterior())
            mInteriorSegments.clear();
    }

    CtrGpu::TexturePtr LocalMap::getMapTexture(int x, int y)
    {
        auto& segments(mInterior ? mInteriorSegments : mExteriorSegments);
        const auto found = segments.find(std::make_pair(x, y));
        if (found == segments.end())
            return nullptr;
        return found->second.mMapTexture;
    }

    void LocalMap::requestExteriorMap(const MWWorld::CellStore* cell, MapSegment& segment)
    {
        mInterior = false;

        const int x = cell->getCell()->getGridX();
        const int y = cell->getCell()->getGridY();

        setupRenderToTexture(x, y,
            osg::Vec2f(x * mMapWorldSize + mMapWorldSize / 2.f, y * mMapWorldSize + mMapWorldSize / 2.f),
            osg::Vec2f(0, 1), -20000.f, 20000.f);
    }

    void LocalMap::requestInteriorMap(const MWWorld::CellStore* cell)
    {
        RenderingManager* rendering = RenderingManager::getInstance();
        if (rendering == nullptr)
            return;
        osg::BoundingBox bounds = rendering->getStaticSceneBounds();

        if (!bounds.valid() || bounds.radius2() == 0.0)
            return;

        mInterior = true;
        mExteriorSegments.clear();

        mBounds = bounds;

        const osg::Vec2f north = getNorthVector(cell);

        mAngle = std::atan2(north.x(), north.y());

        const osg::Vec2f origCenter(bounds.center().x(), bounds.center().y());
        osg::Vec3f origCorners[8];
        for (int i = 0; i < 8; ++i)
            origCorners[i] = mBounds.corner(i);

        for (int i = 0; i < 8; ++i)
        {
            const osg::Vec3f corner = origCorners[i];
            osg::Vec2f corner2d(corner.x(), corner.y());
            corner2d = rotatePoint(corner2d, origCenter, mAngle);
            mBounds.expandBy(osg::Vec3f(corner2d.x(), corner2d.y(), 0));
        }

        const float padding = 500.0f;
        mBounds.set(mBounds._min - osg::Vec3f(padding, padding, 0.f), mBounds._max + osg::Vec3f(padding, padding, 0.f));

        const float zMin = mBounds.zMin();
        const float zMax = mBounds.zMax();
        mCenter = osg::Vec2f(mBounds.center().x(), mBounds.center().y());

        if (const ESM::FogState* fog = cell->getFog())
        {
            if (std::abs(mAngle - fog->mNorthMarkerAngle) < osg::DegreesToRadians(5.f))
            {
                if (fog->mBounds.mMinX < mBounds.xMin())
                    mBounds.xMin() = fog->mBounds.mMinX;
                else if (fog->mBounds.mMinX > mBounds.xMin())
                {
                    const float diff = fog->mBounds.mMinX - mBounds.xMin();
                    const int xOffset = static_cast<int>(std::ceil(diff / mMapWorldSize));
                    mBounds.xMin() = fog->mBounds.mMinX - xOffset * mMapWorldSize;
                }
                if (fog->mBounds.mMinY < mBounds.yMin())
                    mBounds.yMin() = fog->mBounds.mMinY;
                else if (fog->mBounds.mMinY > mBounds.yMin())
                {
                    const float diff = fog->mBounds.mMinY - mBounds.yMin();
                    const int yOffset = static_cast<int>(std::ceil(diff / mMapWorldSize));
                    mBounds.yMin() = fog->mBounds.mMinY - yOffset * mMapWorldSize;
                }
                if (fog->mBounds.mMaxX > mBounds.xMax())
                    mBounds.xMax() = fog->mBounds.mMaxX;
                if (fog->mBounds.mMaxY > mBounds.yMax())
                    mBounds.yMax() = fog->mBounds.mMaxY;

                mAngle = fog->mNorthMarkerAngle;
                mCenter.x() = fog->mCenterX;
                mCenter.y() = fog->mCenterY;
            }
        }

        const osg::Vec2f min(mBounds.xMin(), mBounds.yMin());
        const osg::Quat cameraOrient(mAngle, osg::Vec3d(0, 0, -1));

        const auto segments = divideIntoSegments(mBounds, mMapWorldSize);
        for (int x = 0; x < segments.first; ++x)
        {
            for (int y = 0; y < segments.second; ++y)
            {
                const osg::Vec2f start
                    = min + osg::Vec2f(static_cast<float>(mMapWorldSize * x), static_cast<float>(mMapWorldSize * y));
                const osg::Vec2f newcenter = start + osg::Vec2f(mMapWorldSize / 2.f, mMapWorldSize / 2.f);

                const osg::Vec2f a = newcenter - mCenter;
                const osg::Vec3f rotatedCenter = cameraOrient * (osg::Vec3f(a.x(), a.y(), 0));

                const osg::Vec2f pos = osg::Vec2f(rotatedCenter.x(), rotatedCenter.y()) + mCenter;

                setupRenderToTexture(x, y, pos, north, zMin, zMax);
            }
        }
    }

    void LocalMap::worldToInteriorMapPosition(osg::Vec2f pos, float& nX, float& nY, int& x, int& y) const
    {
        pos = rotatePoint(pos, mCenter, mAngle);

        const osg::Vec2f min(mBounds.xMin(), mBounds.yMin());

        x = static_cast<int>(std::ceil((pos.x() - min.x()) / mMapWorldSize) - 1);
        y = static_cast<int>(std::ceil((pos.y() - min.y()) / mMapWorldSize) - 1);

        nX = (pos.x() - min.x() - mMapWorldSize * x) / mMapWorldSize;
        nY = 1.0f - (pos.y() - min.y() - mMapWorldSize * y) / mMapWorldSize;
    }

    osg::Vec2f LocalMap::interiorMapToWorldPosition(float nX, float nY, int x, int y) const
    {
        const osg::Vec2f min(mBounds.xMin(), mBounds.yMin());
        osg::Vec2f pos(mMapWorldSize * (nX + x) + min.x(), mMapWorldSize * (1.0f - nY + y) + min.y());

        pos = rotatePoint(pos, mCenter, -mAngle);
        return pos;
    }

    void LocalMap::updatePlayer(const osg::Vec3f& position, const osg::Quat& orientation, float& u, float& v, int& x,
        int& y, osg::Vec3f& direction)
    {
        const osg::Vec2f pos(position.x(), position.y());

        if (mInterior)
        {
            worldToInteriorMapPosition(pos, u, v, x, y);

            const osg::Quat cameraOrient(mAngle, osg::Vec3(0, 0, -1));
            direction = orientation * cameraOrient.inverse() * osg::Vec3f(0, 1, 0);
        }
        else
        {
            direction = orientation * osg::Vec3f(0, 1, 0);

            x = static_cast<int>(std::ceil(pos.x() / mMapWorldSize) - 1);
            y = static_cast<int>(std::ceil(pos.y() / mMapWorldSize) - 1);

            u = std::abs((pos.x() - (mMapWorldSize * x)) / mMapWorldSize);
            v = 1.0f - std::abs((pos.y() - (mMapWorldSize * y)) / mMapWorldSize);
        }
    }

    MyGUI::IntRect LocalMap::getInteriorGrid() const
    {
        const auto segments = divideIntoSegments(mBounds, mMapWorldSize);
        return { -1, -1, segments.first, segments.second };
    }

    std::uint8_t LocalMap::getExteriorNeighbourFlags(int cellX, int cellY) const
    {
        constexpr std::tuple<NeighbourCellFlag, int, int> flags[] = {
            { NeighbourCellTopLeft, -1, -1 },
            { NeighbourCellTopCenter, 0, -1 },
            { NeighbourCellTopRight, 1, -1 },
            { NeighbourCellMiddleLeft, -1, 0 },
            { NeighbourCellMiddleRight, 1, 0 },
            { NeighbourCellBottomLeft, -1, 1 },
            { NeighbourCellBottomCenter, 0, 1 },
            { NeighbourCellBottomRight, 1, 1 },
        };
        std::uint8_t result = 0;
        for (const auto& [flag, dx, dy] : flags)
        {
            const auto it = mExteriorSegments.find(std::pair(cellX + dx, cellY + dy));
            if (it != mExteriorSegments.end() && it->second.mMapTexture)
                result |= flag;
        }
        return result;
    }

    void LocalMap::renderPending(RenderingManager& rendering)
    {
        int rendered = 0;
        while (!mRequests.empty() && rendered < sRendersPerFrame)
        {
            if (CtrGpu::commandBufferUsage() > sMaxCommandBufferUsage)
                return;
            Request request = std::move(mRequests.front());
            mRequests.erase(mRequests.begin());
            if (request.mTexture.use_count() <= 1)
                continue;
            const bool complete = rendering.renderMapView(*request.mTexture, request.mCenter, request.mUp,
                mMapWorldSize / 2.f, request.mZMin, request.mZMax);
            ++rendered;
            if (!complete && ++request.mRetries < 3)
                mRequests.push_back(std::move(request));
        }
    }
}
