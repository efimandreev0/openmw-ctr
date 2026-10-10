#include "context.hpp"

#include <components/resource/resourcesystem.hpp>
#include <components/settings/values.hpp>

namespace MWRender
{
    namespace
    {
        CtrContext* sContext = nullptr;
    }

    CtrContext::CtrContext(Resource::ResourceSystem* resourceSystem)
        : mResourceSystem(resourceSystem)
    {
        constexpr int maxTextureSize = 128;
        mTextures = std::make_unique<CtrScene::TextureCache>(resourceSystem->getVFS(), maxTextureSize);
        mModels = std::make_unique<CtrScene::ModelCache>(resourceSystem->getNifFileManager(), mTextures.get());
        mKeyframes = std::make_unique<CtrScene::KeyframeCache>(resourceSystem->getNifFileManager());
        mRenderer = std::make_unique<CtrScene::Renderer>();
        mRenderer->init();
        sContext = this;
    }

    CtrContext::~CtrContext()
    {
        if (sContext == this)
            sContext = nullptr;
        mRenderer.reset();
        mModels.reset();
        mKeyframes.reset();
        mTextures.reset();
    }

    CtrContext* CtrContext::get()
    {
        return sContext;
    }

    void CtrContext::purgeUnused()
    {
        mKeyframes->purgeUnused();
        mModels->purgeUnused();
        mTextures->purgeUnused();
    }
}
