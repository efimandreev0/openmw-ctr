#ifndef OPENMW_MWRENDER_CTR_SKYRENDER_H
#define OPENMW_MWRENDER_CTR_SKYRENDER_H


#include <string>

#include <osg/Matrixf>
#include <osg/Vec4f>

#include <components/ctr/gpu/ctrgpu.hpp>
#include <components/ctr/scene/model.hpp>
#include <components/ctr/scene/renderer.hpp>

namespace CtrScene
{
    class TextureCache;
}

namespace MWRender
{
    class SkyManager;

    class CtrSkyRenderer
    {
    public:
        explicit CtrSkyRenderer(CtrScene::TextureCache& textures);

        void draw(CtrScene::Renderer& renderer, const SkyManager& sky, const osg::Matrixf& view, float fovY,
            const osg::Vec4f& clearColor, const osg::Vec4f& fogColor);

    private:
        void build();
        CtrGpu::TexturePtr getCloudTexture(const std::string& name, int slot);

        CtrScene::TextureCache& mTextures;
        CtrScene::Mesh mClouds;
        CtrScene::Mesh mSun;
        std::string mCloudNames[2];
        CtrGpu::TexturePtr mCloudTextures[2];
        bool mBuilt = false;
    };
}

#endif
