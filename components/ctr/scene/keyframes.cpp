#include "keyframes.hpp"

#include <algorithm>

#include <components/debug/debuglog.hpp>
#include <components/misc/strings/algorithm.hpp>
#include <components/misc/strings/lower.hpp>
#include <components/nif/controller.hpp>
#include <components/nif/data.hpp>
#include <components/nif/extra.hpp>
#include <components/nif/niffile.hpp>
#include <components/nif/node.hpp>
#include <components/resource/niffilemanager.hpp>

namespace CtrScene
{
    namespace
    {
        template <class T>
        T lerp(const T& a, const T& b, float t)
        {
            return a + (b - a) * t;
        }

        osg::Quat lerp(const osg::Quat& a, const osg::Quat& b, float t)
        {
            osg::Quat out;
            out.slerp(t, a, b);
            return out;
        }

        template <class T>
        T identity()
        {
            return T();
        }

        template <>
        float identity<float>()
        {
            return 1.f;
        }

        template <class Map, class T>
        void copyKeys(const Map* map, KeyChannel<T>& out)
        {
            if (map == nullptr)
                return;
            out.mTimes.reserve(map->mKeys.size());
            out.mValues.reserve(map->mKeys.size());
            for (const auto& [time, key] : map->mKeys)
            {
                out.mTimes.push_back(time);
                out.mValues.push_back(key.mValue);
            }
        }
    }

    template <class T>
    T KeyChannel<T>::sample(float time) const
    {
        if (mTimes.empty())
            return identity<T>();
        if (time <= mTimes.front())
            return mValues.front();
        if (time >= mTimes.back())
            return mValues.back();
        const auto it = std::upper_bound(mTimes.begin(), mTimes.end(), time);
        const std::size_t i = static_cast<std::size_t>(it - mTimes.begin());
        const float t0 = mTimes[i - 1];
        const float t1 = mTimes[i];
        const float f = t1 > t0 ? (time - t0) / (t1 - t0) : 0.f;
        return lerp(mValues[i - 1], mValues[i], f);
    }

    template struct KeyChannel<osg::Quat>;
    template struct KeyChannel<osg::Vec3f>;
    template struct KeyChannel<float>;

    osg::Quat KfTrack::getRotation(float time) const
    {
        if (!mRotations.empty())
            return mRotations.sample(time);

        const float angles[3] = { mEuler[0].empty() ? 0.f : mEuler[0].sample(time),
            mEuler[1].empty() ? 0.f : mEuler[1].sample(time), mEuler[2].empty() ? 0.f : mEuler[2].sample(time) };
        static const osg::Vec3f axes[3] = { osg::Vec3f(1, 0, 0), osg::Vec3f(0, 1, 0), osg::Vec3f(0, 0, 1) };
        static const int orders[6][3] = { { 0, 1, 2 }, { 0, 2, 1 }, { 1, 2, 0 }, { 1, 0, 2 }, { 2, 0, 1 }, { 2, 1, 0 } };
        const int* order = orders[mAxisOrder < 6 ? mAxisOrder : 0];
        osg::Quat result;
        for (int i = 0; i < 3; ++i)
            result = osg::Quat(angles[order[i]], axes[order[i]]) * result;
        return result;
    }

    const KfTrack* KfAnimation::findTrack(std::string_view bone) const
    {
        for (const KfTrack& track : mTracks)
            if (Misc::StringUtils::ciEqual(track.mBone, bone))
                return &track;
        return nullptr;
    }

    KeyframeCache::KeyframeCache(Resource::NifFileManager* nifFileManager)
        : mNifFileManager(nifFileManager)
    {
    }

    KfAnimationPtr KeyframeCache::get(VFS::Path::NormalizedView path)
    {
        const std::string key(path.value());
        if (auto found = mAnimations.find(key); found != mAnimations.end())
            return found->second;

        std::shared_ptr<KfAnimation> animation;
        try
        {
            Nif::NIFFilePtr file = mNifFileManager->get(path);
            const Nif::FileView nif(*file);
            const Nif::NiSequenceStreamHelper* seq = nullptr;
            for (std::size_t i = 0; i < nif.numRoots(); ++i)
            {
                const Nif::Record* r = nif.getRoot(i);
                if (r != nullptr && r->mRecordType == Nif::RC_NiSequenceStreamHelper)
                {
                    seq = static_cast<const Nif::NiSequenceStreamHelper*>(r);
                    break;
                }
            }

            const Nif::ExtraList extraList = seq != nullptr ? seq->getExtraList() : Nif::ExtraList();
            if (seq != nullptr && !extraList.empty() && !extraList[0].empty()
                && extraList[0]->mRecordType == Nif::RC_NiTextKeyExtraData)
            {
                animation = std::make_shared<KfAnimation>();
                const auto* textKeys = static_cast<const Nif::NiTextKeyExtraData*>(extraList[0].getPtr());
                for (const Nif::NiTextKeyExtraData::TextKey& textKey : textKeys->mList)
                {
                    std::vector<std::string> lines;
                    Misc::StringUtils::split(textKey.mText, lines, "\r\n");
                    for (std::string& line : lines)
                    {
                        Misc::StringUtils::trim(line);
                        Misc::StringUtils::lowerCaseInPlace(line);
                        if (!line.empty())
                            animation->mTextKeys.emplace(textKey.mTime, std::move(line));
                    }
                }

                Nif::NiTimeControllerPtr ctrl = seq->mController;
                for (std::size_t i = 1; i < extraList.size() && !ctrl.empty(); i++, (ctrl = ctrl->mNext))
                {
                    const Nif::ExtraPtr& extra = extraList[i];
                    if (extra.empty() || extra->mRecordType != Nif::RC_NiStringExtraData
                        || ctrl->mRecordType != Nif::RC_NiKeyframeController)
                        continue;
                    const auto* name = static_cast<const Nif::NiStringExtraData*>(extra.getPtr());
                    const auto* keyCtrl = static_cast<const Nif::NiKeyframeController*>(ctrl.getPtr());
                    if (keyCtrl->mData.empty())
                        continue;
                    const std::string bone = Misc::StringUtils::lowerCase(name->mData);
                    if (animation->findTrack(bone) != nullptr)
                        continue;
                    const Nif::NiKeyframeData& data = *keyCtrl->mData.getPtr();
                    KfTrack track;
                    track.mBone = bone;
                    if (data.mRotations && data.mRotations->mInterpolationType == Nif::InterpolationType_XYZ)
                    {
                        copyKeys(data.mXRotations.get(), track.mEuler[0]);
                        copyKeys(data.mYRotations.get(), track.mEuler[1]);
                        copyKeys(data.mZRotations.get(), track.mEuler[2]);
                        track.mAxisOrder = static_cast<std::uint8_t>(data.mAxisOrder);
                    }
                    else
                        copyKeys(data.mRotations.get(), track.mRotations);
                    copyKeys(data.mTranslations.get(), track.mTranslations);
                    copyKeys(data.mScales.get(), track.mScales);
                    animation->mTracks.push_back(std::move(track));
                }
                animation->mTracks.shrink_to_fit();
            }
        }
        catch (const std::exception& e)
        {
            Log(Debug::Error) << "Failed to load animation " << path << ": " << e.what();
            animation = nullptr;
        }
        mNifFileManager->release(path);
        mAnimations.emplace(key, animation);
        return animation;
    }

    void KeyframeCache::purgeUnused()
    {
        for (auto it = mAnimations.begin(); it != mAnimations.end();)
        {
            if (it->second == nullptr || it->second.use_count() == 1)
                it = mAnimations.erase(it);
            else
                ++it;
        }
    }
}
