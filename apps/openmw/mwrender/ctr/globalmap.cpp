#include "globalmap.hpp"

#include <algorithm>
#include <cstdint>
#include <vector>

#include <components/ctr/gpu/imagedecode.hpp>
#include <components/debug/debuglog.hpp>
#include <components/esm3/loadland.hpp>
#include <components/resource/resourcesystem.hpp>

#include <components/esm3/globalmap.hpp>
#include <components/esm3/loadcell.hpp>
#include <components/misc/constants.hpp>
#include <components/settings/values.hpp>

#include "../../mwbase/environment.hpp"
#include "../../mwworld/esmstore.hpp"

namespace MWRender
{
    GlobalMap::GlobalMap(void* /*root*/, const std::shared_ptr<SceneUtil::WorkQueue>& /*workQueue*/) {}

    void GlobalMap::ensureLoaded()
    {
        if (mLoaded)
            return;
        mLoaded = true;
        const MWWorld::ESMStore& esmStore = *MWBase::Environment::get().getESMStore();
        bool first = true;
        for (auto it = esmStore.get<ESM::Cell>().extBegin(); it != esmStore.get<ESM::Cell>().extEnd(); ++it)
        {
            const int x = it->getGridX();
            const int y = it->getGridY();
            if (first)
            {
                mMinX = mMaxX = x;
                mMinY = mMaxY = y;
                first = false;
                continue;
            }
            mMinX = std::min(mMinX, x);
            mMaxX = std::max(mMaxX, x);
            mMinY = std::min(mMinY, y);
            mMaxY = std::max(mMaxY, y);
        }
        const int cells = std::max(mMaxX - mMinX + 1, mMaxY - mMinY + 1);
        mCellSize = std::clamp(std::min(Settings::map().mGlobalMapCellSize.get(), 1024 / cells), 1, 64);
        mWidth = mCellSize * (mMaxX - mMinX + 1);
        mHeight = mCellSize * (mMaxY - mMinY + 1);
    }

    CtrGpu::TexturePtr GlobalMap::getBaseTexture()
    {
        ensureLoaded();
        if (mBaseCreated)
            return mBaseTexture;
        mBaseCreated = true;

        const Resource::ResourceSystem* resourceSystem = MWBase::Environment::get().getResourceSystem();
        CtrGpu::RgbaImage lut;
        constexpr VFS::Path::NormalizedView colorLutPath("textures/omw_map_color_palette.dds");
        if (!CtrGpu::loadImage(*resourceSystem->getVFS(), colorLutPath, 256, lut) || lut.mWidth != 256)
        {
            Log(Debug::Error) << "Global map colour palette " << colorLutPath << " is missing or not 256x1";
            return nullptr;
        }

        const MWWorld::Store<ESM::Land>& landStore = MWBase::Environment::get().getESMStore()->get<ESM::Land>();
        std::vector<std::uint8_t> image(static_cast<std::size_t>(mWidth) * mHeight * 4);
        for (int x = mMinX; x <= mMaxX; ++x)
        {
            for (int y = mMinY; y <= mMaxY; ++y)
            {
                const ESM::Land* land = landStore.search(x, y);
                for (int cellY = 0; cellY < mCellSize; ++cellY)
                {
                    for (int cellX = 0; cellX < mCellSize; ++cellX)
                    {
                        const int vertexX = (cellX * 9) / mCellSize;
                        const int vertexY = (cellY * 9) / mCellSize;
                        int lutIndex = 0;
                        if (land != nullptr && (land->mDataTypes & ESM::Land::DATA_WNAM))
                            lutIndex = static_cast<int>(land->mWnam[vertexY * 9 + vertexX]) + 128;
                        const int texelX = (x - mMinX) * mCellSize + cellX;
                        const int texelY = mHeight - 1 - ((y - mMinY) * mCellSize + cellY);
                        const std::uint8_t* color = &lut.mPixels[static_cast<std::size_t>(lutIndex) * 4];
                        std::uint8_t* out = &image[(static_cast<std::size_t>(texelY) * mWidth + texelX) * 4];
                        out[0] = color[0];
                        out[1] = color[1];
                        out[2] = color[2];
                        out[3] = 255;
                    }
                }
            }
        }

        mBaseTexture = CtrGpu::createTextureFromRGBA8(
            image.data(), mWidth, mHeight, CtrGpu::TextureFormat::RGB565, 512, false, false);
        if (mBaseTexture)
        {
            mBaseTexture->setLinearFilter(true);
            mBaseTexture->setWrap(false);
        }
        return mBaseTexture;
    }

    void GlobalMap::worldPosToImageSpace(float x, float z, float& imageX, float& imageY)
    {
        ensureLoaded();
        imageX = (float(x / float(Constants::CellSizeInUnits) - mMinX) / (mMaxX - mMinX + 1)) * getWidth();
        imageY = (1.f - float(z / float(Constants::CellSizeInUnits) - mMinY) / (mMaxY - mMinY + 1)) * getHeight();
    }

    void GlobalMap::write(ESM::GlobalMap& map)
    {
        ensureLoaded();
        map.mBounds.mMinX = mMinX;
        map.mBounds.mMaxX = mMaxX;
        map.mBounds.mMinY = mMinY;
        map.mBounds.mMaxY = mMaxY;
    }
}
