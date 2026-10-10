#ifndef OPENMW_MWRENDER_CTR_GLOBALMAP_H
#define OPENMW_MWRENDER_CTR_GLOBALMAP_H

#include <memory>

#include <components/ctr/gpu/ctrgpu.hpp>

namespace ESM
{
    struct GlobalMap;
}

namespace SceneUtil
{
    class WorkQueue;
}

namespace MWRender
{
    class GlobalMap
    {
    public:
        GlobalMap(void* root, const std::shared_ptr<SceneUtil::WorkQueue>& workQueue);

        void render() { ensureLoaded(); }

        int getWidth() const { return mWidth; }
        int getHeight() const { return mHeight; }

        void worldPosToImageSpace(float x, float z, float& imageX, float& imageY);

        void exploreCell(int cellX, int cellY, const CtrGpu::TexturePtr& localMapTexture) {}

        void clear() {}

        void cleanupCameras() {}

        void write(ESM::GlobalMap& map);
        void read(ESM::GlobalMap& map) {}

        CtrGpu::TexturePtr getBaseTexture();
        CtrGpu::TexturePtr getOverlayTexture() { return nullptr; }

        void ensureLoaded();

        void asyncWritePng() {}

    private:
        int mWidth = 1;
        int mHeight = 1;
        int mMinX = 0, mMinY = 0, mMaxX = 0, mMaxY = 0;
        int mCellSize = 1;
        bool mLoaded = false;
        bool mBaseCreated = false;
        CtrGpu::TexturePtr mBaseTexture;
    };
}

#endif
