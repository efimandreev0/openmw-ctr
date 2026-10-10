#ifndef OPENMW_MWRENDER_CTR_WATER_H
#define OPENMW_MWRENDER_CTR_WATER_H


#include <vector>

#include <osg/Vec3f>

#include <components/ctr/gpu/ctrgpu.hpp>
#include <components/ctr/scene/model.hpp>
#include <components/ctr/scene/renderer.hpp>

namespace CtrScene
{
    class TextureCache;
}

namespace MWRender
{
    class CtrWater
    {
    public:
        explicit CtrWater(CtrScene::TextureCache& textures);

        void draw(CtrScene::Renderer& renderer, const osg::Vec3f& cameraPosition, float height, float time);

    private:
        void build();

        CtrScene::TextureCache& mTextures;
        CtrScene::Mesh mMesh;
        std::vector<CtrGpu::TexturePtr> mFrames;
        bool mBuilt = false;
    };
}

#endif
