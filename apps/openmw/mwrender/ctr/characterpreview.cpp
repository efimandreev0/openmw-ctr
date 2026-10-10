#include "characterpreview.hpp"

#include <osg/Quat>

#include <components/debug/debuglog.hpp>
#include <components/esm3/cellref.hpp>

#include "../../mwmechanics/actorutil.hpp"
#include "../../mwworld/class.hpp"

#include "../blendmask.hpp"
#include "npcanimation.hpp"
#include "renderingmanager.hpp"

namespace MWRender
{
    namespace
    {
        constexpr float sFovYDegrees = 12.3f;
        const osg::Vec3f sEyeOffset(0.f, 125.f, 8.f);
        const osg::Vec3f sLookAtOffset(0.f, 0.f, 8.f);
        constexpr int sTextureSize = 256;
    }

    RaceSelectionPreview* RaceSelectionPreview::sActive = nullptr;

    RaceSelectionPreview::RaceSelectionPreview(void* /*parent*/, Resource::ResourceSystem* resourceSystem)
        : mResourceSystem(resourceSystem)
        , mBase(*MWMechanics::getPlayer().get<ESM::NPC>()->mBase)
        , mRef(ESM::makeBlankCellRef(), &mBase)
        , mCharacter(&mRef, nullptr)
        , mNode(new SceneUtil::PositionAttitudeTransform)
        , mPitchRadians(osg::DegreesToRadians(6.f))
    {
        auto texture = std::make_shared<CtrGpu::Texture>();
        if (texture->create(sTextureSize, sTextureSize, CtrGpu::TextureFormat::RGB565, true))
            mTexture = std::move(texture);
        else
            Log(Debug::Error) << "No memory for the race preview";
        sActive = this;
    }

    RaceSelectionPreview::~RaceSelectionPreview()
    {
        if (sActive == this)
            sActive = nullptr;
    }

    void RaceSelectionPreview::setAngle(float angleRadians)
    {
        mNode->setAttitude(osg::Quat(mPitchRadians, osg::Vec3f(1, 0, 0)) * osg::Quat(angleRadians, osg::Vec3f(0, 0, 1)));
        mDirty = true;
    }

    void RaceSelectionPreview::setPrototype(const ESM::NPC& proto)
    {
        mBase = proto;
        mBase.mId = ESM::RefId::stringRefId("Player");
        rebuild();
    }

    void RaceSelectionPreview::rebuild()
    {
        mAnimation = nullptr;
        try
        {
            mAnimation = new NpcAnimation(mCharacter, mNode.get(), mResourceSystem, true, NpcAnimation::VM_HeadOnly);
            mAnimation->play("idle", 1, BlendMask_All, false, 1.0f, "start", "stop", 0.0f, 0);
            mAnimation->runAnimation(0.f);
        }
        catch (const std::exception& e)
        {
            Log(Debug::Error) << "Failed to build the race preview: " << e.what();
            mAnimation = nullptr;
        }
        mDirty = true;
    }

    void RaceSelectionPreview::renderPending(RenderingManager& rendering)
    {
        RaceSelectionPreview* preview = sActive;
        if (preview == nullptr || !preview->mDirty || !preview->mTexture)
            return;
        preview->mDirty = false;

        osg::Matrixf head;
        osg::Vec3f headPosition = preview->mNode->getPosition();
        const bool foundHead
            = preview->mAnimation && preview->mAnimation->getNodeWorldMatrix("Bip01 Head", head);
        if (foundHead)
            headPosition = head.getTrans();

        osg::Matrixf view;
        view.makeLookAt(headPosition + sEyeOffset, headPosition + sLookAtOffset, osg::Vec3f(0.f, 0.f, 1.f));
        const std::uint32_t draws = rendering.renderCharacterPreview(
            *preview->mTexture, preview->mAnimation.get(), view, sFovYDegrees);
        Log(Debug::Info) << "[Preview] animation=" << (preview->mAnimation != nullptr) << " head=" << foundHead
                         << " at " << headPosition.x() << "," << headPosition.y() << "," << headPosition.z()
                         << " draws=" << draws << " vram=" << preview->mTexture->inVram();
    }
}
