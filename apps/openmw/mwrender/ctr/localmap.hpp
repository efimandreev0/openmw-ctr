#ifndef OPENMW_MWRENDER_CTR_LOCALMAP_H
#define OPENMW_MWRENDER_CTR_LOCALMAP_H


#include <cstdint>
#include <map>
#include <utility>
#include <vector>

#include <MyGUI_Types.h>

#include <osg/BoundingBox>
#include <osg/Quat>
#include <osg/Vec2f>
#include <osg/Vec3f>

#include <components/ctr/gpu/ctrgpu.hpp>

namespace MWWorld
{
    class CellStore;
}

namespace MWRender
{
    class RenderingManager;

    class LocalMap
    {
    public:
        explicit LocalMap(void* root);
        ~LocalMap();

        static LocalMap* getInstance() { return sInstance; }

        void clear();

        void requestMap(const MWWorld::CellStore* cell);

        void addCell(MWWorld::CellStore* cell);
        void removeExteriorCell(int x, int y);
        void removeCell(MWWorld::CellStore* cell);

        CtrGpu::TexturePtr getMapTexture(int x, int y);
        CtrGpu::TexturePtr getFogOfWarTexture(int x, int y) { return nullptr; }

        void cleanupCameras() {}

        void updatePlayer(const osg::Vec3f& position, const osg::Quat& orientation, float& u, float& v, int& x, int& y,
            osg::Vec3f& direction);

        void saveFogOfWar(MWWorld::CellStore* cell) const {}

        void worldToInteriorMapPosition(osg::Vec2f pos, float& nX, float& nY, int& x, int& y) const;
        osg::Vec2f interiorMapToWorldPosition(float nX, float nY, int x, int y) const;

        bool isPositionExplored(float nX, float nY, int x, int y) { return true; }

        void* getRoot() { return mRoot; }

        MyGUI::IntRect getInteriorGrid() const;

        void renderPending(RenderingManager& rendering);

    private:
        struct MapSegment
        {
            CtrGpu::TexturePtr mMapTexture;
            std::uint8_t mLastRenderNeighbourFlags = 0;
        };

        struct Request
        {
            CtrGpu::TexturePtr mTexture;
            osg::Vec2f mCenter;
            osg::Vec2f mUp;
            float mZMin;
            float mZMax;
            int mRetries = 0;
        };

        using SegmentMap = std::map<std::pair<int, int>, MapSegment>;

        void requestExteriorMap(const MWWorld::CellStore* cell, MapSegment& segment);
        void requestInteriorMap(const MWWorld::CellStore* cell);
        void setupRenderToTexture(
            int segmentX, int segmentY, const osg::Vec2f& center, const osg::Vec2f& up, float zMin, float zMax);
        std::uint8_t getExteriorNeighbourFlags(int cellX, int cellY) const;
        osg::Vec2f rotatePoint(const osg::Vec2f& point, const osg::Vec2f& center, float angle) const;

        static LocalMap* sInstance;

        void* mRoot;
        SegmentMap mExteriorSegments;
        SegmentMap mInteriorSegments;
        std::vector<Request> mRequests;

        int mMapResolution;
        int mMapWorldSize;
        int mCellDistance;

        float mAngle = 0.f;
        osg::BoundingBox mBounds;
        osg::Vec2f mCenter;
        bool mInterior = false;
    };
}

#endif
