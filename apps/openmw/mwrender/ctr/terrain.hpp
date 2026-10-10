#ifndef OPENMW_MWRENDER_CTR_TERRAIN_H
#define OPENMW_MWRENDER_CTR_TERRAIN_H

#include <map>
#include <memory>
#include <utility>
#include <vector>

#include <components/ctr/scene/model.hpp>
#include <components/ctr/scene/renderer.hpp>

namespace MWRender
{
    class LandManager;

    class CtrTerrain
    {
    public:
        explicit CtrTerrain(LandManager* landManager);
        ~CtrTerrain();

        void addCell(int x, int y);
        void removeCell(int x, int y);
        void clear();

        void setEnabled(bool enabled) { mEnabled = enabled; }
        bool isEnabled() const { return mEnabled; }

        void draw(CtrScene::Renderer& renderer, const CtrScene::DrawParams& params) const;

    private:
        struct Chunk;
        LandManager* mLandManager;
        std::map<std::pair<int, int>, std::unique_ptr<Chunk>> mChunks;
        bool mEnabled = true;

        std::unique_ptr<Chunk> build(int x, int y);
    };
}

#endif
