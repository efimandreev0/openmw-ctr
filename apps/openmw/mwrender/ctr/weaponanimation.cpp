#include "weaponanimation.hpp"

#include <components/esm3/loadgmst.hpp>
#include <components/esm3/loadweap.hpp>

#include "../../mwbase/environment.hpp"
#include "../../mwbase/soundmanager.hpp"
#include "../../mwbase/world.hpp"

#include "../../mwworld/class.hpp"
#include "../../mwworld/esmstore.hpp"
#include "../../mwworld/inventorystore.hpp"

#include "../../mwmechanics/combat.hpp"
#include "../../mwmechanics/weapontype.hpp"

namespace MWRender
{
    float WeaponAnimationTime::getValue() const
    {
        if (mWeaponGroup.empty())
            return 0;

        float current = mAnimation->getCurrentTime(mWeaponGroup);
        if (current == -1)
            return 0;
        return current - mStartTime;
    }

    void WeaponAnimationTime::setGroup(const std::string& group, bool relativeTime)
    {
        mWeaponGroup = group;
        mRelativeTime = relativeTime;

        if (mRelativeTime)
            mStartTime = mAnimation->getStartTime(mWeaponGroup);
        else
            mStartTime = 0;
    }

    void WeaponAnimationTime::updateStartTime()
    {
        setGroup(mWeaponGroup, mRelativeTime);
    }

    WeaponAnimation::WeaponAnimation()
        : mPitchFactor(0)
    {
    }

    WeaponAnimation::~WeaponAnimation() = default;

    void WeaponAnimation::attachArrow(const MWWorld::Ptr& actor)
    {
        const MWWorld::InventoryStore& inv = actor.getClass().getInventoryStore(actor);
        MWWorld::ConstContainerStoreIterator weaponSlot = inv.getSlot(MWWorld::InventoryStore::Slot_CarriedRight);
        if (weaponSlot == inv.end())
            return;
        if (weaponSlot->getType() != ESM::Weapon::sRecordId)
            return;

        const ESM::RefId type = weaponSlot->get<ESM::Weapon>()->mBase->mData.mType;
        ESM::WeaponType::Class weapclass = MWMechanics::getWeaponType(type)->mWeaponClass;
        if (weapclass == ESM::WeaponType::Thrown)
        {
            const auto& soundid = weaponSlot->getClass().getUpSoundId(*weaponSlot);
            if (!soundid.empty())
            {
                MWBase::SoundManager* sndMgr = MWBase::Environment::get().getSoundManager();
                sndMgr->playSound3D(actor, soundid, 1.0f, 1.0f);
            }
            showWeapon(true);
        }
        else if (weapclass == ESM::WeaponType::Ranged)
        {
            MWWorld::ConstContainerStoreIterator ammo = inv.getSlot(MWWorld::InventoryStore::Slot_Ammunition);
            if (ammo == inv.end())
                return;
            const VFS::Path::Normalized model(ammo->getClass().getCorrectedModel(*ammo));
            mAmmunition = attachAmmunition(model);
        }
    }

    void WeaponAnimation::detachArrow(MWWorld::Ptr actor)
    {
        mAmmunition.reset();
    }

    void WeaponAnimation::releaseArrow(MWWorld::Ptr actor, float attackStrength, float attackWindUp)
    {
        MWWorld::InventoryStore& inv = actor.getClass().getInventoryStore(actor);
        MWWorld::ContainerStoreIterator weapon = inv.getSlot(MWWorld::InventoryStore::Slot_CarriedRight);
        if (weapon == inv.end())
            return;
        if (weapon->getType() != ESM::Weapon::sRecordId)
            return;

        osg::Quat orient = osg::Quat(actor.getRefData().getPosition().rot[0], osg::Vec3f(-1, 0, 0))
            * osg::Quat(actor.getRefData().getPosition().rot[2], osg::Vec3f(0, 0, -1));

        const MWWorld::Store<ESM::GameSetting>& gmst
            = MWBase::Environment::get().getESMStore()->get<ESM::GameSetting>();

        MWMechanics::applyFatigueLoss(actor, *weapon, attackStrength);

        if (MWMechanics::getWeaponType(weapon->get<ESM::Weapon>()->mBase->mData.mType)->mWeaponClass
            == ESM::WeaponType::Thrown)
        {
            osg::Vec3f launchPos;
            if (!getWeaponWorldPosition(launchPos))
                return;

            float fThrownWeaponMinSpeed = gmst.find("fThrownWeaponMinSpeed")->mValue.getFloat();
            float fThrownWeaponMaxSpeed = gmst.find("fThrownWeaponMaxSpeed")->mValue.getFloat();
            float speed = fThrownWeaponMinSpeed + (fThrownWeaponMaxSpeed - fThrownWeaponMinSpeed) * attackStrength;

            MWWorld::Ptr weaponPtr = *weapon;
            MWBase::Environment::get().getWorld()->launchProjectile(
                actor, weaponPtr, launchPos, orient, weaponPtr, speed, attackStrength, attackWindUp);

            showWeapon(false);

            inv.remove(*weapon, 1);
        }
        else
        {
            MWWorld::ContainerStoreIterator ammo = inv.getSlot(MWWorld::InventoryStore::Slot_Ammunition);
            if (ammo == inv.end())
                return;

            if (!mAmmunition)
                return;

            osg::Vec3f launchPos;
            if (!getAmmunitionWorldPosition(launchPos))
                return;

            float fProjectileMinSpeed = gmst.find("fProjectileMinSpeed")->mValue.getFloat();
            float fProjectileMaxSpeed = gmst.find("fProjectileMaxSpeed")->mValue.getFloat();
            float speed = fProjectileMinSpeed + (fProjectileMaxSpeed - fProjectileMinSpeed) * attackStrength;

            MWWorld::Ptr weaponPtr = *weapon;
            MWWorld::Ptr ammoPtr = *ammo;
            MWBase::Environment::get().getWorld()->launchProjectile(
                actor, ammoPtr, launchPos, orient, weaponPtr, speed, attackStrength, attackWindUp);

            inv.remove(ammoPtr, 1);
            mAmmunition.reset();
        }
    }

    void WeaponAnimation::configureControllers(float characterPitchRadians)
    {
        if (mPitchFactor == 0.f || characterPitchRadians == 0.f)
        {
            setSpineRotation(osg::Quat(), false);
            return;
        }

        float pitch = characterPitchRadians * mPitchFactor;
        setSpineRotation(osg::Quat(pitch / 2, osg::Vec3f(-1, 0, 0)), true);
    }
}
