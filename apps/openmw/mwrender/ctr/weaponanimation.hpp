#ifndef OPENMW_MWRENDER_CTR_WEAPONANIMATION_H
#define OPENMW_MWRENDER_CTR_WEAPONANIMATION_H

#include "../../mwworld/ptr.hpp"

#include "animation.hpp"

namespace MWRender
{
    class WeaponAnimationTime
    {
    private:
        Animation* mAnimation;
        std::string mWeaponGroup;
        float mStartTime;
        bool mRelativeTime;

    public:
        WeaponAnimationTime(Animation* animation)
            : mAnimation(animation)
            , mStartTime(0)
            , mRelativeTime(false)
        {
        }

        void setGroup(const std::string& group, bool relativeTime);
        void updateStartTime();
        float getValue() const;
    };

    class WeaponAnimation
    {
    public:
        WeaponAnimation();
        virtual ~WeaponAnimation();

        void attachArrow(const MWWorld::Ptr& actor);

        void detachArrow(MWWorld::Ptr actor);

        void releaseArrow(MWWorld::Ptr actor, float attackStrength, float attackWindUp);

        void configureControllers(float characterPitchRadians);

    protected:
        PartHolderPtr mAmmunition;

        float mPitchFactor;

        virtual PartHolderPtr attachAmmunition(VFS::Path::NormalizedView model) = 0;
        virtual bool getAmmunitionWorldPosition(osg::Vec3f& out) = 0;
        virtual bool getWeaponWorldPosition(osg::Vec3f& out) = 0;
        virtual void showWeapon(bool show) = 0;
        virtual void setSpineRotation(const osg::Quat& rotate, bool enabled) = 0;
    };
}

#endif
