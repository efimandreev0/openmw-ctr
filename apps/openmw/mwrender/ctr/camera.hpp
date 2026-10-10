#ifndef OPENMW_MWRENDER_CTR_CAMERA_H
#define OPENMW_MWRENDER_CTR_CAMERA_H

#include <optional>
#include <string>

#include <osg/Matrix>
#include <osg/Vec3>
#include <osg/Vec3d>
#include <osg/ref_ptr>

#include "../../mwworld/ptr.hpp"


namespace MWRender
{
    class NpcAnimation;

    class Camera
    {
    public:
        enum class Mode : int
        {
            Static = 0,
            FirstPerson = 1,
            ThirdPerson = 2,
            Vanity = 3,
            Preview = 4
        };

        Camera();
        ~Camera();

        void attachTo(const MWWorld::Ptr& ptr);
        MWWorld::Ptr getTrackingPtr() const { return mTrackingPtr; }

        void setFocalPointTransitionSpeed(float v) { mFocalPointTransitionSpeedCoef = v; }
        float getFocalPointTransitionSpeed() const { return mFocalPointTransitionSpeedCoef; }
        void setFocalPointTargetOffset(const osg::Vec2d& v);
        osg::Vec2d getFocalPointTargetOffset() const { return mFocalPointTargetOffset; }
        void instantTransition();
        void showCrosshair(bool v) { mShowCrosshair = v; }

        void updateCamera();

        void reset() { setMode(Mode::FirstPerson); }

        void rotateCameraToTrackingPtr();

        float getPitch() const { return mPitch; }
        float getYaw() const { return mYaw; }
        float getRoll() const { return mRoll; }

        void setPitch(float angle, bool force = false);
        void setYaw(float angle, bool force = false);
        void setRoll(float angle) { mRoll = angle; }

        float getExtraPitch() const { return mExtraPitch; }
        float getExtraYaw() const { return mExtraYaw; }
        float getExtraRoll() const { return mExtraRoll; }
        void setExtraPitch(float angle) { mExtraPitch = angle; }
        void setExtraYaw(float angle) { mExtraYaw = angle; }
        void setExtraRoll(float angle) { mExtraRoll = angle; }

        osg::Quat getOrient() const;

        void toggleViewMode(bool force = false);
        bool toggleVanityMode(bool enable);

        void applyDeferredPreviewRotationToPlayer(float dt);
        void disableDeferredPreviewRotation() { mDeferredRotationDisabled = true; }

        void setSneakOffset(float offset);

        void processViewChange();

        void update(float duration, bool paused = false);

        float getCameraDistance() const { return mCameraDistance; }
        void setPreferredCameraDistance(float v) { mPreferredCameraDistance = v; }

        void setAnimation(NpcAnimation* anim);

        osg::Vec3d getTrackedPosition() const { return mTrackedPosition; }
        const osg::Vec3d& getPosition() const { return mPosition; }
        void setStaticPosition(const osg::Vec3d& pos);

        bool isVanityOrPreviewModeEnabled() const { return mMode == Mode::Vanity || mMode == Mode::Preview; }
        Mode getMode() const { return mMode; }
        std::optional<Mode> getQueuedMode() const { return mQueuedMode; }
        void setMode(Mode mode, bool force = true);

        void allowCharacterDeferredRotation(bool v) { mDeferredRotationAllowed = v; }
        void calculateDeferredRotation();
        void setFirstPersonOffset(const osg::Vec3f& v) { mFirstPersonOffset = v; }
        osg::Vec3f getFirstPersonOffset() const { return mFirstPersonOffset; }

        int getCollisionType() const { return mCollisionType; }
        void setCollisionType(int collisionType) { mCollisionType = collisionType; }

        const osg::Matrixf& getViewMatrix() const { return mViewMatrix; }
        const osg::Matrixf& getProjectionMatrix() const { return mProjectionMatrix; }

    private:
        MWWorld::Ptr mTrackingPtr;
        std::string mTrackingBone;
        bool mTracking = false;
        osg::Vec3d mTrackedPosition;
        float mHeightScale;
        int mCollisionType;


        NpcAnimation* mAnimation;

        bool mFirstPersonView;

        Mode mMode;
        std::optional<Mode> mQueuedMode;
        bool mVanityAllowed;
        bool mDeferredRotationAllowed;

        bool mProcessViewChange;

        float mHeight;
        float mPitch, mYaw, mRoll;
        float mExtraPitch = 0, mExtraYaw = 0, mExtraRoll = 0;
        bool mLockPitch = false, mLockYaw = false;
        osg::Vec3d mPosition;
        osg::Matrixf mViewMatrix;
        osg::Matrixf mProjectionMatrix;

        float mCameraDistance, mPreferredCameraDistance;

        osg::Vec3f mFirstPersonOffset{ 0, 0, 0 };

        osg::Vec2d mFocalPointCurrentOffset;
        osg::Vec2d mFocalPointTargetOffset;
        float mFocalPointTransitionSpeedCoef;
        bool mSkipFocalPointTransition;

        float mPreviousTransitionInfluence;
        osg::Vec2d mFocalPointTransitionSpeed;
        osg::Vec2d mPreviousTransitionSpeed;
        osg::Vec2d mPreviousExtraOffset;

        bool mShowCrosshair;

        osg::Vec3d calculateTrackedPosition() const;
        osg::Vec3d calculateFirstPersonPosition(const osg::Vec3d& trackedPosition) const;
        osg::Vec3d getFocalPointOffset() const;
        void updateFocalPointOffset(float duration);
        void updatePosition();


        osg::Vec3f mDeferredRotation;
        bool mDeferredRotationDisabled;
    };
}

#endif
