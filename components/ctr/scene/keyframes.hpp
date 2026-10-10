#ifndef OPENMW_COMPONENTS_CTR_SCENE_KEYFRAMES_H
#define OPENMW_COMPONENTS_CTR_SCENE_KEYFRAMES_H

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <osg/Quat>
#include <osg/Vec3f>

#include <components/sceneutil/textkeymap.hpp>
#include <components/vfs/pathutil.hpp>

namespace Resource
{
    class NifFileManager;
}

namespace CtrScene
{
    template <class T>
    struct KeyChannel
    {
        std::vector<float> mTimes;
        std::vector<T> mValues;

        bool empty() const { return mTimes.empty(); }
        T sample(float time) const;
    };

    struct KfTrack
    {
        std::string mBone; ///< lower case
        KeyChannel<osg::Quat> mRotations;
        KeyChannel<float> mEuler[3];
        std::uint8_t mAxisOrder = 0;
        KeyChannel<osg::Vec3f> mTranslations;
        KeyChannel<float> mScales;

        bool hasRotation() const { return !mRotations.empty() || !mEuler[0].empty() || !mEuler[1].empty() || !mEuler[2].empty(); }
        osg::Quat getRotation(float time) const;
        osg::Vec3f getTranslation(float time) const { return mTranslations.sample(time); }
        float getScale(float time) const { return mScales.sample(time); }
    };

    struct KfAnimation
    {
        SceneUtil::TextKeyMap mTextKeys;
        std::vector<KfTrack> mTracks;

        const KfTrack* findTrack(std::string_view bone) const;
    };

    using KfAnimationPtr = std::shared_ptr<const KfAnimation>;

    class KeyframeCache
    {
    public:
        explicit KeyframeCache(Resource::NifFileManager* nifFileManager);

        KfAnimationPtr get(VFS::Path::NormalizedView path);

        void purgeUnused();
        void clear() { mAnimations.clear(); }

    private:
        Resource::NifFileManager* mNifFileManager;
        std::unordered_map<std::string, KfAnimationPtr> mAnimations;
    };
}

#endif
