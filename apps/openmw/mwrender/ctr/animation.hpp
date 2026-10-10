#ifndef OPENMW_MWRENDER_CTR_ANIMATION_H
#define OPENMW_MWRENDER_CTR_ANIMATION_H


#include "../animationpriority.hpp"
#include "../blendmask.hpp"
#include "../bonegroup.hpp"

#include "../../mwworld/movementdirection.hpp"
#include "../../mwworld/ptr.hpp"

#include <components/ctr/scene/keyframes.hpp>
#include <components/ctr/scene/model.hpp>
#include <components/ctr/scene/particles.hpp>
#include <components/ctr/scene/renderer.hpp>
#include <components/misc/strings/algorithm.hpp>
#include <components/sceneutil/textkeymap.hpp>
#include <components/vfs/pathutil.hpp>

#include <osg/BoundingSphere>
#include <osg/Matrix>
#include <osg/Matrixf>
#include <osg/Quat>
#include <osg/Referenced>
#include <osg/Vec3f>
#include <osg/Vec4f>

#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <unordered_set>
#include <vector>

namespace ESM
{
    struct Light;
}

namespace Resource
{
    class ResourceSystem;
}

namespace SceneUtil
{
    class PositionAttitudeTransform;
    struct LightCommon;
}

namespace MWRender
{
    class Animation;

    class EffectAnimationTime
    {
    public:
        void addTime(float duration) { mTime += duration; }
        void resetTime(float time) { mTime = time; }
        float getTime() const { return mTime; }

    private:
        float mTime = 0.f;
    };

    struct BoneNode;

    class PartHolder
    {
    public:
        PartHolder(Animation* owner, int partId, VFS::Path::NormalizedView model);
        ~PartHolder();

        PartHolder(const PartHolder&) = delete;
        PartHolder& operator=(const PartHolder&) = delete;

        int getPartId() const { return mPartId; }
        const VFS::Path::Normalized& getModel() const { return mModel; }

    private:
        Animation* mOwner;
        int mPartId;
        VFS::Path::Normalized mModel;
    };
    using PartHolderPtr = std::unique_ptr<PartHolder>;

    class Animation : public osg::Referenced
    {
    public:
        using BlendMask = MWRender::BlendMask;
        using BoneGroup = MWRender::BoneGroup;
        using AnimPriority = MWRender::AnimPriority;

        class TextKeyListener
        {
        public:
            virtual void handleTextKey(
                std::string_view groupname, SceneUtil::TextKeyMap::ConstIterator key, const SceneUtil::TextKeyMap& map)
                = 0;

            virtual ~TextKeyListener() = default;
        };

        void setTextKeyListener(TextKeyListener* listener);

        virtual bool updateCarriedLeftVisible(ESM::RefId weaptype) const { return false; }

    protected:
        struct AnimSource
        {
            CtrScene::KfAnimationPtr mKeyframes;
            std::vector<std::int16_t> mNodeTracks;

            const SceneUtil::TextKeyMap& getTextKeys() const { return mKeyframes->mTextKeys; }
        };

        struct AnimState
        {
            std::shared_ptr<AnimSource> mSource;
            float mStartTime = 0;
            float mLoopStartTime = 0;
            float mLoopStopTime = 0;
            float mStopTime = 0;

            std::shared_ptr<float> mTime = std::make_shared<float>(0.0f);
            float mSpeedMult = 1;

            bool mPlaying = false;
            bool mLoopingEnabled = true;
            uint32_t mLoopCount = 0;

            AnimPriority mPriority{ 0 };
            int mBlendMask = 0;
            bool mAutoDisable = true;

            std::string mGroupname;
            std::string mStartKey;
            std::string mStopKey;

            float getCompletion() const;
            float getTime() const { return *mTime; }
            void setTime(float time) { *mTime = time; }
            bool blendMaskContains(size_t blendMask) const { return (mBlendMask & (1 << blendMask)); }
            bool shouldLoop() const { return getTime() >= mLoopStopTime && mLoopingEnabled && mLoopCount > 0; }
        };

        typedef std::map<std::string, AnimState, std::less<>> AnimStateMap;
        AnimStateMap mStates;

        typedef std::vector<std::shared_ptr<AnimSource>> AnimSourceList;
        AnimSourceList mAnimSources;

        std::unordered_set<std::string_view> mSupportedAnimations;
        mutable std::vector<std::pair<std::string, MWWorld::MovementDirectionFlags>> mSupportedDirections;

        struct ActiveGroup
        {
            std::shared_ptr<AnimSource> mSource;
            std::shared_ptr<float> mTime;
        };
        ActiveGroup mActive[sNumBlendMasks];

        struct NodeState
        {
            osg::Quat mBindRotation;
            osg::Vec3f mBindTranslation;
            float mBindScale = 1.f;
            osg::Matrixf mWorld; ///< object space
            std::uint8_t mBlendMask = 0;
        };

        struct RotateController
        {
            int mNode = -1;
            osg::Quat mRotate;
            osg::Vec3f mOffset;
            bool mEnabled = false;
        };

        enum class PartKind : std::uint8_t
        {
            Rigid, ///< whole model attached to a bone
            Skinned, ///< filtered skinned meshes bound to the skeleton
            Effect, ///< magic effect, removed when its controllers finish (unless looping)
        };

        struct Part
        {
            CtrScene::ModelPtr mModel;
            PartKind mKind = PartKind::Rigid;
            int mAttachNode = -1; ///< skeleton node, -1 = object root
            osg::Matrixf mAttach; ///< extra transform below the attach node (bone offset, mirroring, rotation)
            std::vector<osg::Matrixf> mBindPose; ///< part node transforms relative to the part root
            std::vector<std::vector<std::int16_t>> mBoneNodes;
            std::vector<std::uint8_t> mMeshEnabled;
            bool mVisible = true;
            bool mFullAmbient = false;
            std::string mEffectId;
            std::string mBoneName;
            bool mLoop = false;
            bool mFinished = false;
            float mTime = 0.f;
            float mMaxTime = 0.f;
            mutable std::vector<CtrScene::ParticleInstance> mParticles;
            mutable float mParticleTime = -1.f;
        };

        std::vector<std::unique_ptr<Part>> mParts;

        CtrScene::ModelPtr mObjectModel;
        bool mDrawObjectMeshes = false;
        bool mHideTriBip = false;
        std::vector<NodeState> mNodes;
        std::vector<std::vector<std::int16_t>> mObjectBoneNodes;
        std::vector<std::uint8_t> mSwitchChild;
        mutable bool mPoseDirty = true;
        mutable std::vector<CtrScene::ParticleInstance> mParticles;
        mutable float mParticleTime = -1.f;

        int mAccumRoot = -1;
        osg::Vec3f mAccumulate;

        MWWorld::Ptr mPtr;
        SceneUtil::PositionAttitudeTransform* mInsert;
        Resource::ResourceSystem* mResourceSystem;

        TextKeyListener* mTextKeyListener;

        RotateController mHeadController;
        RotateController mSpineController;
        RotateController mRootController;
        std::vector<RotateController*> mControllerList;
        float mHeadYawRadians;
        float mHeadPitchRadians;
        float mUpperBodyYawRadians;
        float mLegsYawRadians;
        float mBodyPitchRadians;

        bool mHasMagicEffects;

        float mAlpha;
        float mActorFade;

        struct ExtraLight
        {
            CtrScene::PointLight mLight;
            int mPart = -1; ///< attached to this part's root, or to the object when -1
            float mBaseRadius = 0.f;
            bool mFlicker = false;
            bool mPulse = false;
            float mPhase = 0.f;
        };
        std::unique_ptr<ExtraLight> mExtraLight;
        float mLightEffect = 0.f;
        CtrScene::PointLight mGlowLight;

        mutable std::map<std::string, float, std::less<>> mAnimVelocities;

        bool mPlayScriptedOnly;
        int mActiveState = 2;

        int findNode(std::string_view name) const;

        /* Sets the appropriate animations on the bone groups based on priority by finding
         * the highest priority AnimationStates and linking the appropriate controllers stored
         * in the AnimationState to the corresponding nodes.
         */
        void resetActiveGroups();

        /* Updates the position of the accum root node for the given time, and
         * returns the wanted movement vector from the previous time. */
        void updatePosition(float oldtime, float newtime, osg::Vec3f& position);

        /* Resets the animation to the time of the specified start marker, without
         * moving anything, and set the end time to the specified stop marker. If
         * the marker is not found, or if the markers are the same, it returns
         * false.
         */
        bool reset(AnimState& state, const SceneUtil::TextKeyMap& keys, std::string_view groupname,
            std::string_view start, std::string_view stop, float startpoint, bool loopfallback);

        void handleTextKey(AnimState& state, std::string_view groupname, SceneUtil::TextKeyMap::ConstIterator key,
            const SceneUtil::TextKeyMap& map);

        /** Sets the root model of the object.
         * @param baseonly If true, the model is only used as the skeleton and its meshes are not drawn.
         */
        void setObjectRoot(const std::string& model, bool forceskeleton, bool baseonly, bool isCreature);

        /** Adds the keyframes of the model's .kf file as a new animation source. Later sources have priority. */
        void addAnimSource(std::string_view model, const std::string& baseModel);
        std::shared_ptr<AnimSource> addSingleAnimSource(VFS::Path::NormalizedView kfname, const std::string& baseModel);

        /** Adds an additional light, attached to a part (or the object when part is -1). */
        void addExtraLight(int part, const SceneUtil::LightCommon& light);

        void clearAnimSources();

        virtual void addControllers();

        int addPart(VFS::Path::NormalizedView model, std::string_view bonename, std::string_view filter,
            const osg::Quat* attitude = nullptr);
        void removePart(int id);
        int addPartAtPartNode(VFS::Path::NormalizedView model, int parentPart, std::string_view nodeName);
        bool getPartWorldPosition(int id, osg::Vec3f& out) const;
        Part* getPartData(int id) { return id >= 0 && id < static_cast<int>(mParts.size()) ? mParts[id].get() : nullptr; }
        bool hasNode(std::string_view name) const { return findNode(name) >= 0; }

        void animationEnded(AnimState& state) const;

        void updatePose() const;
        osg::Matrixf getObjectWorldMatrix() const;
        void computePartRootWorld(const Part& part, osg::Matrixf& out) const;
        void drawPart(CtrScene::Renderer& renderer, const Part& part, const osg::Matrixf& object,
            const CtrScene::DrawParams& params) const;

        friend class PartHolder;

    public:
        Animation(const MWWorld::Ptr& ptr, SceneUtil::PositionAttitudeTransform* parentNode,
            Resource::ResourceSystem* resourceSystem);

        virtual ~Animation();

        MWWorld::ConstPtr getPtr() const { return mPtr; }

        MWWorld::Ptr getPtr() { return mPtr; }

        const CtrScene::Model* getObjectModel() const { return mObjectModel.get(); }
        void logMeshes(const osg::Matrixf& view) const;

        void setActive(int active) { mActiveState = active; }

        void addEffect(std::string_view model, std::string_view effectId, bool loop = false,
            std::string_view bonename = {}, std::string_view texture = {}, bool useAmbientLight = true,
            bool autoTransform = true, const std::optional<osg::Matrix>& transform = std::nullopt);

        void removeEffect(std::string_view effectId);
        void removeEffects();
        std::vector<std::string_view> getLoopingEffects() const;

        void addSpellCastGlow(const osg::Vec4f& color, float glowDuration = 1.5) {}

        virtual void updatePtr(const MWWorld::Ptr& ptr);

        bool hasAnimation(std::string_view anim) const;

        bool isLoopingAnimation(std::string_view group) const;

        void setAccumulation(const osg::Vec3f& accum);

        void play(std::string_view groupname, const AnimPriority& priority, int blendMask, bool autodisable,
            float speedmult, std::string_view start, std::string_view stop, float startpoint, uint32_t loops,
            bool loopfallback = false);

        void adjustSpeedMult(const std::string& groupname, float speedmult);

        bool isPlaying(std::string_view groupname) const;

        bool upperBodyReady() const;

        bool getInfo(std::string_view groupname, float* complete = nullptr, float* speedmult = nullptr,
            uint32_t* loopcount = nullptr) const;

        std::string_view getActiveGroup(BoneGroup boneGroup) const;

        float getStartTime(const std::string& groupname) const;

        float getTextKeyTime(std::string_view textKey) const;

        void getTextKeyMaps(std::vector<const SceneUtil::TextKeyMap*>& out) const;

        float getCurrentTime(std::string_view groupname) const;

        void disable(std::string_view groupname);

        float getVelocity(std::string_view groupname) const;

        virtual osg::Vec3f runAnimation(float duration);

        void setLoopingEnabled(std::string_view groupname, bool enabled);

        void updateEffects();
        void advanceEffects(float duration);
        bool hasMagicEffects() const { return mHasMagicEffects; }

        const BoneNode* getNode(std::string_view name) const;

        bool getNodeWorldMatrix(std::string_view name, osg::Matrixf& out) const;
        bool getNodeObjectMatrix(std::string_view name, osg::Matrixf& out) const;
        bool getNodeObjectMatrixByRecordIndex(int recordIndex, osg::Matrixf& out) const;

        MWWorld::MovementDirectionFlags getSupportedMovementDirections(
            std::span<const std::string_view> prefixes) const;

        bool getPlayScriptedOnly() const { return mPlayScriptedOnly; }
        void setPlayScriptedOnly(bool playScriptedOnly) { mPlayScriptedOnly = playScriptedOnly; }

        virtual bool useShieldAnimations() const { return false; }
        virtual bool getWeaponsShown() const { return false; }
        virtual void showWeapons(bool showWeapon) {}
        virtual bool getCarriedLeftShown() const { return false; }
        virtual void showCarriedLeft(bool show) {}
        virtual void setWeaponGroup(const std::string& group, bool relativeDuration) {}
        virtual void setVampire(bool vampire) {}
        void setAlpha(float actorFade, float alpha);
        virtual void setPitchFactor(float factor) {}
        virtual void attachArrow() {}
        virtual void detachArrow() {}
        virtual void releaseArrow(float attackStrength, float attackWindUp) {}
        virtual void enableHeadAnimation(bool enable) {}
        virtual void setLightEffect(float effect);

        virtual void setHeadPitch(float pitchRadians);
        virtual void setHeadYaw(float yawRadians);
        virtual float getHeadPitch() const;
        virtual float getHeadYaw() const;

        virtual void setUpperBodyYawRadians(float v) { mUpperBodyYawRadians = v; }
        virtual void setLegsYawRadians(float v) { mLegsYawRadians = v; }
        virtual float getUpperBodyYawRadians() const { return mUpperBodyYawRadians; }
        virtual float getLegsYawRadians() const { return mLegsYawRadians; }
        virtual void setBodyPitchRadians(float v) { mBodyPitchRadians = v; }
        virtual float getBodyPitchRadians() const { return mBodyPitchRadians; }

        virtual void setAccurateAiming(bool enabled) {}
        virtual bool canBeHarvested() const { return false; }
        virtual void harvest(const MWWorld::Ptr& ptr) {}

        virtual void removeFromScene() {}


        virtual void render(CtrScene::Renderer& renderer, const CtrScene::DrawParams& params) const;

        osg::BoundingSpheref getWorldBound() const;

        virtual void collectLights(std::vector<const CtrScene::PointLight*>& out, float time);

        bool isRenderable() const;

    private:
        Animation(const Animation&);
        void operator=(Animation&);
    };

    class ObjectAnimation : public Animation
    {
    public:
        ObjectAnimation(const MWWorld::Ptr& ptr, const std::string& model, Resource::ResourceSystem* resourceSystem,
            bool animated, bool allowLight);

        bool canBeHarvested() const override;
        void harvest(const MWWorld::Ptr& ptr) override;
    };

    inline void clearAnimationModelCache() {}
}
#endif
