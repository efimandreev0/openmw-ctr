#ifndef OPENMW_MWRENDER_CTR_CONTEXT_H
#define OPENMW_MWRENDER_CTR_CONTEXT_H

#include <memory>

#include <components/ctr/scene/keyframes.hpp>
#include <components/ctr/scene/model.hpp>
#include <components/ctr/scene/renderer.hpp>
#include <components/ctr/scene/texturecache.hpp>

namespace Resource
{
    class ResourceSystem;
}

namespace MWRender
{
    class CtrContext
    {
    public:
        explicit CtrContext(Resource::ResourceSystem* resourceSystem);
        ~CtrContext();

        static CtrContext* get();

        CtrScene::TextureCache& getTextures() { return *mTextures; }
        CtrScene::ModelCache& getModels() { return *mModels; }
        CtrScene::KeyframeCache& getKeyframes() { return *mKeyframes; }
        CtrScene::Renderer& getRenderer() { return *mRenderer; }
        Resource::ResourceSystem* getResourceSystem() const { return mResourceSystem; }

        void purgeUnused();

    private:
        Resource::ResourceSystem* mResourceSystem;
        std::unique_ptr<CtrScene::TextureCache> mTextures;
        std::unique_ptr<CtrScene::ModelCache> mModels;
        std::unique_ptr<CtrScene::KeyframeCache> mKeyframes;
        std::unique_ptr<CtrScene::Renderer> mRenderer;
    };
}

#endif
