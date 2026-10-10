#include "creatureanimation.hpp"

#include <components/debug/debuglog.hpp>
#include <components/esm3/loadcrea.hpp>
#include <components/esm3/loadligh.hpp>
#include <components/esm3/loadweap.hpp>
#include <components/sceneutil/lightcommon.hpp>
#include <components/sceneutil/positionattitudetransform.hpp>
#include <components/settings/values.hpp>

#include "../../mwmechanics/weapontype.hpp"
#include "../../mwworld/class.hpp"

namespace MWRender
{
    CreatureAnimation::CreatureAnimation(
        const MWWorld::Ptr& ptr, const std::string& model, Resource::ResourceSystem* resourceSystem, bool animated)
        : ActorAnimation(ptr, ptr.getRefData().getBaseNode(), resourceSystem)
    {
        MWWorld::LiveCellRef<ESM::Creature>* ref = mPtr.get<ESM::Creature>();

        if (!model.empty())
        {
            setObjectRoot(model, false, false, true);

            if ((ref->mBase->mFlags & ESM::Creature::Bipedal))
                addAnimSource(Settings::models().mXbaseanim.get().value(), model);

            if (animated)
                addAnimSource(model, model);
        }
    }

    CreatureWeaponAnimation::CreatureWeaponAnimation(
        const MWWorld::Ptr& ptr, const std::string& model, Resource::ResourceSystem* resourceSystem, bool animated)
        : ActorAnimation(ptr, ptr.getRefData().getBaseNode(), resourceSystem)
        , mShowWeapons(false)
        , mShowCarriedLeft(false)
    {
        mWeaponAnimationTime = std::make_shared<WeaponAnimationTime>(this);

        MWWorld::LiveCellRef<ESM::Creature>* ref = mPtr.get<ESM::Creature>();

        if (!model.empty())
        {
            setObjectRoot(model, true, false, true);

            if ((ref->mBase->mFlags & ESM::Creature::Bipedal))
                addAnimSource(Settings::models().mXbaseanim.get().value(), model);

            if (animated)
                addAnimSource(model, model);

            mPtr.getClass().getInventoryStore(mPtr).setInvListener(this);

            updateParts();
        }
    }

    void CreatureWeaponAnimation::showWeapons(bool showWeapon)
    {
        if (showWeapon != mShowWeapons)
        {
            mShowWeapons = showWeapon;
            updateParts();
        }
    }

    void CreatureWeaponAnimation::showCarriedLeft(bool show)
    {
        if (show != mShowCarriedLeft)
        {
            mShowCarriedLeft = show;
            updateParts();
        }
    }

    void CreatureWeaponAnimation::updateParts()
    {
        mAmmunition.reset();
        mWeapon.reset();
        mShield.reset();

        if (mShowWeapons)
            updatePart(mWeapon, MWWorld::InventoryStore::Slot_CarriedRight);
        if (mShowCarriedLeft)
            updatePart(mShield, MWWorld::InventoryStore::Slot_CarriedLeft);
    }

    void CreatureWeaponAnimation::updatePart(PartHolderPtr& scene, int slot)
    {
        if (!mObjectModel)
            return;

        const MWWorld::InventoryStore& inv = mPtr.getClass().getInventoryStore(mPtr);
        MWWorld::ConstContainerStoreIterator it = inv.getSlot(slot);

        if (it == inv.end())
        {
            scene.reset();
            return;
        }
        MWWorld::ConstPtr item = *it;

        std::string_view bonename;
        VFS::Path::Normalized itemModel = item.getClass().getCorrectedModel(item);
        if (slot == MWWorld::InventoryStore::Slot_CarriedRight)
        {
            if (item.getType() == ESM::Weapon::sRecordId)
            {
                const ESM::RefId type = item.get<ESM::Weapon>()->mBase->mData.mType;
                bonename = MWMechanics::getWeaponType(type)->mAttachBone;
                if (bonename != "Weapon Bone" && !hasBone(bonename))
                    bonename = "Weapon Bone";
            }
            else
                bonename = "Weapon Bone";
        }
        else
        {
            bonename = "Shield Bone";
            if (item.getType() == ESM::Armor::sRecordId)
                itemModel = getShieldMesh(item, false);
        }

        try
        {
            scene = attach(itemModel, bonename, bonename, item.getType() == ESM::Light::sRecordId);

            if (slot == MWWorld::InventoryStore::Slot_CarriedRight && item.getType() == ESM::Weapon::sRecordId
                && item.get<ESM::Weapon>()->mBase->mData.mType == ESM::WeaponType::MarksmanCrossbow)
            {
                const ESM::WeaponType* weaponInfo = MWMechanics::getWeaponType(ESM::WeaponType::MarksmanCrossbow);
                MWWorld::ConstContainerStoreIterator ammo = inv.getSlot(MWWorld::InventoryStore::Slot_Ammunition);
                if (ammo != inv.end() && ammo->get<ESM::Weapon>()->mBase->mData.mType == weaponInfo->mAmmoType)
                    attachArrow();
                else
                    mAmmunition.reset();
            }
            else
                mAmmunition.reset();

            if (item.getType() == ESM::Light::sRecordId)
                addExtraLight(scene->getPartId(), SceneUtil::LightCommon(*item.get<ESM::Light>()->mBase));
        }
        catch (std::exception& e)
        {
            Log(Debug::Error) << "Can not add creature part: " << e.what();
        }
    }

    bool CreatureWeaponAnimation::isArrowAttached() const
    {
        return mAmmunition != nullptr;
    }

    void CreatureWeaponAnimation::detachArrow()
    {
        WeaponAnimation::detachArrow(mPtr);
    }

    void CreatureWeaponAnimation::attachArrow()
    {
        WeaponAnimation::attachArrow(mPtr);
    }

    void CreatureWeaponAnimation::releaseArrow(float attackStrength, float attackWindUp)
    {
        WeaponAnimation::releaseArrow(mPtr, attackStrength, attackWindUp);
    }

    PartHolderPtr CreatureWeaponAnimation::attachAmmunition(VFS::Path::NormalizedView model)
    {
        if (!mWeapon || !mPtr.getClass().hasInventoryStore(mPtr))
            return nullptr;

        const MWWorld::InventoryStore& inv = mPtr.getClass().getInventoryStore(mPtr);
        MWWorld::ConstContainerStoreIterator weapon = inv.getSlot(MWWorld::InventoryStore::Slot_CarriedRight);
        if (weapon == inv.end() || weapon->getType() != ESM::Weapon::sRecordId)
            return nullptr;

        const ESM::RefId type = weapon->get<ESM::Weapon>()->mBase->mData.mType;
        const ESM::RefId ammoType = MWMechanics::getWeaponType(type)->mAmmoType;
        if (ammoType.empty())
            return nullptr;

        const std::string& bone = MWMechanics::getWeaponType(ammoType)->mAttachBone;
        int id = hasBone(bone) ? addPart(model, bone, bone) : addPartAtPartNode(model, mWeapon->getPartId(), "ArrowBone");
        if (id < 0)
            return nullptr;
        return std::make_unique<PartHolder>(this, id, model);
    }

    bool CreatureWeaponAnimation::getAmmunitionWorldPosition(osg::Vec3f& out)
    {
        return mAmmunition && getPartWorldPosition(mAmmunition->getPartId(), out);
    }

    bool CreatureWeaponAnimation::getWeaponWorldPosition(osg::Vec3f& out)
    {
        return mWeapon && getPartWorldPosition(mWeapon->getPartId(), out);
    }

    void CreatureWeaponAnimation::setSpineRotation(const osg::Quat& rotate, bool enabled)
    {
        for (RotateController& controller : mWeaponSpine)
        {
            controller.mRotate = rotate;
            controller.mEnabled = enabled;
        }
    }

    void CreatureWeaponAnimation::addControllers()
    {
        Animation::addControllers();
        mWeaponSpine[0].mNode = findNode("bip01 spine1");
        mWeaponSpine[1].mNode = findNode("bip01 spine2");
        for (RotateController& controller : mWeaponSpine)
            if (controller.mNode >= 0)
                mControllerList.push_back(&controller);
    }

    osg::Vec3f CreatureWeaponAnimation::runAnimation(float duration)
    {
        osg::Vec3f ret = Animation::runAnimation(duration);
        WeaponAnimation::configureControllers(mPtr.getRefData().getPosition().rot[0] + getBodyPitchRadians());
        return ret;
    }
}
