#ifndef OPENMW_MWRENDER_CTR_ACTORANIMATION_H
#define OPENMW_MWRENDER_CTR_ACTORANIMATION_H

#include <map>

#include <components/vfs/pathutil.hpp>

#include "../../mwworld/containerstore.hpp"

#include "animation.hpp"

namespace MWWorld
{
    class ConstPtr;
}

namespace MWRender
{
    class ActorAnimation : public Animation, public MWWorld::ContainerStoreListener
    {
    public:
        ActorAnimation(const MWWorld::Ptr& ptr, SceneUtil::PositionAttitudeTransform* parentNode,
            Resource::ResourceSystem* resourceSystem);
        virtual ~ActorAnimation();

        void itemAdded(const MWWorld::ConstPtr& item, int count) override;
        void itemRemoved(const MWWorld::ConstPtr& item, int count) override;
        virtual bool isArrowAttached() const { return false; }
        bool useShieldAnimations() const override;
        bool updateCarriedLeftVisible(ESM::RefId weaptype) const override;

        void collectLights(std::vector<const CtrScene::PointLight*>& out, float time) override;

    protected:
        bool hasBone(std::string_view boneName) const { return hasNode(boneName); }
        VFS::Path::Normalized getShieldMesh(const MWWorld::ConstPtr& shield, bool female) const;
        virtual std::string getSheathedShieldMesh(const MWWorld::ConstPtr& shield) const;
        std::string_view getHolsteredWeaponBoneName(const MWWorld::ConstPtr& weapon);

        PartHolderPtr attachMesh(VFS::Path::NormalizedView model, std::string_view bonename);
        PartHolderPtr attach(
            VFS::Path::NormalizedView model, std::string_view bonename, std::string_view bonefilter, bool isLight);

        void updateHolsteredWeapon(bool /*showHolsteredWeapons*/) {}
        void updateHolsteredShield(bool /*showCarriedLeft*/) {}
        void updateQuiver() {}

    private:
        void addHiddenItemLight(const MWWorld::ConstPtr& item, const ESM::Light* esmLight);
        void removeHiddenItemLight(const MWWorld::ConstPtr& item);

        typedef std::map<MWWorld::ConstPtr, CtrScene::PointLight> ItemLightMap;
        ItemLightMap mItemLights;
    };
}

#endif
