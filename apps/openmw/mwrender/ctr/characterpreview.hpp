#ifndef OPENMW_MWRENDER_CTR_CHARACTERPREVIEW_H
#define OPENMW_MWRENDER_CTR_CHARACTERPREVIEW_H

#include <osg/ref_ptr>

#include <components/ctr/gpu/ctrgpu.hpp>
#include <components/esm3/loadnpc.hpp>
#include <components/sceneutil/positionattitudetransform.hpp>

#include "../../mwworld/livecellref.hpp"
#include "../../mwworld/ptr.hpp"

namespace Resource
{
    class ResourceSystem;
}

namespace MWRender
{
    class NpcAnimation;
    class RenderingManager;

    class CharacterPreview
    {
    public:
        virtual ~CharacterPreview() = default;

        int getTextureWidth() const { return 256; }
        int getTextureHeight() const { return 256; }

        void redraw() {}
        void rebuild() {}

        CtrGpu::TexturePtr getTexture() { return nullptr; }
        int getTextureStateSet() { return 0; }
    };

    class InventoryPreview : public CharacterPreview
    {
    public:
        InventoryPreview(void* parent, Resource::ResourceSystem* resourceSystem, const MWWorld::Ptr& character) {}

        void updatePtr(const MWWorld::Ptr& ptr) {}
        void update() {}
        void setViewport(int sizeX, int sizeY) {}

        int getSlotSelected(int posX, int posY) { return -1; }
    };

    class RaceSelectionPreview : public CharacterPreview
    {
    public:
        RaceSelectionPreview(void* parent, Resource::ResourceSystem* resourceSystem);
        ~RaceSelectionPreview() override;

        void setAngle(float angleRadians);

        const ESM::NPC& getPrototype() const { return mBase; }
        void setPrototype(const ESM::NPC& proto);

        void rebuild();
        void redraw() { mDirty = true; }

        CtrGpu::TexturePtr getTexture() { return mTexture; }

        static void renderPending(RenderingManager& rendering);

    private:
        Resource::ResourceSystem* mResourceSystem;
        ESM::NPC mBase;
        MWWorld::LiveCellRef<ESM::NPC> mRef;
        MWWorld::Ptr mCharacter;
        osg::ref_ptr<SceneUtil::PositionAttitudeTransform> mNode;
        osg::ref_ptr<NpcAnimation> mAnimation;
        CtrGpu::TexturePtr mTexture;
        float mPitchRadians;
        bool mDirty = true;

        static RaceSelectionPreview* sActive;
    };
}

#endif
