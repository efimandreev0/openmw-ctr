#ifndef OPENMW_MWRENDER_CTR_NPCANIMATION_H
#define OPENMW_MWRENDER_CTR_NPCANIMATION_H

#include "actoranimation.hpp"
#include "animation.hpp"
#include "weaponanimation.hpp"

#include <components/esm3/loadarmo.hpp>
#include <components/vfs/pathutil.hpp>

#include "../../mwworld/inventorystore.hpp"

#include <array>

namespace ESM
{
    struct NPC;
    struct BodyPart;
}

namespace MWSound
{
    class Sound;
}

namespace MWRender
{
    class NpcAnimation : public ActorAnimation, public WeaponAnimation, public MWWorld::InventoryStoreListener
    {
    public:
        void equipmentChanged() override;

    public:
        typedef std::map<ESM::PartReferenceType, std::string> PartBoneMap;

        enum ViewMode
        {
            VM_Normal,
            VM_FirstPerson,
            VM_HeadOnly
        };

    private:
        static const PartBoneMap sPartList;

        PartHolderPtr mObjectParts[ESM::PRT_Count];
        std::array<MWSound::Sound*, ESM::PRT_Count> mSounds;

        const ESM::NPC* mNpc;
        VFS::Path::Normalized mHeadModel;
        VFS::Path::Normalized mHairModel;
        ViewMode mViewMode;
        bool mShowWeapons;
        bool mShowCarriedLeft;

        enum NpcType
        {
            Type_Normal,
            Type_Werewolf,
            Type_Vampire
        };
        NpcType mNpcType;

        int mPartslots[ESM::PRT_Count]; // Each part slot is taken by clothing, armor, or is empty
        int mPartPriorities[ESM::PRT_Count];

        osg::Vec3f mFirstPersonOffset;
        float mFirstPersonFieldOfView;

        std::shared_ptr<WeaponAnimationTime> mWeaponAnimationTime;

        bool mSoundsDisabled;

        bool mAccurateAiming;
        float mAimingFactor;

        RotateController mFirstPersonNeckController;
        RotateController mWeaponSpine[2];

        void updateNpcBase();

        NpcType getNpcType() const;

        PartHolderPtr insertBoundedPart(VFS::Path::NormalizedView model, std::string_view bonename,
            std::string_view bonefilter, bool isLight);

        void removeIndividualPart(ESM::PartReferenceType type);
        void reserveIndividualPart(ESM::PartReferenceType type, int group, int priority);

        bool addOrReplaceIndividualPart(ESM::PartReferenceType type, int group, int priority,
            VFS::Path::NormalizedView mesh, bool isLight = false);
        void removePartGroup(int group);
        void addPartGroup(int group, int priority, const std::vector<ESM::PartReference>& parts);

        static bool isFemalePart(const ESM::BodyPart* bodypart);
        static NpcType getNpcType(const MWWorld::Ptr& ptr);

    protected:
        void addControllers() override;
        bool isArrowAttached() const override;

        PartHolderPtr attachAmmunition(VFS::Path::NormalizedView model) override;
        bool getAmmunitionWorldPosition(osg::Vec3f& out) override;
        bool getWeaponWorldPosition(osg::Vec3f& out) override;
        void showWeapon(bool show) override { showWeapons(show); }
        void setSpineRotation(const osg::Quat& rotate, bool enabled) override;

    public:
        /**
         * @param disableSounds Don't play the sounds of carried items (preview dolls).
         */
        NpcAnimation(const MWWorld::Ptr& ptr, SceneUtil::PositionAttitudeTransform* parentNode,
            Resource::ResourceSystem* resourceSystem, bool disableSounds = false, ViewMode viewMode = VM_Normal,
            float firstPersonFieldOfView = 55.f);
        virtual ~NpcAnimation();

        void enableHeadAnimation(bool enable) override {}

        void setAccurateAiming(bool enabled) override;

        void setWeaponGroup(const std::string& group, bool relativeDuration) override;

        osg::Vec3f runAnimation(float timepassed) override;

        void setPitchFactor(float factor) override { mPitchFactor = factor; }

        bool getWeaponsShown() const override { return mShowWeapons; }
        void showWeapons(bool showWeapon) override;

        bool updateCarriedLeftVisible(ESM::RefId weaptype) const override;
        bool getCarriedLeftShown() const override { return mShowCarriedLeft; }
        void showCarriedLeft(bool show) override;

        void attachArrow() override;
        void detachArrow() override;
        void releaseArrow(float attackStrength, float attackWindUp) override;

        void setViewMode(ViewMode viewMode);
        ViewMode getViewMode() const { return mViewMode; }

        void updateParts();

        void rebuild();

        const PartHolder* getPart(ESM::PartReferenceType type) const { return mObjectParts[type].get(); }

        void setVampire(bool vampire) override;

        void setFirstPersonOffset(const osg::Vec3f& offset);

        void updatePtr(const MWWorld::Ptr& updated) override;

        float getFirstPersonFieldOfView() const { return mFirstPersonFieldOfView; }

        static const std::vector<const ESM::BodyPart*>& getBodyParts(
            const ESM::RefId& raceId, bool female, bool firstperson, bool werewolf);
    };
}

#endif
