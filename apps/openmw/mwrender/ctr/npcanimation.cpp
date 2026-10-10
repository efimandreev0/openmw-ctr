#include "npcanimation.hpp"

#include <components/debug/debuglog.hpp>
#include <components/esm3/loadbody.hpp>
#include <components/esm3/loadclot.hpp>
#include <components/esm3/loadligh.hpp>
#include <components/esm3/loadmgef.hpp>
#include <components/esm3/loadnpc.hpp>
#include <components/esm3/loadrace.hpp>
#include <components/esm3/loadweap.hpp>
#include <components/misc/resourcehelpers.hpp>
#include <components/misc/rng.hpp>
#include <components/resource/resourcesystem.hpp>
#include <components/sceneutil/lightcommon.hpp>
#include <components/sceneutil/positionattitudetransform.hpp>
#include <components/settings/values.hpp>
#include <components/vfs/manager.hpp>

#include "../../mwworld/class.hpp"
#include "../../mwworld/esmstore.hpp"
#include "../../mwworld/inventorystore.hpp"

#include "../../mwmechanics/actorutil.hpp"
#include "../../mwmechanics/npcstats.hpp"
#include "../../mwmechanics/weapontype.hpp"

#include "../../mwbase/environment.hpp"
#include "../../mwbase/mechanicsmanager.hpp"
#include "../../mwbase/soundmanager.hpp"
#include "../../mwbase/world.hpp"

#include "../actorutil.hpp"

namespace
{

    VFS::Path::Normalized getVampireHead(ESM::RefId race, bool female)
    {
        static std::map<std::pair<ESM::RefId, bool>, const ESM::BodyPart*> sVampireMapping;

        const std::pair<ESM::RefId, bool> key(race, female);
        const ESM::BodyPart* vampireHead = nullptr;

        if (const auto it = sVampireMapping.find(key); it == sVampireMapping.end())
        {
            const MWWorld::ESMStore& store = *MWBase::Environment::get().getESMStore();

            for (const ESM::BodyPart& bodyPart : store.get<ESM::BodyPart>())
            {
                if (!bodyPart.mData.mVampire)
                    continue;
                if (bodyPart.mData.mType != ESM::BodyPart::MT_Skin)
                    continue;
                if (bodyPart.mData.mPart != ESM::BodyPart::MP_Head)
                    continue;
                if (female != (bodyPart.mData.mFlags & ESM::BodyPart::BPF_Female))
                    continue;
                if (!(bodyPart.mRace == race))
                    continue;

                vampireHead = &bodyPart;
            }

            sVampireMapping.emplace_hint(it, key, vampireHead);
        }
        else
        {
            vampireHead = it->second;
        }

        if (vampireHead == nullptr)
            return {};

        return Misc::ResourceHelpers::correctMeshPath(vampireHead->mModel.getNormalized());
    }

}

namespace MWRender
{
    NpcAnimation::NpcType NpcAnimation::getNpcType() const
    {
        const MWWorld::Class& cls = mPtr.getClass();
        if (mNpcType == Type_Vampire && cls.getNpcStats(mPtr).isDead() && !cls.getNpcStats(mPtr).isWerewolf())
            return mNpcType;
        return getNpcType(mPtr);
    }

    NpcAnimation::NpcType NpcAnimation::getNpcType(const MWWorld::Ptr& ptr)
    {
        const MWWorld::Class& cls = ptr.getClass();
        NpcAnimation::NpcType curType = Type_Normal;
        if (cls.getCreatureStats(ptr).getMagicEffects().getOrDefault(ESM::MagicEffect::Vampirism).getMagnitude() > 0)
            curType = Type_Vampire;
        if (cls.getNpcStats(ptr).isWerewolf())
            curType = Type_Werewolf;

        return curType;
    }

    static const inline NpcAnimation::PartBoneMap createPartListMap()
    {
        return { { ESM::PRT_Head, "Head" },
            { ESM::PRT_Hair, "Head" }, // note it uses "Head" as attach bone, but "Hair" as filter
            { ESM::PRT_Neck, "Neck" }, { ESM::PRT_Cuirass, "Chest" }, { ESM::PRT_Groin, "Groin" },
            { ESM::PRT_Skirt, "Groin" }, { ESM::PRT_RHand, "Right Hand" }, { ESM::PRT_LHand, "Left Hand" },
            { ESM::PRT_RWrist, "Right Wrist" }, { ESM::PRT_LWrist, "Left Wrist" }, { ESM::PRT_Shield, "Shield Bone" },
            { ESM::PRT_RForearm, "Right Forearm" }, { ESM::PRT_LForearm, "Left Forearm" },
            { ESM::PRT_RUpperarm, "Right Upper Arm" }, { ESM::PRT_LUpperarm, "Left Upper Arm" },
            { ESM::PRT_RFoot, "Right Foot" }, { ESM::PRT_LFoot, "Left Foot" }, { ESM::PRT_RAnkle, "Right Ankle" },
            { ESM::PRT_LAnkle, "Left Ankle" }, { ESM::PRT_RKnee, "Right Knee" }, { ESM::PRT_LKnee, "Left Knee" },
            { ESM::PRT_RLeg, "Right Upper Leg" }, { ESM::PRT_LLeg, "Left Upper Leg" },
            { ESM::PRT_RPauldron, "Right Clavicle" }, { ESM::PRT_LPauldron, "Left Clavicle" },
            { ESM::PRT_Weapon, "Weapon Bone" }, // Fallback. The real node name depends on the current weapon type.
            { ESM::PRT_Tail, "Tail" } };
    }
    const NpcAnimation::PartBoneMap NpcAnimation::sPartList = createPartListMap();

    NpcAnimation::~NpcAnimation()
    {
        mAmmunition.reset();
        for (auto& part : mObjectParts)
            part.reset();
    }

    NpcAnimation::NpcAnimation(const MWWorld::Ptr& ptr, SceneUtil::PositionAttitudeTransform* parentNode,
        Resource::ResourceSystem* resourceSystem, bool disableSounds, ViewMode viewMode, float firstPersonFieldOfView)
        : ActorAnimation(ptr, parentNode, resourceSystem)
        , mViewMode(viewMode)
        , mShowWeapons(false)
        , mShowCarriedLeft(true)
        , mNpcType(getNpcType(ptr))
        , mFirstPersonFieldOfView(firstPersonFieldOfView)
        , mSoundsDisabled(disableSounds)
        , mAccurateAiming(false)
        , mAimingFactor(0.f)
    {
        mNpc = mPtr.get<ESM::NPC>()->mBase;

        mWeaponAnimationTime = std::make_shared<WeaponAnimationTime>(this);

        for (size_t i = 0; i < ESM::PRT_Count; i++)
        {
            mPartslots[i] = -1; // each slot is empty
            mPartPriorities[i] = 0;
        }

        std::fill(mSounds.begin(), mSounds.end(), nullptr);

        updateNpcBase();
    }

    void NpcAnimation::setViewMode(NpcAnimation::ViewMode viewMode)
    {
        assert(viewMode != VM_HeadOnly);
        if (mViewMode == viewMode)
            return;
        mViewMode = viewMode;
        MWBase::Environment::get().getWorld()->scaleObject(
            mPtr, mPtr.getCellRef().getScale(), true); // apply race height after view change

        mAmmunition.reset();
        rebuild();
    }

    void NpcAnimation::rebuild()
    {
        updateNpcBase();

        MWBase::Environment::get().getMechanicsManager()->forceStateUpdate(mPtr);
    }

    void NpcAnimation::updateNpcBase()
    {
        mAmmunition.reset();
        for (size_t i = 0; i < ESM::PRT_Count; i++)
            removeIndividualPart((ESM::PartReferenceType)i);
        clearAnimSources();

        const MWWorld::ESMStore& store = *MWBase::Environment::get().getESMStore();
        const ESM::Race* race = store.get<ESM::Race>().find(mNpc->mRace);
        NpcType curType = getNpcType();
        bool isWerewolf = (curType == Type_Werewolf);
        bool isVampire = (curType == Type_Vampire);
        bool isFemale = !mNpc->isMale();

        mHeadModel.clear();
        mHairModel.clear();

        const ESM::RefId headName = isWerewolf ? ESM::RefId::stringRefId("WerewolfHead") : mNpc->mHead;
        const ESM::RefId hairName = isWerewolf ? ESM::RefId::stringRefId("WerewolfHair") : mNpc->mHair;

        if (!headName.empty())
        {
            const ESM::BodyPart* bp = store.get<ESM::BodyPart>().search(headName);
            if (bp)
                mHeadModel = Misc::ResourceHelpers::correctMeshPath(bp->mModel.getNormalized());
            else
                Log(Debug::Warning) << "Warning: Failed to load body part '" << headName << "'";
        }

        if (!hairName.empty())
        {
            const ESM::BodyPart* bp = store.get<ESM::BodyPart>().search(hairName);
            if (bp)
                mHairModel = Misc::ResourceHelpers::correctMeshPath(bp->mModel.getNormalized());
            else
                Log(Debug::Warning) << "Warning: Failed to load body part '" << hairName << "'";
        }

        if (!isWerewolf && isVampire)
            if (VFS::Path::Normalized vampireHead = getVampireHead(mNpc->mRace, isFemale); !vampireHead.empty())
                mHeadModel = std::move(vampireHead);

        bool is1stPerson = mViewMode == VM_FirstPerson;
        bool isBeast = (race->mData.mFlags & ESM::Race::Beast) != 0;

        std::string_view base;
        if (!isWerewolf)
        {
            if (!is1stPerson)
                base = Settings::models().mXbaseanim.get().value();
            else
                base = Settings::models().mXbaseanim1st.get().value();
        }

        const std::string defaultSkeleton = Misc::ResourceHelpers::correctActorModelPath(
            VFS::Path::toNormalized(getActorSkeleton(is1stPerson, isFemale, isBeast, isWerewolf)),
            mResourceSystem->getVFS());

        std::string smodel = defaultSkeleton;
        bool isCustomModel = false;
        if (!is1stPerson && !isWerewolf && !mNpc->mModel.empty())
        {
            const VFS::Path::Normalized model = Misc::ResourceHelpers::correctMeshPath(mNpc->mModel.getNormalized());
            isCustomModel = !isDefaultActorSkeleton(model);
            smodel = Misc::ResourceHelpers::correctActorModelPath(model, mResourceSystem->getVFS());
        }

        setObjectRoot(smodel, true, true, false);

        updateParts();

        if (!base.empty())
            addAnimSource(base, smodel);

        if (defaultSkeleton != base)
            addAnimSource(defaultSkeleton, smodel);

        if (isCustomModel)
            addAnimSource(smodel, smodel);

        const bool customArgonianSwim = !is1stPerson && !isWerewolf && isBeast && mNpc->mRace.contains("argonian");
        if (customArgonianSwim)
            addAnimSource(Settings::models().mXargonianswimkna.get().value(), smodel);

        mWeaponAnimationTime->updateStartTime();
    }

    void NpcAnimation::updateParts()
    {
        if (!mObjectModel)
            return;

        NpcType curType = getNpcType();
        if (curType != mNpcType)
        {
            mNpcType = curType;
            rebuild();
            return;
        }

        static const struct
        {
            int mSlot;
            int mBasePriority;
        } slotlist[] = { // FIXME: Priority is based on the number of reserved slots. There should be a better way.
            { MWWorld::InventoryStore::Slot_Robe, 11 }, { MWWorld::InventoryStore::Slot_Skirt, 3 },
            { MWWorld::InventoryStore::Slot_Helmet, 0 }, { MWWorld::InventoryStore::Slot_Cuirass, 0 },
            { MWWorld::InventoryStore::Slot_Greaves, 0 }, { MWWorld::InventoryStore::Slot_LeftPauldron, 0 },
            { MWWorld::InventoryStore::Slot_RightPauldron, 0 }, { MWWorld::InventoryStore::Slot_Boots, 0 },
            { MWWorld::InventoryStore::Slot_LeftGauntlet, 0 }, { MWWorld::InventoryStore::Slot_RightGauntlet, 0 },
            { MWWorld::InventoryStore::Slot_Shirt, 0 }, { MWWorld::InventoryStore::Slot_Pants, 0 },
            { MWWorld::InventoryStore::Slot_CarriedLeft, 0 }, { MWWorld::InventoryStore::Slot_CarriedRight, 0 }
        };
        static const size_t slotlistsize = sizeof(slotlist) / sizeof(slotlist[0]);

        bool wasArrowAttached = isArrowAttached();
        mAmmunition.reset();

        const MWWorld::InventoryStore& inv = mPtr.getClass().getInventoryStore(mPtr);
        for (size_t i = 0; i < slotlistsize && mViewMode != VM_HeadOnly; i++)
        {
            MWWorld::ConstContainerStoreIterator store = inv.getSlot(slotlist[i].mSlot);

            removePartGroup(slotlist[i].mSlot);

            if (store == inv.end())
                continue;

            if (slotlist[i].mSlot == MWWorld::InventoryStore::Slot_Helmet)
                removeIndividualPart(ESM::PRT_Hair);

            int prio = 1;
            if (store->getType() == ESM::Clothing::sRecordId)
            {
                prio = ((slotlist[i].mBasePriority + 1) << 1) + 0;
                const ESM::Clothing* clothes = store->get<ESM::Clothing>()->mBase;
                addPartGroup(slotlist[i].mSlot, prio, clothes->mParts.mParts);
            }
            else if (store->getType() == ESM::Armor::sRecordId)
            {
                prio = ((slotlist[i].mBasePriority + 1) << 1) + 1;
                const ESM::Armor* armor = store->get<ESM::Armor>()->mBase;
                addPartGroup(slotlist[i].mSlot, prio, armor->mParts.mParts);
            }

            if (slotlist[i].mSlot == MWWorld::InventoryStore::Slot_Robe)
            {
                ESM::PartReferenceType parts[] = { ESM::PRT_Groin, ESM::PRT_Skirt, ESM::PRT_RLeg, ESM::PRT_LLeg,
                    ESM::PRT_RUpperarm, ESM::PRT_LUpperarm, ESM::PRT_RKnee, ESM::PRT_LKnee, ESM::PRT_RForearm,
                    ESM::PRT_LForearm, ESM::PRT_Cuirass };
                const size_t partsSize = sizeof(parts) / sizeof(parts[0]);
                for (size_t p = 0; p < partsSize; ++p)
                    reserveIndividualPart(parts[p], slotlist[i].mSlot, prio);
            }
            else if (slotlist[i].mSlot == MWWorld::InventoryStore::Slot_Skirt)
            {
                reserveIndividualPart(ESM::PRT_Groin, slotlist[i].mSlot, prio);
                reserveIndividualPart(ESM::PRT_RLeg, slotlist[i].mSlot, prio);
                reserveIndividualPart(ESM::PRT_LLeg, slotlist[i].mSlot, prio);
            }
        }

        if (mViewMode != VM_FirstPerson)
        {
            if (mPartPriorities[ESM::PRT_Head] < 1 && !mHeadModel.empty())
                addOrReplaceIndividualPart(ESM::PRT_Head, -1, 1, mHeadModel);
            if (mPartPriorities[ESM::PRT_Hair] < 1 && mPartPriorities[ESM::PRT_Head] <= 1 && !mHairModel.empty())
                addOrReplaceIndividualPart(ESM::PRT_Hair, -1, 1, mHairModel);
        }
        if (mViewMode == VM_HeadOnly)
            return;

        if (mPartPriorities[ESM::PRT_Shield] < 1)
        {
            MWWorld::ConstContainerStoreIterator store = inv.getSlot(MWWorld::InventoryStore::Slot_CarriedLeft);
            MWWorld::ConstPtr part;
            if (store != inv.end() && (part = *store).getType() == ESM::Light::sRecordId)
            {
                const ESM::Light* light = part.get<ESM::Light>()->mBase;
                addOrReplaceIndividualPart(ESM::PRT_Shield, MWWorld::InventoryStore::Slot_CarriedLeft, 1,
                    Misc::ResourceHelpers::correctMeshPath(light->mModel.getNormalized()), true);
                if (mObjectParts[ESM::PRT_Shield])
                    addExtraLight(mObjectParts[ESM::PRT_Shield]->getPartId(), SceneUtil::LightCommon(*light));
            }
        }

        showWeapons(mShowWeapons);
        showCarriedLeft(mShowCarriedLeft);

        bool isWerewolf = (getNpcType() == Type_Werewolf);
        ESM::RefId race = (isWerewolf ? ESM::RefId::stringRefId("werewolf") : mNpc->mRace);

        const std::vector<const ESM::BodyPart*>& parts
            = getBodyParts(race, !mNpc->isMale(), mViewMode == VM_FirstPerson, isWerewolf);
        for (int part = ESM::PRT_Neck; part < ESM::PRT_Count; ++part)
        {
            if (mPartPriorities[part] < 1)
            {
                if (const ESM::BodyPart* bodypart = parts[part])
                    addOrReplaceIndividualPart(static_cast<ESM::PartReferenceType>(part), -1, 1,
                        Misc::ResourceHelpers::correctMeshPath(bodypart->mModel.getNormalized()));
            }
        }

        if (wasArrowAttached)
            attachArrow();
    }

    PartHolderPtr NpcAnimation::insertBoundedPart(
        VFS::Path::NormalizedView model, std::string_view bonename, std::string_view bonefilter, bool isLight)
    {
        return attach(model, bonename, bonefilter, isLight);
    }

    osg::Vec3f NpcAnimation::runAnimation(float timepassed)
    {
        osg::Vec3f ret = Animation::runAnimation(timepassed);

        if (mFirstPersonNeckController.mNode >= 0)
        {
            if (mAccurateAiming)
                mAimingFactor = 1.f;
            else
                mAimingFactor = std::max(0.f, mAimingFactor - timepassed * 0.5f);

            float rotateFactor = 0.75f + 0.25f * mAimingFactor;

            mFirstPersonNeckController.mEnabled = true;
            mFirstPersonNeckController.mRotate
                = osg::Quat(mPtr.getRefData().getPosition().rot[0] * rotateFactor, osg::Vec3f(-1, 0, 0));
            mFirstPersonNeckController.mOffset = mFirstPersonOffset;
        }

        WeaponAnimation::configureControllers(mPtr.getRefData().getPosition().rot[0] + getBodyPitchRadians());

        return ret;
    }

    void NpcAnimation::removeIndividualPart(ESM::PartReferenceType type)
    {
        mPartPriorities[type] = 0;
        mPartslots[type] = -1;

        mObjectParts[type].reset();
        if (mSounds[type] != nullptr && !mSoundsDisabled)
        {
            MWBase::Environment::get().getSoundManager()->stopSound(mSounds[type]);
            mSounds[type] = nullptr;
        }
    }

    void NpcAnimation::reserveIndividualPart(ESM::PartReferenceType type, int group, int priority)
    {
        if (priority > mPartPriorities[type])
        {
            removeIndividualPart(type);
            mPartPriorities[type] = priority;
            mPartslots[type] = group;
        }
    }

    void NpcAnimation::removePartGroup(int group)
    {
        for (int i = 0; i < ESM::PRT_Count; i++)
        {
            if (mPartslots[i] == group)
                removeIndividualPart((ESM::PartReferenceType)i);
        }
    }

    bool NpcAnimation::isFemalePart(const ESM::BodyPart* bodypart)
    {
        return bodypart->mData.mFlags & ESM::BodyPart::BPF_Female;
    }

    bool NpcAnimation::addOrReplaceIndividualPart(
        ESM::PartReferenceType type, int group, int priority, VFS::Path::NormalizedView mesh, bool isLight)
    {
        if (priority <= mPartPriorities[type])
            return false;

        removeIndividualPart(type);
        mPartslots[type] = group;
        mPartPriorities[type] = priority;
        try
        {
            std::string_view bonename = sPartList.at(type);
            if (type == ESM::PRT_Weapon)
            {
                const MWWorld::InventoryStore& inv = mPtr.getClass().getInventoryStore(mPtr);
                MWWorld::ConstContainerStoreIterator weapon = inv.getSlot(MWWorld::InventoryStore::Slot_CarriedRight);
                if (weapon != inv.end() && weapon->getType() == ESM::Weapon::sRecordId)
                {
                    const ESM::RefId weaponTypeId = weapon->get<ESM::Weapon>()->mBase->mData.mType;
                    const std::string& weaponBonename = MWMechanics::getWeaponType(weaponTypeId)->mAttachBone;

                    if (weaponBonename != bonename && hasBone(weaponBonename))
                        bonename = weaponBonename;
                }
            }

            const std::string_view bonefilter = (type == ESM::PRT_Hair) ? std::string_view{ "hair" } : bonename;
            mObjectParts[type] = insertBoundedPart(mesh, bonename, bonefilter, isLight);
        }
        catch (std::exception& e)
        {
            Log(Debug::Error) << "Error adding NPC part: " << e.what();
            return false;
        }

        if (!mSoundsDisabled && group == MWWorld::InventoryStore::Slot_CarriedLeft)
        {
            const MWWorld::InventoryStore& inv = mPtr.getClass().getInventoryStore(mPtr);
            MWWorld::ConstContainerStoreIterator csi = inv.getSlot(group);
            if (csi != inv.end())
            {
                const auto soundId = csi->getClass().getSound(*csi);
                if (!soundId.empty())
                {
                    mSounds[type] = MWBase::Environment::get().getSoundManager()->playSound3D(
                        mPtr, soundId, 1.0f, 1.0f, MWSound::Type::Sfx, MWSound::PlayMode::Loop);
                }
            }
        }

        return true;
    }

    void NpcAnimation::addPartGroup(int group, int priority, const std::vector<ESM::PartReference>& parts)
    {
        const MWWorld::ESMStore& store = *MWBase::Environment::get().getESMStore();
        const MWWorld::Store<ESM::BodyPart>& partStore = store.get<ESM::BodyPart>();

        const char* ext = (mViewMode == VM_FirstPerson) ? ".1st" : "";
        for (const ESM::PartReference& part : parts)
        {
            const ESM::BodyPart* bodypart = nullptr;
            if (!mNpc->isMale() && !part.mFemale.empty())
            {
                bodypart = partStore.search(ESM::RefId::stringRefId(part.mFemale.getRefIdString() + ext));
                if (!bodypart && mViewMode == VM_FirstPerson)
                {
                    bodypart = partStore.search(part.mFemale);
                    if (bodypart
                        && !(bodypart->mData.mPart == ESM::BodyPart::MP_Hand
                            || bodypart->mData.mPart == ESM::BodyPart::MP_Wrist
                            || bodypart->mData.mPart == ESM::BodyPart::MP_Forearm
                            || bodypart->mData.mPart == ESM::BodyPart::MP_Upperarm))
                        bodypart = nullptr;
                }
                else if (!bodypart)
                    Log(Debug::Warning) << "Warning: Failed to find body part '" << part.mFemale << "'";
            }
            if (!bodypart && !part.mMale.empty())
            {
                bodypart = partStore.search(ESM::RefId::stringRefId(part.mMale.getRefIdString() + ext));
                if (!bodypart && mViewMode == VM_FirstPerson)
                {
                    bodypart = partStore.search(part.mMale);
                    if (bodypart
                        && !(bodypart->mData.mPart == ESM::BodyPart::MP_Hand
                            || bodypart->mData.mPart == ESM::BodyPart::MP_Wrist
                            || bodypart->mData.mPart == ESM::BodyPart::MP_Forearm
                            || bodypart->mData.mPart == ESM::BodyPart::MP_Upperarm))
                        bodypart = nullptr;
                }
                else if (!bodypart)
                    Log(Debug::Warning) << "Warning: Failed to find body part '" << part.mMale << "'";
            }

            if (bodypart)
                addOrReplaceIndividualPart(static_cast<ESM::PartReferenceType>(part.mPart), group, priority,
                    Misc::ResourceHelpers::correctMeshPath(bodypart->mModel.getNormalized()));
            else
                reserveIndividualPart((ESM::PartReferenceType)part.mPart, group, priority);
        }
    }

    void NpcAnimation::addControllers()
    {
        Animation::addControllers();

        mFirstPersonNeckController = RotateController();
        mWeaponSpine[0] = RotateController();
        mWeaponSpine[1] = RotateController();

        if (mViewMode == VM_FirstPerson)
        {
            if (mStates.size() > 0)
            {
                mFirstPersonNeckController.mNode = findNode("bip01 neck");
                if (mFirstPersonNeckController.mNode >= 0)
                    mControllerList.push_back(&mFirstPersonNeckController);
            }
        }
        else if (mViewMode == VM_Normal)
        {
            mWeaponSpine[0].mNode = findNode("bip01 spine1");
            mWeaponSpine[1].mNode = findNode("bip01 spine2");
            for (RotateController& controller : mWeaponSpine)
                if (controller.mNode >= 0)
                    mControllerList.push_back(&controller);
        }
    }

    void NpcAnimation::setSpineRotation(const osg::Quat& rotate, bool enabled)
    {
        for (RotateController& controller : mWeaponSpine)
        {
            controller.mRotate = rotate;
            controller.mEnabled = enabled;
        }
    }

    void NpcAnimation::showWeapons(bool showWeapon)
    {
        mShowWeapons = showWeapon;
        mAmmunition.reset();
        if (showWeapon)
        {
            const MWWorld::InventoryStore& inv = mPtr.getClass().getInventoryStore(mPtr);
            MWWorld::ConstContainerStoreIterator weapon = inv.getSlot(MWWorld::InventoryStore::Slot_CarriedRight);
            if (weapon != inv.end())
            {
                const VFS::Path::Normalized mesh = weapon->getClass().getCorrectedModel(*weapon);
                addOrReplaceIndividualPart(ESM::PRT_Weapon, MWWorld::InventoryStore::Slot_CarriedRight, 1, mesh);

                if (weapon->getType() == ESM::Weapon::sRecordId
                    && weapon->get<ESM::Weapon>()->mBase->mData.mType == ESM::WeaponType::MarksmanCrossbow)
                {
                    const ESM::RefId ammotype
                        = MWMechanics::getWeaponType(ESM::WeaponType::MarksmanCrossbow)->mAmmoType;
                    MWWorld::ConstContainerStoreIterator ammo = inv.getSlot(MWWorld::InventoryStore::Slot_Ammunition);
                    if (ammo != inv.end() && ammo->get<ESM::Weapon>()->mBase->mData.mType == ammotype)
                        attachArrow();
                }
            }
        }
        else
        {
            removeIndividualPart(ESM::PRT_Weapon);
            if (mPtr == MWMechanics::getPlayer())
                mPtr.getClass().getCreatureStats(mPtr).setAttackingOrSpell(false);
        }

        updateHolsteredWeapon(!mShowWeapons);
        updateQuiver();
    }

    bool NpcAnimation::updateCarriedLeftVisible(ESM::RefId weaptype) const
    {
        return !(MWMechanics::getWeaponType(weaptype)->mFlags & ESM::WeaponType::TwoHanded);
    }

    void NpcAnimation::showCarriedLeft(bool show)
    {
        mShowCarriedLeft = show;
        const MWWorld::InventoryStore& inv = mPtr.getClass().getInventoryStore(mPtr);
        MWWorld::ConstContainerStoreIterator iter = inv.getSlot(MWWorld::InventoryStore::Slot_CarriedLeft);
        if (show && iter != inv.end())
        {
            VFS::Path::Normalized mesh = iter->getClass().getCorrectedModel(*iter);
            if (iter->getType() == ESM::Armor::sRecordId)
            {
                mesh = getShieldMesh(*iter, !mNpc->isMale());
            }
            if (mesh.empty()
                || addOrReplaceIndividualPart(ESM::PRT_Shield, MWWorld::InventoryStore::Slot_CarriedLeft, 1, mesh,
                    iter->getType() == ESM::Light::sRecordId))
            {
                if (mesh.empty())
                    reserveIndividualPart(ESM::PRT_Shield, MWWorld::InventoryStore::Slot_CarriedLeft, 1);
                if (iter->getType() == ESM::Light::sRecordId && mObjectParts[ESM::PRT_Shield])
                    addExtraLight(mObjectParts[ESM::PRT_Shield]->getPartId(),
                        SceneUtil::LightCommon(*iter->get<ESM::Light>()->mBase));
            }
        }
        else
            removeIndividualPart(ESM::PRT_Shield);

        updateHolsteredShield(mShowCarriedLeft);
    }

    void NpcAnimation::attachArrow()
    {
        WeaponAnimation::attachArrow(mPtr);
        updateQuiver();
    }

    void NpcAnimation::detachArrow()
    {
        WeaponAnimation::detachArrow(mPtr);
        updateQuiver();
    }

    void NpcAnimation::releaseArrow(float attackStrength, float attackWindUp)
    {
        WeaponAnimation::releaseArrow(mPtr, attackStrength, attackWindUp);
        updateQuiver();
    }

    PartHolderPtr NpcAnimation::attachAmmunition(VFS::Path::NormalizedView model)
    {
        const PartHolder* const weaponPart = mObjectParts[ESM::PRT_Weapon].get();
        if (weaponPart == nullptr)
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
        const int id
            = hasBone(bone) ? addPart(model, bone, bone) : addPartAtPartNode(model, weaponPart->getPartId(), "ArrowBone");
        if (id < 0)
            return nullptr;
        return std::make_unique<PartHolder>(this, id, model);
    }

    bool NpcAnimation::getAmmunitionWorldPosition(osg::Vec3f& out)
    {
        return mAmmunition && getPartWorldPosition(mAmmunition->getPartId(), out);
    }

    bool NpcAnimation::getWeaponWorldPosition(osg::Vec3f& out)
    {
        const PartHolder* const part = mObjectParts[ESM::PRT_Weapon].get();
        return part != nullptr && getPartWorldPosition(part->getPartId(), out);
    }

    void NpcAnimation::setWeaponGroup(const std::string& group, bool relativeDuration)
    {
        mWeaponAnimationTime->setGroup(group, relativeDuration);
    }

    void NpcAnimation::equipmentChanged()
    {
        updateParts();
    }

    void NpcAnimation::setVampire(bool vampire)
    {
        if (mNpcType == Type_Werewolf) // we can't have werewolf vampires, can we
            return;
        if ((mNpcType == Type_Vampire) != vampire)
        {
            if (mPtr == MWMechanics::getPlayer())
                MWBase::Environment::get().getWorld()->reattachPlayerCamera();
            else
                rebuild();
        }
    }

    void NpcAnimation::setFirstPersonOffset(const osg::Vec3f& offset)
    {
        mFirstPersonOffset = offset;
    }

    void NpcAnimation::updatePtr(const MWWorld::Ptr& updated)
    {
        Animation::updatePtr(updated);
    }

    typedef std::map<std::pair<ESM::RefId, int>, std::vector<const ESM::BodyPart*>> RaceMapping;
    static RaceMapping sRaceMapping;

    const std::vector<const ESM::BodyPart*>& NpcAnimation::getBodyParts(
        const ESM::RefId& race, bool female, bool firstPerson, bool werewolf)
    {
        constexpr int flagFirstPerson = 1 << 1;
        constexpr int flagFemale = 1 << 0;

        int flags = (werewolf ? -1 : 0);
        if (female)
            flags |= flagFemale;
        if (firstPerson)
            flags |= flagFirstPerson;

        RaceMapping::iterator found = sRaceMapping.find(std::make_pair(race, flags));
        if (found != sRaceMapping.end())
            return found->second;
        else
        {
            std::vector<const ESM::BodyPart*>& parts = sRaceMapping[std::make_pair(race, flags)];

            typedef std::multimap<ESM::BodyPart::MeshPart, ESM::PartReferenceType> BodyPartMapType;
            static const BodyPartMapType sBodyPartMap = { { ESM::BodyPart::MP_Neck, ESM::PRT_Neck },
                { ESM::BodyPart::MP_Chest, ESM::PRT_Cuirass }, { ESM::BodyPart::MP_Groin, ESM::PRT_Groin },
                { ESM::BodyPart::MP_Hand, ESM::PRT_RHand }, { ESM::BodyPart::MP_Hand, ESM::PRT_LHand },
                { ESM::BodyPart::MP_Wrist, ESM::PRT_RWrist }, { ESM::BodyPart::MP_Wrist, ESM::PRT_LWrist },
                { ESM::BodyPart::MP_Forearm, ESM::PRT_RForearm }, { ESM::BodyPart::MP_Forearm, ESM::PRT_LForearm },
                { ESM::BodyPart::MP_Upperarm, ESM::PRT_RUpperarm }, { ESM::BodyPart::MP_Upperarm, ESM::PRT_LUpperarm },
                { ESM::BodyPart::MP_Foot, ESM::PRT_RFoot }, { ESM::BodyPart::MP_Foot, ESM::PRT_LFoot },
                { ESM::BodyPart::MP_Ankle, ESM::PRT_RAnkle }, { ESM::BodyPart::MP_Ankle, ESM::PRT_LAnkle },
                { ESM::BodyPart::MP_Knee, ESM::PRT_RKnee }, { ESM::BodyPart::MP_Knee, ESM::PRT_LKnee },
                { ESM::BodyPart::MP_Upperleg, ESM::PRT_RLeg }, { ESM::BodyPart::MP_Upperleg, ESM::PRT_LLeg },
                { ESM::BodyPart::MP_Tail, ESM::PRT_Tail } };

            parts.resize(ESM::PRT_Count, nullptr);

            if (werewolf)
                return parts;

            const MWWorld::ESMStore& store = *MWBase::Environment::get().getESMStore();

            for (const ESM::BodyPart& bodypart : store.get<ESM::BodyPart>())
            {
                if (bodypart.mData.mFlags & ESM::BodyPart::BPF_NotPlayable)
                    continue;
                if (bodypart.mData.mType != ESM::BodyPart::MT_Skin)
                    continue;

                if (!(bodypart.mRace == race))
                    continue;

                const bool partFirstPerson = ESM::isFirstPersonBodyPart(bodypart);

                bool isHand = bodypart.mData.mPart == ESM::BodyPart::MP_Hand
                    || bodypart.mData.mPart == ESM::BodyPart::MP_Wrist
                    || bodypart.mData.mPart == ESM::BodyPart::MP_Forearm
                    || bodypart.mData.mPart == ESM::BodyPart::MP_Upperarm;

                bool isSameGender = isFemalePart(&bodypart) == female;

                /* A fallback for the arms if 1st person is missing:
                 1. Try to use 3d person skin for same gender
                 2. Try to use 1st person skin for male, if female == true
                 3. Try to use 3d person skin for male, if female == true

                 A fallback in another cases: allow to use male bodyparts, if female == true
                */
                if (firstPerson && isHand && !partFirstPerson)
                {
                    BodyPartMapType::const_iterator bIt
                        = sBodyPartMap.lower_bound(BodyPartMapType::key_type(bodypart.mData.mPart));
                    while (bIt != sBodyPartMap.end() && bIt->first == bodypart.mData.mPart)
                    {
                        if (!parts[bIt->second] && isSameGender)
                            parts[bIt->second] = &bodypart;

                        else if (isSameGender && isFemalePart(parts[bIt->second]) != female)
                            parts[bIt->second] = &bodypart;

                        else if (!parts[bIt->second] && female)
                            parts[bIt->second] = &bodypart;

                        ++bIt;
                    }

                    continue;
                }

                if (partFirstPerson != firstPerson)
                    continue;

                if (female && !isFemalePart(&bodypart))
                {
                    BodyPartMapType::const_iterator bIt
                        = sBodyPartMap.lower_bound(BodyPartMapType::key_type(bodypart.mData.mPart));
                    while (bIt != sBodyPartMap.end() && bIt->first == bodypart.mData.mPart)
                    {
                        if (!parts[bIt->second])
                            parts[bIt->second] = &bodypart;

                        else if (isHand && !ESM::isFirstPersonBodyPart(*parts[bIt->second]) && partFirstPerson)
                            parts[bIt->second] = &bodypart;

                        ++bIt;
                    }

                    continue;
                }

                if (female != isFemalePart(&bodypart))
                    continue;

                BodyPartMapType::const_iterator bIt
                    = sBodyPartMap.lower_bound(BodyPartMapType::key_type(bodypart.mData.mPart));
                while (bIt != sBodyPartMap.end() && bIt->first == bodypart.mData.mPart)
                {
                    parts[bIt->second] = &bodypart;
                    ++bIt;
                }
            }
            return parts;
        }
    }

    void NpcAnimation::setAccurateAiming(bool enabled)
    {
        mAccurateAiming = enabled;
    }

    bool NpcAnimation::isArrowAttached() const
    {
        return mAmmunition != nullptr;
    }

}
