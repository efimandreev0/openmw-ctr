#include "animation.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>

#include <components/debug/debuglog.hpp>
#include <components/esm3/loadcont.hpp>
#include <components/esm3/loadcrea.hpp>
#include <components/esm3/loadligh.hpp>
#include <components/misc/constants.hpp>
#include <components/misc/resourcehelpers.hpp>
#include <components/sceneutil/lightcommon.hpp>
#include <components/sceneutil/positionattitudetransform.hpp>
#include <components/settings/values.hpp>
#include <components/vfs/manager.hpp>

#include <components/resource/resourcesystem.hpp>

#include "../../mwbase/environment.hpp"
#include "../../mwbase/luamanager.hpp"
#include "../../mwbase/world.hpp"
#include "../../mwworld/cellstore.hpp"
#include "../../mwworld/class.hpp"
#include "../../mwworld/containerstore.hpp"
#include "../../mwworld/esmstore.hpp"

#include "../../mwmechanics/character.hpp"
#include "../../mwmechanics/weapontype.hpp"

#include "context.hpp"

namespace MWRender
{
    namespace
    {
        bool equalsParts(std::string_view value, std::string_view s1, std::string_view s2, std::string_view s3 = {})
        {
            if (value.starts_with(s1))
            {
                value = value.substr(s1.size());
                if (value.starts_with(s2))
                    return value.substr(s2.size()) == s3;
            }
            return false;
        }

        float calcAnimVelocity(const SceneUtil::TextKeyMap& keys, const CtrScene::KfTrack& accumTrack,
            const osg::Vec3f& accum, std::string_view groupname)
        {
            float starttime = std::numeric_limits<float>::max();
            float stoptime = 0.0f;

            auto keyiter = keys.rbegin();
            while (keyiter != keys.rend())
            {
                if (equalsParts(keyiter->second, groupname, ": start")
                    || equalsParts(keyiter->second, groupname, ": loop start"))
                {
                    starttime = keyiter->first;
                    break;
                }
                ++keyiter;
            }
            keyiter = keys.rbegin();
            while (keyiter != keys.rend())
            {
                if (equalsParts(keyiter->second, groupname, ": stop"))
                    stoptime = keyiter->first;
                else if (equalsParts(keyiter->second, groupname, ": loop stop"))
                {
                    stoptime = keyiter->first;
                    break;
                }
                ++keyiter;
            }

            if (stoptime > starttime)
            {
                osg::Vec3f startpos = osg::componentMultiply(accumTrack.getTranslation(starttime), accum);
                osg::Vec3f endpos = osg::componentMultiply(accumTrack.getTranslation(stoptime), accum);
                return (startpos - endpos).length() / (stoptime - starttime);
            }

            return 0.0f;
        }

        std::uint8_t detectBlendMask(const CtrScene::Model& model, int node)
        {
            static const std::string_view sBlendMaskRoots[sNumBlendMasks] = {
                "", /* Lower body / character root */
                "Bip01 Spine1", /* Torso */
                "Bip01 L Clavicle", /* Left arm */
                "Bip01 R Clavicle", /* Right arm */
            };
            while (node >= 0)
            {
                const std::string& name = model.mNodes[node].mName;
                for (std::size_t i = 1; i < sNumBlendMasks; ++i)
                    if (name == sBlendMaskRoots[i])
                        return static_cast<std::uint8_t>(i);
                node = model.mNodes[node].mParent;
            }
            return 0;
        }

        void decompose(const osg::Matrixf& m, osg::Quat& rotation, osg::Vec3f& translation, float& scale)
        {
            translation = m.getTrans();
            scale = std::sqrt(m(0, 0) * m(0, 0) + m(0, 1) * m(0, 1) + m(0, 2) * m(0, 2));
            osg::Matrixf normalized = m;
            if (scale > 0.f && std::abs(scale - 1.f) > 1e-4f)
            {
                const float inv = 1.f / scale;
                for (int r = 0; r < 3; ++r)
                    for (int c = 0; c < 3; ++c)
                        normalized(r, c) *= inv;
            }
            rotation = normalized.getRotate();
        }

        osg::Matrixf compose(const osg::Quat& rotation, const osg::Vec3f& translation, float scale)
        {
            osg::Matrixf m;
            m.makeRotate(rotation);
            if (scale != 1.f)
                for (int r = 0; r < 3; ++r)
                    for (int c = 0; c < 3; ++c)
                        m(r, c) *= scale;
            m.setTrans(translation);
            return m;
        }

        bool filterMatches(std::string_view name, std::string_view filter)
        {
            if (Misc::StringUtils::ciStartsWith(name, filter))
                return true;
            constexpr std::string_view prefix = "tri ";
            if (Misc::StringUtils::ciStartsWith(name, prefix))
                return Misc::StringUtils::ciStartsWith(name.substr(prefix.size()), filter);
            return false;
        }

        osg::Vec3f toColor(const osg::Vec4f& c)
        {
            return osg::Vec3f(c.r(), c.g(), c.b());
        }
    }

    struct BoneNode
    {
    };


    PartHolder::PartHolder(Animation* owner, int partId, VFS::Path::NormalizedView model)
        : mOwner(owner)
        , mPartId(partId)
        , mModel(model)
    {
    }

    PartHolder::~PartHolder()
    {
        if (mOwner != nullptr)
            mOwner->removePart(mPartId);
    }


    Animation::Animation(const MWWorld::Ptr& ptr, SceneUtil::PositionAttitudeTransform* parentNode,
        Resource::ResourceSystem* resourceSystem)
        : mAccumulate(1.f, 1.f, 0.f)
        , mPtr(ptr)
        , mInsert(parentNode)
        , mResourceSystem(resourceSystem)
        , mTextKeyListener(nullptr)
        , mHeadYawRadians(0.f)
        , mHeadPitchRadians(0.f)
        , mUpperBodyYawRadians(0.f)
        , mLegsYawRadians(0.f)
        , mBodyPitchRadians(0.f)
        , mHasMagicEffects(false)
        , mAlpha(1.f)
        , mActorFade(1.f)
        , mPlayScriptedOnly(false)
    {
    }

    Animation::~Animation() = default;

    void Animation::updatePtr(const MWWorld::Ptr& ptr)
    {
        mPtr = ptr;
        mInsert = ptr.getRefData().getBaseNode();
    }

    void Animation::setAccumulation(const osg::Vec3f& accum)
    {
        mAccumulate = accum;
        mPoseDirty = true;
    }

    int Animation::findNode(std::string_view name) const
    {
        if (!mObjectModel)
            return -1;
        return mObjectModel->findNode(name);
    }

    const BoneNode* Animation::getNode(std::string_view name) const
    {
        static BoneNode sHandle;
        return findNode(name) >= 0 ? &sHandle : nullptr;
    }

    void Animation::addAnimSource(std::string_view model, const std::string& baseModel)
    {
        constexpr VFS::Path::ExtensionView kf("kf");
        constexpr VFS::Path::ExtensionView nif("nif");

        VFS::Path::Normalized kfname(model);
        if (kfname.extension() == nif)
            kfname.changeExtension(kf);

        addSingleAnimSource(kfname, baseModel);
    }

    std::shared_ptr<Animation::AnimSource> Animation::addSingleAnimSource(
        VFS::Path::NormalizedView kfname, const std::string& baseModel)
    {
        if (!mObjectModel || !mResourceSystem->getVFS()->exists(kfname))
            return nullptr;

        CtrScene::KfAnimationPtr keyframes = CtrContext::get()->getKeyframes().get(kfname);
        if (keyframes == nullptr || keyframes->mTextKeys.empty() || keyframes->mTracks.empty())
            return nullptr;

        auto animsrc = std::make_shared<AnimSource>();
        animsrc->mKeyframes = std::move(keyframes);
        animsrc->mNodeTracks.assign(mNodes.size(), -1);
        const auto& tracks = animsrc->mKeyframes->mTracks;
        for (std::size_t t = 0; t < tracks.size(); ++t)
        {
            const int node = findNode(tracks[t].mBone);
            if (node < 0)
            {
                Log(Debug::Verbose) << "addAnimSource: can't find bone '" << tracks[t].mBone << "' in " << baseModel
                                    << " (referenced by " << kfname << ")";
                continue;
            }
            animsrc->mNodeTracks[node] = static_cast<std::int16_t>(t);
        }

        mAnimSources.push_back(animsrc);

        mSupportedDirections.clear();
        for (const std::string& group : mAnimSources.back()->getTextKeys().getGroups())
            mSupportedAnimations.insert(group);

        if (mAccumRoot < 0)
        {
            for (std::string_view name : { std::string_view("bip01"), std::string_view("root bone") })
            {
                const int node = findNode(name);
                if (node >= 0 && animsrc->mNodeTracks[node] >= 0)
                {
                    mAccumRoot = node;
                    break;
                }
            }
        }

        return animsrc;
    }

    void Animation::clearAnimSources()
    {
        mStates.clear();
        for (ActiveGroup& group : mActive)
            group = ActiveGroup();
        mSupportedAnimations.clear();
        mSupportedDirections.clear();
        mAnimSources.clear();
        mAnimVelocities.clear();
        mPoseDirty = true;
    }

    bool Animation::hasAnimation(std::string_view anim) const
    {
        return mSupportedAnimations.find(anim) != mSupportedAnimations.end();
    }

    bool Animation::isLoopingAnimation(std::string_view group) const
    {
        static const std::unordered_set<std::string_view> loopingAnimations = { "walkforward", "walkback", "walkleft",
            "walkright", "swimwalkforward", "swimwalkback", "swimwalkleft", "swimwalkright", "runforward", "runback",
            "runleft", "runright", "swimrunforward", "swimrunback", "swimrunleft", "swimrunright", "sneakforward",
            "sneakback", "sneakleft", "sneakright", "turnleft", "turnright", "swimturnleft", "swimturnright",
            "spellturnleft", "spellturnright", "torch", "idle", "idle2", "idle3", "idle4", "idle5", "idle6", "idle7",
            "idle8", "idle9", "idlesneak", "idlestorm", "idleswim", "jump", "inventoryhandtohand",
            "inventoryweapononehand", "inventoryweapontwohand", "inventoryweapontwowide" };
        static const std::vector<std::string_view> shortGroups = MWMechanics::getAllWeaponTypeShortGroups();

        if (getTextKeyTime(std::string(group) + ": loop start") >= 0)
            return true;

        std::size_t suffixLength = 0;
        for (std::string_view suffix : shortGroups)
        {
            if (suffix.length() > suffixLength && group.ends_with(suffix))
                suffixLength = suffix.length();
        }
        group.remove_suffix(suffixLength);

        return loopingAnimations.count(group) > 0;
    }

    float Animation::getStartTime(const std::string& groupname) const
    {
        for (auto iter = mAnimSources.rbegin(); iter != mAnimSources.rend(); ++iter)
        {
            const SceneUtil::TextKeyMap& keys = (*iter)->getTextKeys();
            const auto found = keys.findGroupStart(groupname);
            if (found != keys.end())
                return found->first;
        }
        return -1.f;
    }

    float Animation::getTextKeyTime(std::string_view textKey) const
    {
        for (auto iter = mAnimSources.rbegin(); iter != mAnimSources.rend(); ++iter)
        {
            const SceneUtil::TextKeyMap& keys = (*iter)->getTextKeys();
            for (auto iterKey = keys.begin(); iterKey != keys.end(); ++iterKey)
            {
                if (iterKey->second.starts_with(textKey))
                    return iterKey->first;
            }
        }
        return -1.f;
    }

    void Animation::getTextKeyMaps(std::vector<const SceneUtil::TextKeyMap*>& out) const
    {
        for (const auto& source : mAnimSources)
            out.push_back(&source->getTextKeys());
    }

    void Animation::handleTextKey(AnimState& state, std::string_view groupname,
        SceneUtil::TextKeyMap::ConstIterator key, const SceneUtil::TextKeyMap& map)
    {
        std::string_view evt = key->second;

        if (evt.starts_with(groupname) && evt.substr(groupname.size()).starts_with(": "))
        {
            size_t off = groupname.size() + 2;
            if (evt.substr(off) == "loop start")
                state.mLoopStartTime = key->first;
            else if (evt.substr(off) == "loop stop")
                state.mLoopStopTime = key->first;
        }

        try
        {
            if (mTextKeyListener != nullptr)
                mTextKeyListener->handleTextKey(groupname, key, map);
        }
        catch (std::exception& e)
        {
            Log(Debug::Error) << "Error handling text key " << evt << ": " << e.what();
        }
    }

    void Animation::play(std::string_view groupname, const AnimPriority& priority, int blendMask, bool autodisable,
        float speedmult, std::string_view start, std::string_view stop, float startpoint, uint32_t loops,
        bool loopfallback)
    {
        if (!mObjectModel || mAnimSources.empty())
            return;

        if (groupname.empty())
        {
            resetActiveGroups();
            return;
        }

        AnimStateMap::iterator foundstateiter = mStates.find(groupname);
        if (foundstateiter != mStates.end())
            foundstateiter->second.mPriority = priority;

        AnimStateMap::iterator stateiter = mStates.begin();
        while (stateiter != mStates.end())
        {
            if (stateiter->second.mPriority == priority && stateiter->first != groupname)
            {
                animationEnded(stateiter->second);
                stateiter = mStates.erase(stateiter);
            }
            else
                ++stateiter;
        }

        if (foundstateiter != mStates.end())
        {
            resetActiveGroups();
            return;
        }

        /* Look in reverse; last-inserted source has priority. */
        AnimState state;
        for (auto iter = mAnimSources.rbegin(); iter != mAnimSources.rend(); ++iter)
        {
            const SceneUtil::TextKeyMap& textkeys = (*iter)->getTextKeys();
            if (reset(state, textkeys, groupname, start, stop, startpoint, loopfallback))
            {
                state.mSource = *iter;
                state.mSpeedMult = speedmult;
                state.mLoopCount = loops;
                state.mPlaying = (state.getTime() < state.mStopTime);
                state.mPriority = priority;
                state.mBlendMask = blendMask;
                state.mAutoDisable = autodisable;
                state.mGroupname = groupname;
                state.mStartKey = start;
                state.mStopKey = stop;
                mStates[std::string{ groupname }] = state;

                if (state.mPlaying)
                {
                    auto textkey = textkeys.lowerBound(state.getTime());
                    while (textkey != textkeys.end() && textkey->first <= state.getTime())
                    {
                        handleTextKey(state, groupname, textkey, textkeys);
                        ++textkey;
                    }
                }

                if (state.getTime() >= state.mLoopStopTime && state.mLoopCount > 0)
                {
                    state.mLoopCount--;
                    state.setTime(state.mLoopStartTime);
                    state.mPlaying = true;
                    if (state.getTime() >= state.mLoopStopTime)
                        break;

                    auto textkey = textkeys.lowerBound(state.getTime());
                    while (textkey != textkeys.end() && textkey->first <= state.getTime())
                    {
                        handleTextKey(state, groupname, textkey, textkeys);
                        ++textkey;
                    }
                }

                break;
            }
        }

        resetActiveGroups();
    }

    bool Animation::reset(AnimState& state, const SceneUtil::TextKeyMap& keys, std::string_view groupname,
        std::string_view start, std::string_view stop, float startpoint, bool loopfallback)
    {
        auto groupend = keys.rbegin();
        for (; groupend != keys.rend(); ++groupend)
        {
            if (groupend->second.starts_with(groupname) && groupend->second.compare(groupname.size(), 2, ": ") == 0)
                break;
        }

        auto startkey = groupend;
        while (startkey != keys.rend() && !equalsParts(startkey->second, groupname, ": ", start))
            ++startkey;
        if (startkey == keys.rend() && start == "loop start")
        {
            startkey = groupend;
            while (startkey != keys.rend() && !equalsParts(startkey->second, groupname, ": start"))
                ++startkey;
        }
        if (startkey == keys.rend())
            return false;

        auto stopkey = groupend;
        std::size_t checkLength = groupname.size() + 2 + stop.size();
        while (stopkey != keys.rend()
            && !equalsParts(std::string_view{ stopkey->second }.substr(0, checkLength), groupname, ": ", stop))
            ++stopkey;
        if (stopkey == keys.rend())
            return false;

        if (startkey->first > stopkey->first)
            return false;

        state.mStartTime = startkey->first;
        if (loopfallback)
        {
            state.mLoopStartTime = startkey->first;
            state.mLoopStopTime = stopkey->first;
        }
        else
        {
            state.mLoopStartTime = startkey->first;
            state.mLoopStopTime = std::numeric_limits<float>::max();
        }
        state.mStopTime = stopkey->first;

        state.setTime(state.mStartTime + ((state.mStopTime - state.mStartTime) * startpoint));

        auto key = groupend;
        for (; key != startkey && key != keys.rend(); ++key)
        {
            if (key->first > state.getTime())
                continue;

            if (equalsParts(key->second, groupname, ": loop start"))
                state.mLoopStartTime = key->first;
            else if (equalsParts(key->second, groupname, ": loop stop"))
                state.mLoopStopTime = key->first;
        }

        return true;
    }

    void Animation::setTextKeyListener(TextKeyListener* listener)
    {
        mTextKeyListener = listener;
    }

    void Animation::resetActiveGroups()
    {
        for (size_t blendMask = 0; blendMask < sNumBlendMasks; blendMask++)
        {
            AnimStateMap::const_iterator active = mStates.end();
            for (AnimStateMap::const_iterator state = mStates.begin(); state != mStates.end(); ++state)
            {
                if (!state->second.blendMaskContains(blendMask))
                    continue;
                if (active == mStates.end()
                    || active->second.mPriority[(BoneGroup)blendMask] < state->second.mPriority[(BoneGroup)blendMask])
                    active = state;
            }

            if (active == mStates.end())
                mActive[blendMask] = ActiveGroup();
            else
                mActive[blendMask] = ActiveGroup{ active->second.mSource, active->second.mTime };
        }
        mPoseDirty = true;
        addControllers();
    }

    void Animation::adjustSpeedMult(const std::string& groupname, float speedmult)
    {
        AnimStateMap::iterator state(mStates.find(groupname));
        if (state != mStates.end())
            state->second.mSpeedMult = speedmult;
    }

    bool Animation::isPlaying(std::string_view groupname) const
    {
        AnimStateMap::const_iterator state(mStates.find(groupname));
        if (state != mStates.end())
            return state->second.mPlaying;
        return false;
    }

    bool Animation::getInfo(std::string_view groupname, float* complete, float* speedmult, uint32_t* loopcount) const
    {
        AnimStateMap::const_iterator iter = mStates.find(groupname);
        if (iter == mStates.end())
        {
            if (complete)
                *complete = 0.0f;
            if (speedmult)
                *speedmult = 0.0f;
            if (loopcount)
                *loopcount = 0;
            return false;
        }

        if (complete)
            *complete = iter->second.getCompletion();
        if (speedmult)
            *speedmult = iter->second.mSpeedMult;
        if (loopcount)
            *loopcount = iter->second.mLoopCount;
        return true;
    }

    std::string_view Animation::getActiveGroup(BoneGroup boneGroup) const
    {
        if (const auto& timePtr = mActive[boneGroup].mTime)
            for (auto& state : mStates)
                if (state.second.mTime == timePtr)
                    return state.first;
        return "";
    }

    float Animation::getCurrentTime(std::string_view groupname) const
    {
        AnimStateMap::const_iterator iter = mStates.find(groupname);
        if (iter == mStates.end())
            return -1.f;
        return iter->second.getTime();
    }

    void Animation::disable(std::string_view groupname)
    {
        AnimStateMap::iterator iter = mStates.find(groupname);
        if (iter != mStates.end())
        {
            animationEnded(iter->second);
            mStates.erase(iter);
        }
        resetActiveGroups();
    }

    float Animation::getVelocity(std::string_view groupname) const
    {
        if (mAccumRoot < 0)
            return 0.0f;

        auto found = mAnimVelocities.find(groupname);
        if (found != mAnimVelocities.end())
            return found->second;

        auto animsrc = mAnimSources.rbegin();
        for (; animsrc != mAnimSources.rend(); ++animsrc)
        {
            const SceneUtil::TextKeyMap& keys = (*animsrc)->getTextKeys();
            if (keys.hasGroupStart(groupname))
                break;
        }
        if (animsrc == mAnimSources.rend())
            return 0.0f;

        auto velocityOf = [&](const AnimSource& source) {
            const int track = source.mNodeTracks[mAccumRoot];
            if (track < 0)
                return 0.f;
            return calcAnimVelocity(
                source.getTextKeys(), source.mKeyframes->mTracks[track], mAccumulate, groupname);
        };

        float velocity = velocityOf(**animsrc);

        if (!(velocity > 1.0f))
        {
            auto animiter = mAnimSources.rbegin();
            while (*animiter != *animsrc)
                ++animiter;

            while (!(velocity > 1.0f) && ++animiter != mAnimSources.rend())
                velocity = velocityOf(**animiter);
        }

        mAnimVelocities.emplace(groupname, velocity);
        return velocity;
    }

    void Animation::updatePosition(float oldtime, float newtime, osg::Vec3f& position)
    {
        const ActiveGroup& group = mActive[0];
        const CtrScene::KfTrack& track = group.mSource->mKeyframes->mTracks[group.mSource->mNodeTracks[mAccumRoot]];
        osg::Vec3f off = osg::componentMultiply(track.getTranslation(newtime), mAccumulate);
        position += off - osg::componentMultiply(track.getTranslation(oldtime), mAccumulate);
    }

    osg::Vec3f Animation::runAnimation(float duration)
    {
        const bool hasAccum = mAccumRoot >= 0 && mActive[0].mSource
            && mActive[0].mSource->mNodeTracks[mAccumRoot] >= 0;

        osg::Vec3f movement(0.f, 0.f, 0.f);
        AnimStateMap::iterator stateiter = mStates.begin();
        while (stateiter != mStates.end())
        {
            AnimState& state = stateiter->second;
            if (mPlayScriptedOnly && !state.mPriority.contains(MWMechanics::Priority_Scripted))
            {
                ++stateiter;
                continue;
            }

            const SceneUtil::TextKeyMap& textkeys = state.mSource->getTextKeys();
            auto textkey = textkeys.upperBound(state.getTime());

            float timepassed = duration * state.mSpeedMult;
            while (state.mPlaying)
            {
                if (!state.shouldLoop())
                {
                    float targetTime = state.getTime() + timepassed;
                    if (textkey == textkeys.end() || textkey->first > targetTime)
                    {
                        if (hasAccum && state.mTime == mActive[0].mTime)
                            updatePosition(state.getTime(), targetTime, movement);
                        state.setTime(std::min(targetTime, state.mStopTime));
                    }
                    else
                    {
                        if (hasAccum && state.mTime == mActive[0].mTime)
                            updatePosition(state.getTime(), textkey->first, movement);
                        state.setTime(textkey->first);
                    }

                    state.mPlaying = (state.getTime() < state.mStopTime);
                    timepassed = targetTime - state.getTime();

                    while (textkey != textkeys.end() && textkey->first <= state.getTime())
                    {
                        handleTextKey(state, stateiter->first, textkey, textkeys);
                        ++textkey;
                    }
                }
                if (state.shouldLoop())
                {
                    state.mLoopCount--;
                    state.setTime(state.mLoopStartTime);
                    state.mPlaying = true;

                    textkey = textkeys.lowerBound(state.getTime());
                    while (textkey != textkeys.end() && textkey->first <= state.getTime())
                    {
                        handleTextKey(state, stateiter->first, textkey, textkeys);
                        ++textkey;
                    }

                    if (state.getTime() >= state.mLoopStopTime)
                        break;
                }

                if (timepassed <= 0.0f)
                    break;
            }

            if (!state.mPlaying && state.mAutoDisable)
            {
                animationEnded(stateiter->second);
                stateiter = mStates.erase(stateiter);

                resetActiveGroups();
            }
            else
                ++stateiter;
        }

        updateEffects();

        const float epsilon = 0.001f;
        float yawOffset = 0;
        if (mRootController.mNode >= 0)
        {
            bool enable = std::abs(mLegsYawRadians) > epsilon || std::abs(mBodyPitchRadians) > epsilon;
            mRootController.mEnabled = enable;
            if (enable)
            {
                osg::Quat legYaw = osg::Quat(mLegsYawRadians, osg::Vec3f(0, 0, 1));
                mRootController.mRotate = legYaw * osg::Quat(mBodyPitchRadians, osg::Vec3f(1, 0, 0));
                yawOffset = mLegsYawRadians;
                movement = legYaw * movement;
            }
        }
        if (mSpineController.mNode >= 0)
        {
            float yaw = mUpperBodyYawRadians - yawOffset;
            bool enable = std::abs(yaw) > epsilon;
            mSpineController.mEnabled = enable;
            if (enable)
            {
                mSpineController.mRotate = osg::Quat(yaw, osg::Vec3f(0, 0, 1));
                yawOffset = mUpperBodyYawRadians;
            }
        }
        if (mHeadController.mNode >= 0)
        {
            float yaw = mHeadYawRadians - yawOffset;
            bool enable = (std::abs(mHeadPitchRadians) > epsilon || std::abs(yaw) > epsilon);
            mHeadController.mEnabled = enable;
            if (enable)
                mHeadController.mRotate
                    = osg::Quat(mHeadPitchRadians, osg::Vec3f(1, 0, 0)) * osg::Quat(yaw, osg::Vec3f(0, 0, 1));
        }

        mPoseDirty = true;
        return movement;
    }

    void Animation::setLoopingEnabled(std::string_view groupname, bool enabled)
    {
        AnimStateMap::iterator state(mStates.find(groupname));
        if (state != mStates.end())
            state->second.mLoopingEnabled = enabled;
    }

    void Animation::setObjectRoot(const std::string& model, bool /*forceskeleton*/, bool baseonly, bool isCreature)
    {
        mParts.clear();
        mExtraLight.reset();
        mNodes.clear();
        mObjectBoneNodes.clear();
        mSwitchChild.clear();
        mAccumRoot = -1;
        mObjectModel = nullptr;
        mDrawObjectMeshes = false;
        mHideTriBip = isCreature;
        mHeadController = RotateController();
        mSpineController = RotateController();
        mRootController = RotateController();
        mControllerList.clear();
        mPoseDirty = true;

        if (model.empty())
            return;

        mObjectModel = CtrContext::get()->getModels().get(VFS::Path::toNormalized(model));
        mDrawObjectMeshes = !baseonly;

        const CtrScene::Model& m = *mObjectModel;
        mParticles.clear();
        mParticleTime = -1.f;
        for (const CtrScene::ParticleSystem& system : m.mParticleSystems)
            mParticles.emplace_back(system);
        mNodes.resize(m.mNodes.size());
        for (std::size_t i = 0; i < m.mNodes.size(); ++i)
        {
            NodeState& node = mNodes[i];
            decompose(m.mNodes[i].mLocal, node.mBindRotation, node.mBindTranslation, node.mBindScale);
            node.mBlendMask = detectBlendMask(m, static_cast<int>(i));
        }

        mObjectBoneNodes.resize(m.mMeshes.size());
        for (std::size_t i = 0; i < m.mMeshes.size(); ++i)
            for (const std::string& bone : m.mMeshes[i].mBoneNames)
                mObjectBoneNodes[i].push_back(static_cast<std::int16_t>(m.findNode(bone)));

        mSwitchChild.resize(m.mSwitches.size());
        for (std::size_t i = 0; i < m.mSwitches.size(); ++i)
            mSwitchChild[i] = m.mSwitches[i].mInitialChild;
    }

    void Animation::updatePose() const
    {
        if (!mPoseDirty)
            return;
        mPoseDirty = false;
        auto& nodes = const_cast<std::vector<NodeState>&>(mNodes);
        if (!mObjectModel)
            return;
        const CtrScene::Model& model = *mObjectModel;
        const osg::Vec3f resetAxes(mAccumulate.x() != 0.f ? 0.f : 1.f, mAccumulate.y() != 0.f ? 0.f : 1.f,
            mAccumulate.z() != 0.f ? 0.f : 1.f);

        for (std::size_t i = 0; i < nodes.size(); ++i)
        {
            NodeState& node = nodes[i];
            osg::Quat rotation = node.mBindRotation;
            osg::Vec3f translation = node.mBindTranslation;
            float scale = node.mBindScale;
            bool animated = false;

            const ActiveGroup& group = mActive[node.mBlendMask];
            if (group.mSource && group.mTime)
            {
                const int track = group.mSource->mNodeTracks[i];
                if (track >= 0)
                {
                    const CtrScene::KfTrack& kf = group.mSource->mKeyframes->mTracks[track];
                    const float time = *group.mTime;
                    if (kf.hasRotation())
                        rotation = kf.getRotation(time);
                    if (!kf.mTranslations.empty())
                        translation = kf.getTranslation(time);
                    if (!kf.mScales.empty())
                        scale = kf.getScale(time);
                    animated = true;
                    if (static_cast<int>(i) == mAccumRoot && node.mBlendMask == 0)
                        translation = osg::componentMultiply(resetAxes, translation);
                }
            }

            const int parent = model.mNodes[i].mParent;
            osg::Matrixf local = compose(rotation, translation, scale);

            if (animated)
            {
                for (const RotateController* controller : mControllerList)
                {
                    if (controller->mNode != static_cast<int>(i) || !controller->mEnabled)
                        continue;
                    const osg::Matrixf world = parent >= 0 ? local * nodes[parent].mWorld : local;
                    const osg::Quat worldOrient = world.getRotate();
                    const osg::Quat worldOrientInverse = worldOrient.inverse();
                    rotation = worldOrient * controller->mRotate * worldOrientInverse * rotation;
                    translation += worldOrientInverse * controller->mOffset;
                    local = compose(rotation, translation, scale);
                }
            }

            node.mWorld = parent >= 0 ? local * nodes[parent].mWorld : local;
        }
    }

    osg::Matrixf Animation::getObjectWorldMatrix() const
    {
        osg::Matrixf m;
        if (mInsert == nullptr)
            return m;
        const osg::Vec3f& scale = mInsert->getScale();
        m.makeScale(scale);
        m.postMultRotate(mInsert->getAttitude());
        m.postMultTranslate(mInsert->getPosition());
        return m;
    }

    bool Animation::getNodeObjectMatrix(std::string_view name, osg::Matrixf& out) const
    {
        const int node = findNode(name);
        if (node < 0)
            return false;
        updatePose();
        out = mNodes[node].mWorld;
        return true;
    }

    bool Animation::getNodeObjectMatrixByRecordIndex(int recordIndex, osg::Matrixf& out) const
    {
        if (!mObjectModel)
            return false;
        const int node = mObjectModel->findNodeByRecordIndex(recordIndex);
        if (node < 0)
            return false;
        updatePose();
        out = mNodes[node].mWorld;
        return true;
    }

    bool Animation::getNodeWorldMatrix(std::string_view name, osg::Matrixf& out) const
    {
        if (!getNodeObjectMatrix(name, out))
            return false;
        out = out * getObjectWorldMatrix();
        return true;
    }

    int Animation::addPart(VFS::Path::NormalizedView model, std::string_view bonename, std::string_view filter,
        const osg::Quat* attitude)
    {
        if (!mObjectModel)
            return -1;
        auto part = std::make_unique<Part>();
        part->mModel = CtrContext::get()->getModels().get(model);
        const CtrScene::Model& m = *part->mModel;
        for (const CtrScene::ParticleSystem& system : m.mParticleSystems)
            part->mParticles.emplace_back(system);

        if (m.mHasSkin)
        {
            part->mKind = PartKind::Skinned;
            part->mMeshEnabled.resize(m.mMeshes.size(), 0);
            part->mBoneNodes.resize(m.mMeshes.size());
            for (std::size_t i = 0; i < m.mMeshes.size(); ++i)
            {
                const CtrScene::Mesh& mesh = m.mMeshes[i];
                if (!mesh.isSkinned() || !filterMatches(mesh.mName, filter))
                    continue;
                part->mMeshEnabled[i] = 1;
                for (const std::string& bone : mesh.mBoneNames)
                    part->mBoneNodes[i].push_back(static_cast<std::int16_t>(findNode(bone)));
            }
        }
        else
        {
            const int attach = findNode(bonename);
            if (attach < 0)
                throw std::runtime_error("Can't find attachment node " + std::string(bonename));
            part->mKind = PartKind::Rigid;
            part->mAttachNode = attach;
            part->mMeshEnabled.assign(m.mMeshes.size(), 1);
            m.computeBindPose(part->mBindPose);

            osg::Matrixf attachTransform;
            const int boneOffset = m.findNode("BoneOffset");
            if (boneOffset >= 0)
                attachTransform.makeTranslate(part->mBindPose[boneOffset].getTrans());
            if (mObjectModel->mNodes[attach].mName.find("Left") != std::string::npos)
                attachTransform = attachTransform * osg::Matrixf::scale(-1.f, 1.f, 1.f);
            if (attitude != nullptr)
                attachTransform = osg::Matrixf::rotate(*attitude) * attachTransform;
            part->mAttach = attachTransform;
        }

        for (std::size_t i = 0; i < mParts.size(); ++i)
        {
            if (mParts[i] == nullptr)
            {
                mParts[i] = std::move(part);
                return static_cast<int>(i);
            }
        }
        mParts.push_back(std::move(part));
        return static_cast<int>(mParts.size()) - 1;
    }

    void Animation::removePart(int id)
    {
        if (id < 0 || id >= static_cast<int>(mParts.size()))
            return;
        mParts[id].reset();
        if (mExtraLight && mExtraLight->mPart == id)
            mExtraLight.reset();
    }

    int Animation::addPartAtPartNode(VFS::Path::NormalizedView model, int parentPart, std::string_view nodeName)
    {
        const Part* parent = getPartData(parentPart);
        if (parent == nullptr || parent->mKind == PartKind::Skinned)
            return -1;
        const int node = parent->mModel->findNode(nodeName);
        if (node < 0)
            return -1;
        auto part = std::make_unique<Part>();
        part->mModel = CtrContext::get()->getModels().get(model);
        part->mKind = PartKind::Rigid;
        part->mAttachNode = parent->mAttachNode;
        part->mAttach = parent->mBindPose[node] * parent->mAttach;
        part->mMeshEnabled.assign(part->mModel->mMeshes.size(), 1);
        part->mModel->computeBindPose(part->mBindPose);
        for (const CtrScene::ParticleSystem& system : part->mModel->mParticleSystems)
            part->mParticles.emplace_back(system);
        for (std::size_t i = 0; i < mParts.size(); ++i)
        {
            if (mParts[i] == nullptr)
            {
                mParts[i] = std::move(part);
                return static_cast<int>(i);
            }
        }
        mParts.push_back(std::move(part));
        return static_cast<int>(mParts.size()) - 1;
    }

    bool Animation::getPartWorldPosition(int id, osg::Vec3f& out) const
    {
        const Part* part = id >= 0 && id < static_cast<int>(mParts.size()) ? mParts[id].get() : nullptr;
        if (part == nullptr)
            return false;
        updatePose();
        osg::Matrixf root;
        computePartRootWorld(*part, root);
        out = (root * getObjectWorldMatrix()).getTrans();
        return true;
    }

    void Animation::computePartRootWorld(const Part& part, osg::Matrixf& out) const
    {
        if (part.mAttachNode >= 0)
            out = part.mAttach * mNodes[part.mAttachNode].mWorld;
        else
            out = part.mAttach;
    }

    void Animation::addExtraLight(int part, const SceneUtil::LightCommon& esmLight)
    {
        if (esmLight.mNegative || esmLight.mRadius <= 0.f)
            return;
        mExtraLight = std::make_unique<ExtraLight>();
        mExtraLight->mPart = part;
        mExtraLight->mLight.mColor = toColor(esmLight.mColor);
        mExtraLight->mBaseRadius = esmLight.mRadius;
        mExtraLight->mLight.mRadius = esmLight.mRadius;
        mExtraLight->mFlicker = esmLight.mFlicker || esmLight.mFlickerSlow;
        mExtraLight->mPulse = esmLight.mPulse || esmLight.mPulseSlow;
        mExtraLight->mPhase = static_cast<float>(reinterpret_cast<std::uintptr_t>(this) % 1000) * 0.01f;
    }

    void Animation::collectLights(std::vector<const CtrScene::PointLight*>& out, float time)
    {
        if (!isRenderable())
            return;
        const osg::Matrixf object = getObjectWorldMatrix();
        if (mExtraLight)
        {
            osg::Vec3f position = object.getTrans();
            if (mExtraLight->mPart >= 0)
            {
                if (const Part* part = getPartData(mExtraLight->mPart))
                {
                    updatePose();
                    osg::Matrixf root;
                    computePartRootWorld(*part, root);
                    position = (root * object).getTrans();
                }
            }
            mExtraLight->mLight.mPosition = position;
            float radius = mExtraLight->mBaseRadius;
            if (mExtraLight->mFlicker)
                radius *= 0.9f + 0.1f * std::sin(time * 13.f + mExtraLight->mPhase) * std::sin(time * 7.f);
            else if (mExtraLight->mPulse)
                radius *= 0.85f + 0.15f * std::sin(time * 2.f + mExtraLight->mPhase);
            mExtraLight->mLight.mRadius = radius;
            out.push_back(&mExtraLight->mLight);
        }
        if (mLightEffect > 0.f)
        {
            mGlowLight.mPosition = object.getTrans() + osg::Vec3f(0.f, 0.f, 64.f);
            out.push_back(&mGlowLight);
        }
    }

    void Animation::addEffect(std::string_view model, std::string_view effectId, bool loop, std::string_view bonename,
        std::string_view /*texture*/, bool useAmbientLight, bool autoTransform,
        const std::optional<osg::Matrix>& transform)
    {
        if (!mObjectModel)
            return;

        for (const auto& part : mParts)
            if (part && part->mKind == PartKind::Effect && part->mEffectId == effectId && loop && !part->mFinished
                && part->mLoop && part->mBoneName == bonename)
                return;

        int attach = -1;
        if (!bonename.empty())
        {
            attach = findNode(bonename);
            if (attach < 0)
                throw std::runtime_error("Can't find bone " + std::string{ bonename });
        }

        osg::Matrixf finalTransform;
        if (!mPtr.getClass().isNpc() && autoTransform)
        {
            osg::Vec3f bounds(MWBase::Environment::get().getWorld()->getHalfExtents(mPtr) * 2.f);
            float scale = std::max({ bounds.x(), bounds.y(), bounds.z() / 2.f }) / 64.f;
            if (scale > 1.f)
                finalTransform = osg::Matrixf::scale(scale, scale, scale);
            float offset = 0.f;
            if (bounds.z() < 128.f)
                offset = bounds.z() - 128.f;
            else if (bounds.z() < bounds.x() + bounds.y())
                offset = 128.f - bounds.z();
            if (MWBase::Environment::get().getWorld()->isFlying(mPtr))
                offset /= 20.f;
            finalTransform.setTrans(osg::Vec3f(0.f, 0.f, offset * scale));
        }
        if (transform)
            finalTransform *= osg::Matrixf(*transform);

        auto part = std::make_unique<Part>();
        part->mModel = CtrContext::get()->getModels().get(VFS::Path::toNormalized(model));
        part->mKind = PartKind::Effect;
        part->mAttachNode = attach;
        part->mAttach = finalTransform;
        part->mMeshEnabled.assign(part->mModel->mMeshes.size(), 1);
        part->mModel->computeBindPose(part->mBindPose);
        for (const CtrScene::ParticleSystem& system : part->mModel->mParticleSystems)
            part->mParticles.emplace_back(system);
        part->mFullAmbient = useAmbientLight;
        part->mEffectId = effectId;
        part->mBoneName = bonename;
        part->mLoop = loop;
        part->mMaxTime = part->mModel->mMaxControllerTime;
        mParts.push_back(std::move(part));

        mHasMagicEffects = true;
    }

    void Animation::advanceEffects(float duration)
    {
        if (!mHasMagicEffects)
            return;
        for (auto& part : mParts)
        {
            if (!part || part->mKind != PartKind::Effect || part->mFinished)
                continue;
            part->mTime += duration;
            if (part->mTime >= part->mMaxTime)
            {
                if (part->mLoop)
                    part->mTime = part->mMaxTime > 0.f ? std::fmod(part->mTime, part->mMaxTime) : 0.f;
                else
                    part->mFinished = true;
            }
        }
    }

    void Animation::removeEffect(std::string_view effectId)
    {
        bool remaining = false;
        for (auto& part : mParts)
        {
            if (!part || part->mKind != PartKind::Effect)
                continue;
            if (effectId.empty() || part->mEffectId == effectId)
                part.reset();
            else
                remaining = true;
        }
        mHasMagicEffects = remaining;
    }

    void Animation::removeEffects()
    {
        removeEffect("");
    }

    std::vector<std::string_view> Animation::getLoopingEffects() const
    {
        std::vector<std::string_view> out;
        if (!mHasMagicEffects)
            return out;
        for (const auto& part : mParts)
            if (part && part->mKind == PartKind::Effect && part->mLoop && !part->mFinished)
                out.push_back(part->mEffectId);
        return out;
    }

    void Animation::updateEffects()
    {
        if (!mHasMagicEffects)
            return;
        bool remaining = false;
        for (auto& part : mParts)
        {
            if (!part || part->mKind != PartKind::Effect)
                continue;
            if (part->mFinished)
                part.reset();
            else
                remaining = true;
        }
        mHasMagicEffects = remaining;
    }

    bool Animation::upperBodyReady() const
    {
        for (const auto& [name, state] : mStates)
        {
            if (state.mPriority.contains(int(MWMechanics::Priority_Hit))
                || state.mPriority.contains(int(MWMechanics::Priority_Weapon))
                || state.mPriority.contains(int(MWMechanics::Priority_Knockdown))
                || state.mPriority.contains(int(MWMechanics::Priority_Death)))
                return false;
        }
        return true;
    }

    void Animation::setAlpha(float actorFade, float alpha)
    {
        mAlpha = alpha;
        mActorFade = actorFade;
    }

    void Animation::setLightEffect(float effect)
    {
        mLightEffect = effect;
        if (effect > 0.f)
        {
            const float radius = effect * std::ceil(Constants::UnitsPerFoot);
            mGlowLight.mRadius = radius * 3.f;
            mGlowLight.mColor = osg::Vec3f(1.5f, 1.5f, 1.5f);
        }
    }

    void Animation::addControllers()
    {
        auto setup = [this](RotateController& controller, std::string_view bone) {
            controller.mNode = findNode(bone);
        };
        setup(mHeadController, "bip01 head");
        setup(mSpineController, "bip01 spine1");
        setup(mRootController, "bip01");
        mControllerList.clear();
        for (RotateController* controller : { &mHeadController, &mSpineController, &mRootController })
            if (controller->mNode >= 0)
                mControllerList.push_back(controller);
    }

    void Animation::setHeadPitch(float pitchRadians)
    {
        mHeadPitchRadians = pitchRadians;
    }

    void Animation::setHeadYaw(float yawRadians)
    {
        mHeadYawRadians = yawRadians;
    }

    float Animation::getHeadPitch() const
    {
        return mHeadPitchRadians;
    }

    float Animation::getHeadYaw() const
    {
        return mHeadYawRadians;
    }

    void Animation::animationEnded(AnimState& state) const
    {
        MWBase::Environment::get().getLuaManager()->animationEnded(
            mPtr, state.mGroupname, state.getTime(), state.getCompletion(), state.mStartKey, state.mStopKey);
    }

    MWWorld::MovementDirectionFlags Animation::getSupportedMovementDirections(
        std::span<const std::string_view> prefixes) const
    {
        MWWorld::MovementDirectionFlags result = 0;
        for (const std::string_view prefix : prefixes)
        {
            auto it = std::find_if(mSupportedDirections.begin(), mSupportedDirections.end(),
                [prefix](const auto& direction) { return direction.first == prefix; });
            if (it == mSupportedDirections.end())
            {
                mSupportedDirections.emplace_back(prefix, 0);
                it = mSupportedDirections.end() - 1;
                for (const std::string_view animation : mSupportedAnimations)
                {
                    if (!animation.starts_with(prefix))
                        continue;
                    if (animation.ends_with("forward"))
                        it->second |= MWWorld::MovementDirectionFlag_Forward;
                    else if (animation.ends_with("back"))
                        it->second |= MWWorld::MovementDirectionFlag_Back;
                    else if (animation.ends_with("left"))
                        it->second |= MWWorld::MovementDirectionFlag_Left;
                    else if (animation.ends_with("right"))
                        it->second |= MWWorld::MovementDirectionFlag_Right;
                }
            }
            result |= it->second;
        }
        return result;
    }

    bool Animation::isRenderable() const
    {
        return mInsert != nullptr && mInsert->getNodeMask() != 0 && mActiveState != 0;
    }

    osg::BoundingSpheref Animation::getWorldBound() const
    {
        if (mInsert == nullptr)
            return osg::BoundingSpheref();
        const osg::Vec3f& position = mInsert->getPosition();
        const float scale = std::max({ mInsert->getScale().x(), mInsert->getScale().y(), mInsert->getScale().z() });
        const bool hasBounds = mObjectModel && mObjectModel->mBounds.valid();
        if (mPtr.getClass().isActor())
        {
            osg::BoundingSpheref bound(position + osg::Vec3f(0.f, 0.f, 64.f * scale), 110.f * scale);
            if (hasBounds)
            {
                const osg::BoundingBoxf& box = mObjectModel->mBounds;
                const osg::Vec3f center
                    = mInsert->getAttitude() * osg::componentMultiply(box.center(), mInsert->getScale());
                bound.expandBy(osg::BoundingSpheref(position + center, box.radius() * scale * 1.5f));
            }
            return bound;
        }
        if (!hasBounds)
            return osg::BoundingSpheref(position + osg::Vec3f(0.f, 0.f, 64.f * scale), 110.f * scale);
        const osg::BoundingBoxf& box = mObjectModel->mBounds;
        const osg::Vec3f center = mInsert->getAttitude() * osg::componentMultiply(box.center(), mInsert->getScale());
        return osg::BoundingSpheref(position + center, box.radius() * scale);
    }

    void Animation::logMeshes(const osg::Matrixf& view) const
    {
        if (!mObjectModel)
            return;
        updatePose();
        const osg::Matrixf object = getObjectWorldMatrix();
        const CtrScene::Model& model = *mObjectModel;
        Log(Debug::Info) << "[Visibility]   draw meshes " << mDrawObjectMeshes << " parts " << mParts.size()
                         << " scale " << mInsert->getScale().x();
        for (std::size_t i = 0; i < model.mMeshes.size(); ++i)
        {
            const CtrScene::Mesh& mesh = model.mMeshes[i];
            const CtrScene::Material& material = mesh.mMaterial;
            const bool switchedOff = mesh.mSwitch >= 0 && mesh.mSwitchChild != mSwitchChild[mesh.mSwitch];
            osg::Vec3f center;
            float determinant = 0.f;
            if (mesh.mNode >= 0 && static_cast<std::size_t>(mesh.mNode) < mNodes.size())
            {
                const osg::Matrixf modelView = mNodes[mesh.mNode].mWorld * object * view;
                center = mesh.mBound.center() * modelView;
                determinant = modelView(0, 0) * (modelView(1, 1) * modelView(2, 2) - modelView(1, 2) * modelView(2, 1))
                    - modelView(0, 1) * (modelView(1, 0) * modelView(2, 2) - modelView(1, 2) * modelView(2, 0))
                    + modelView(0, 2) * (modelView(1, 0) * modelView(2, 1) - modelView(1, 1) * modelView(2, 0));
            }
            const CtrGpu::Texture* texture = material.mTexture.get();
            Log(Debug::Info) << "[Visibility]   mesh '" << mesh.mName << "' node " << mesh.mNode << " skinned "
                             << mesh.isSkinned() << " switched off " << switchedOff << " tris "
                             << mesh.mGeometry.mIndexCount / 3 << " flags 0x" << std::hex << material.mFlags
                             << std::dec << " blend " << int(material.mSrcBlend) << "/" << int(material.mDstBlend)
                             << " test " << int(material.mAlphaFunc) << "/" << int(material.mAlphaRef) << " diffuse "
                             << material.mDiffuse.r() << "," << material.mDiffuse.g() << "," << material.mDiffuse.b()
                             << "," << material.mDiffuse.a() << " texture "
                             << (texture != nullptr ? std::to_string(texture->getWidth()) + "x"
                                         + std::to_string(texture->getHeight()) + " fmt "
                                         + std::to_string(static_cast<int>(texture->getFormat()))
                                         + (texture->inVram() ? " vram" : " linear")
                                                    : std::string("none"))
                             << " view " << center.x() << "," << center.y() << "," << center.z() << " r "
                             << mesh.mBound.radius() << " det " << determinant;
        }
    }

    void Animation::drawPart(CtrScene::Renderer& renderer, const Part& part, const osg::Matrixf& object,
        const CtrScene::DrawParams& params) const
    {
        if (!part.mVisible || part.mFinished)
            return;
        const CtrScene::Model& model = *part.mModel;
        CtrScene::DrawParams partParams = params;
        partParams.mFullAmbient = part.mFullAmbient && part.mKind == PartKind::Effect;
        if (part.mKind == PartKind::Skinned)
        {
            osg::Matrixf bones[256];
            for (std::size_t i = 0; i < model.mMeshes.size(); ++i)
            {
                if (!part.mMeshEnabled[i])
                    continue;
                const CtrScene::Mesh& mesh = model.mMeshes[i];
                const auto& boneNodes = part.mBoneNodes[i];
                for (std::size_t b = 0; b < boneNodes.size() && b < 256; ++b)
                    bones[b] = boneNodes[b] >= 0 ? mesh.mBoneBind[b] * mNodes[boneNodes[b]].mWorld : mesh.mBoneBind[b];
                renderer.drawSkinned(mesh, bones, object, partParams);
            }
            return;
        }

        osg::Matrixf root;
        computePartRootWorld(part, root);
        root = root * object;
        for (std::size_t i = 0; i < model.mMeshes.size(); ++i)
        {
            if (!part.mMeshEnabled[i])
                continue;
            const CtrScene::Mesh& mesh = model.mMeshes[i];
            if (mesh.isSkinned() || mesh.mNode < 0)
                continue;
            renderer.drawMesh(mesh, part.mBindPose[mesh.mNode] * root, partParams);
        }

        if (!part.mParticles.empty())
        {
            static std::vector<osg::Matrixf> nodeWorld;
            nodeWorld.resize(part.mBindPose.size());
            for (std::size_t i = 0; i < nodeWorld.size(); ++i)
                nodeWorld[i] = part.mBindPose[i] * root;
            const float input
                = part.mKind == PartKind::Effect ? part.mTime : renderer.getSceneParams().mTime;
            CtrScene::renderParticles(renderer, model, part.mParticles, part.mParticleTime, input, nodeWorld, partParams);
        }
    }

    void Animation::render(CtrScene::Renderer& renderer, const CtrScene::DrawParams& baseParams) const
    {
        if (!mObjectModel)
            return;
        updatePose();
        const osg::Matrixf object = getObjectWorldMatrix();
        CtrScene::DrawParams params = baseParams;
        params.mAlpha = baseParams.mAlpha * mAlpha * mActorFade;

        if (mDrawObjectMeshes)
        {
            const CtrScene::Model& model = *mObjectModel;
            osg::Matrixf bones[256];
            for (std::size_t i = 0; i < model.mMeshes.size(); ++i)
            {
                const CtrScene::Mesh& mesh = model.mMeshes[i];
                if (mHideTriBip && (mesh.mFlags & CtrScene::Mesh_TriBip))
                    continue;
                if (mesh.mSwitch >= 0 && mesh.mSwitchChild != mSwitchChild[mesh.mSwitch])
                    continue;
                if (mesh.isSkinned())
                {
                    const auto& boneNodes = mObjectBoneNodes[i];
                    for (std::size_t b = 0; b < boneNodes.size() && b < 256; ++b)
                        bones[b] = boneNodes[b] >= 0 ? mesh.mBoneBind[b] * mNodes[boneNodes[b]].mWorld
                                                     : mesh.mBoneBind[b];
                    renderer.drawSkinned(mesh, bones, object, params);
                }
                else if (mesh.mNode >= 0)
                    renderer.drawMesh(mesh, mNodes[mesh.mNode].mWorld * object, params);
            }
        }

        if (!mParticles.empty())
        {
            static std::vector<osg::Matrixf> nodeWorld;
            nodeWorld.resize(mNodes.size());
            for (std::size_t i = 0; i < nodeWorld.size(); ++i)
                nodeWorld[i] = mNodes[i].mWorld * object;
            CtrScene::renderParticles(renderer, *mObjectModel, mParticles, mParticleTime, renderer.getSceneParams().mTime,
                nodeWorld, params);
        }

        for (const auto& part : mParts)
            if (part)
                drawPart(renderer, *part, object, params);
    }


    ObjectAnimation::ObjectAnimation(const MWWorld::Ptr& ptr, const std::string& model,
        Resource::ResourceSystem* resourceSystem, bool animated, bool allowLight)
        : Animation(ptr, ptr.getRefData().getBaseNode(), resourceSystem)
    {
        if (!model.empty())
        {
            setObjectRoot(model, false, false, false);
            if (animated)
                addAnimSource(model, model);
        }
        if (ptr.getType() == ESM::Light::sRecordId && allowLight)
            addExtraLight(-1, SceneUtil::LightCommon(*ptr.get<ESM::Light>()->mBase));

        if (mObjectModel && Settings::game().mDayNightSwitches)
        {
            const int sw = mObjectModel->findSwitch(Constants::NightDayLabel);
            if (sw >= 0)
            {
                const unsigned int state = MWBase::Environment::get().getWorld()->getNightDayMode();
                mSwitchChild[sw] = static_cast<std::uint8_t>(
                    mObjectModel->mSwitches[sw].mNumChildren > state ? state : 0);
            }
        }

        if (Settings::game().mGraphicHerbalism && ptr.getRefData().getCustomData() != nullptr
            && ObjectAnimation::canBeHarvested())
            harvest(ptr);
    }

    void ObjectAnimation::harvest(const MWWorld::Ptr& ptr)
    {
        const MWWorld::ContainerStore& store = ptr.getClass().getContainerStore(ptr);
        if (!store.hasVisibleItems() && mObjectModel)
        {
            const int sw = mObjectModel->findSwitch(Constants::HerbalismLabel);
            if (sw >= 0)
                mSwitchChild[sw] = 1;
        }
    }

    bool ObjectAnimation::canBeHarvested() const
    {
        if (mPtr.getType() != ESM::Container::sRecordId)
            return false;

        const MWWorld::LiveCellRef<ESM::Container>* ref = mPtr.get<ESM::Container>();
        if (!(ref->mBase->mFlags & ESM::Container::Organic))
            return false;

        return mObjectModel && mObjectModel->findSwitch(Constants::HerbalismLabel) >= 0;
    }

    float Animation::AnimState::getCompletion() const
    {
        if (mStopTime > mStartTime)
            return (getTime() - mStartTime) / (mStopTime - mStartTime);
        else
            return mPlaying ? 0.0f : 1.0f;
    }
}
