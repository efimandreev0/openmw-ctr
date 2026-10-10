#ifndef OPENMW_MWRENDER_CTR_CREATUREANIMATION_H
#define OPENMW_MWRENDER_CTR_CREATUREANIMATION_H

#include "../../mwworld/inventorystore.hpp"

#include "actoranimation.hpp"
#include "weaponanimation.hpp"

namespace MWRender
{
    class CreatureAnimation : public ActorAnimation
    {
    public:
        CreatureAnimation(
            const MWWorld::Ptr& ptr, const std::string& model, Resource::ResourceSystem* resourceSystem, bool animated);
        virtual ~CreatureAnimation() {}
    };

    class CreatureWeaponAnimation : public ActorAnimation,
                                    public WeaponAnimation,
                                    public MWWorld::InventoryStoreListener
    {
    public:
        CreatureWeaponAnimation(
            const MWWorld::Ptr& ptr, const std::string& model, Resource::ResourceSystem* resourceSystem, bool animated);
        virtual ~CreatureWeaponAnimation() {}

        void equipmentChanged() override { updateParts(); }

        bool getWeaponsShown() const override { return mShowWeapons; }
        void showWeapons(bool showWeapon) override;

        bool getCarriedLeftShown() const override { return mShowCarriedLeft; }
        void showCarriedLeft(bool show) override;

        void updateParts();

        void updatePart(PartHolderPtr& scene, int slot);

        void attachArrow() override;
        void detachArrow() override;
        void releaseArrow(float attackStrength, float attackWindUp) override;

        void showWeapon(bool show) override { showWeapons(show); }
        void setWeaponGroup(const std::string& group, bool relativeDuration) override
        {
            mWeaponAnimationTime->setGroup(group, relativeDuration);
        }

        void addControllers() override;

        osg::Vec3f runAnimation(float duration) override;

        void setPitchFactor(float factor) override { mPitchFactor = factor; }

    protected:
        bool isArrowAttached() const override;

        PartHolderPtr attachAmmunition(VFS::Path::NormalizedView model) override;
        bool getAmmunitionWorldPosition(osg::Vec3f& out) override;
        bool getWeaponWorldPosition(osg::Vec3f& out) override;
        void setSpineRotation(const osg::Quat& rotate, bool enabled) override;

    private:
        PartHolderPtr mWeapon;
        PartHolderPtr mShield;
        bool mShowWeapons;
        bool mShowCarriedLeft;
        RotateController mWeaponSpine[2];

        std::shared_ptr<WeaponAnimationTime> mWeaponAnimationTime;
    };
}

#endif
