#ifndef GAME_SOUND_NDSPOUTPUT_H
#define GAME_SOUND_NDSPOUTPUT_H


#include <array>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

#include <osg/Vec3f>

#include "soundoutput.hpp"

namespace MWSound
{
    class SoundManager;
    class Sound;
    class SoundBase;
    struct PrebuiltAdpcm;

    class NdspOutput final : public SoundOutput
    {
    public:
        explicit NdspOutput(SoundManager& mgr);
        ~NdspOutput() override;

        std::vector<std::string> enumerate() override;
        bool init(const std::string& devname, const std::string& hrtfname, HrtfMode hrtfmode) override;
        void deinit() override;

        std::vector<std::string> enumerateHrtf() override { return {}; }

        std::pair<Sound_Handle, size_t> loadSound(VFS::Path::NormalizedView fname) override;
        size_t unloadSound(Sound_Handle data) override;

        bool playSound(Sound* sound, Sound_Handle data, float offset) override;
        bool playSound3D(Sound* sound, Sound_Handle data, float offset) override;
        void finishSound(Sound* sound) override;
        bool isSoundPlaying(Sound* sound) override;
        void updateSound(Sound* sound) override;

        bool streamSound(DecoderPtr decoder, Stream* sound, bool getLoudnessData = false) override;
        bool streamSound3D(DecoderPtr decoder, Stream* sound, bool getLoudnessData) override;
        void finishStream(Stream* sound) override;
        double getStreamDelay(Stream* sound) override;
        float getStreamOffset(Stream* sound) override;
        float getStreamLoudness(Stream* sound) override;
        bool isStreamPlaying(Stream* sound) override;
        void updateStream(Stream* sound) override;

        void startUpdate() override {}
        void finishUpdate() override;

        void updateListener(const osg::Vec3f& pos, const osg::Vec3f& atdir, const osg::Vec3f& updir,
            const osg::Vec3f& vel, Environment env) override;

        void pauseSounds(int types) override;
        void resumeSounds(int types) override;

        void pauseActiveDevice() override;
        void resumeActiveDevice() override;

        struct Buffer;
        struct Voice;
        struct StreamVoice;

    private:
        static constexpr int sNumChannels = 24;

        int allocChannel();
        void releaseChannel(int channel);
        void stopVoice(Voice& voice);

        bool startSound(Sound* sound, Buffer* buffer, float offset, bool is3D);
        bool startStream(DecoderPtr decoder, Stream* sound, bool is3D);
        bool startAdpcmStream(
            DecoderPtr decoder, std::shared_ptr<const PrebuiltAdpcm> prebuilt, Stream* sound, bool is3D);

        void applyParams(Voice& voice, SoundBase& sound);
        float getTimeScaledPitch(SoundBase& sound) const;

        void refill(StreamVoice& stream);
        void refillAdpcm(StreamVoice& stream);

        static void dspFrameCallback(void* data);
        static void streamThreadMain(void* arg);

        std::array<bool, sNumChannels> mChannelUsed{};
        std::vector<Sound*> mActiveSounds;
        std::vector<Stream*> mActiveStreams;

        std::vector<void*> mPendingFree;
        std::vector<void*> mFreeNow;

        std::mutex mStreamMutex;
        void* mThread = nullptr;
        void* mWakeEvent = nullptr;
        volatile bool mQuit = false;

        osg::Vec3f mListenerPos;
        osg::Vec3f mListenerDir{ 0.f, 1.f, 0.f };
        osg::Vec3f mListenerUp{ 0.f, 0.f, 1.f };
        Environment mListenerEnv = Env_Normal;
        bool mDeviceInitialised = false;
    };
}

#endif
