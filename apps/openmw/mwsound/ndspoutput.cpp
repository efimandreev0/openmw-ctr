#include "ndspoutput.hpp"

#include <3ds.h>

#include <algorithm>
#include <cmath>
#include <cstring>

#include <components/debug/debuglog.hpp>
#include <components/misc/resourcehelpers.hpp>

#include "adpcmpack.hpp"
#include "nsadecoder.hpp"
#include "sound.hpp"
#include "sounddecoder.hpp"
#include "soundmanagerimp.hpp"

namespace MWSound
{
    namespace
    {
        constexpr std::size_t sStreamBuffers = 4;
        constexpr std::size_t sStreamBufferFrames = 2048;
        constexpr float sUnderwaterCutoff = 1200.f;

        u16 ndspFormat(ChannelConfig chans, SampleType type)
        {
            const bool stereo = chans == ChannelConfig_Stereo;
            if (type == SampleType_UInt8)
                return stereo ? NDSP_FORMAT_STEREO_PCM8 : NDSP_FORMAT_MONO_PCM8;
            return stereo ? NDSP_FORMAT_STEREO_PCM16 : NDSP_FORMAT_MONO_PCM16;
        }

        std::size_t ndspFrameBytes(u16 format)
        {
            const std::size_t channels = (format == NDSP_FORMAT_STEREO_PCM8 || format == NDSP_FORMAT_STEREO_PCM16) ? 2 : 1;
            const std::size_t sampleBytes = (format == NDSP_FORMAT_MONO_PCM8 || format == NDSP_FORMAT_STEREO_PCM8) ? 1 : 2;
            return channels * sampleBytes;
        }

        bool isBusy(const ndspWaveBuf& buf)
        {
            const u8 status = *static_cast<const volatile u8*>(&buf.status);
            return status == NDSP_WBUF_QUEUED || status == NDSP_WBUF_PLAYING;
        }
    }

    struct NdspOutput::Buffer
    {
        void* mData = nullptr;
        u32 mFrames = 0;
        u16 mFormat = NDSP_FORMAT_MONO_PCM16;
        int mRate = 22050;
        std::size_t mBytes = 0;
        bool mAdpcm = false;
        std::array<u16, 16> mCoefficients{};
        std::vector<ndspAdpcmData> mBlockContexts;
        u32 mBlockSamples = 0;
    };

    struct NdspOutput::Voice
    {
        int mChannel = -1;
        int mChannel2 = -1;
        bool mIs3D = false;
        bool mPaused = false;
        int mRate = 22050;
        u16 mFormat = NDSP_FORMAT_MONO_PCM16;
        bool mFilterOn = false;
    };

    struct SoundVoice : NdspOutput::Voice
    {
        NdspOutput::Buffer* mBuffer = nullptr;
        ndspWaveBuf mWave{};
        ndspAdpcmData mAdpcmStart{};
    };

    struct NdspOutput::StreamVoice : NdspOutput::Voice
    {
        DecoderPtr mDecoder;
        void* mMemory = nullptr;
        std::size_t mFrameBytes = 0;
        std::array<ndspWaveBuf, sStreamBuffers> mWaves{};
        std::array<bool, sStreamBuffers> mCounted{};
        std::size_t mNext = 0;
        std::size_t mFramesQueued = 0;
        std::size_t mFramesDone = 0;
        bool mEof = false;

        std::shared_ptr<const PrebuiltAdpcm> mAdpcm;
        std::uint32_t mNextBlock = 0;
        std::size_t mBufferBytes = 0;
        std::array<ndspWaveBuf, sStreamBuffers> mWaves2{};
        std::array<ndspAdpcmData, sStreamBuffers> mContexts{};
        std::array<ndspAdpcmData, sStreamBuffers> mContexts2{};
    };

    namespace
    {
        void setupAdpcmChannel(int channel, const std::array<std::uint16_t, 16>& coefficients)
        {
            std::array<u16, 16> copy = coefficients;
            ndspChnReset(channel);
            ndspChnSetInterp(channel, NDSP_INTERP_LINEAR);
            ndspChnSetFormat(channel, NDSP_FORMAT_MONO_ADPCM);
            ndspChnSetAdpcmCoefs(channel, copy.data());
        }

        NdspOutput::Buffer* loadPrebuiltBuffer(SoundDecoder& decoder)
        {
            auto* ctr = dynamic_cast<CtrAudioDecoder*>(&decoder);
            std::shared_ptr<const PrebuiltAdpcm> sound = ctr != nullptr ? ctr->getPrebuilt() : nullptr;
            if (sound == nullptr || sound->mHeader.mChannels != 1)
                return nullptr;
            const CtrPack::AdpcmHeader& header = sound->mHeader;
            const std::size_t frames = (header.mNumSamples + CtrPack::sSamplesPerFrame - 1) / CtrPack::sSamplesPerFrame;
            const std::size_t bytes = frames * CtrPack::sBytesPerFrame;
            void* data = linearAlloc(bytes);
            if (data == nullptr)
                return nullptr;
            if (!sound->mPack->read(sound->mOffset + header.mDataOffset, data, bytes))
            {
                linearFree(data);
                return nullptr;
            }
            DSP_FlushDataCache(data, bytes);

            auto* buffer = new NdspOutput::Buffer;
            buffer->mData = data;
            buffer->mFrames = header.mNumSamples;
            buffer->mFormat = NDSP_FORMAT_MONO_ADPCM;
            buffer->mRate = static_cast<int>(header.mSampleRate);
            buffer->mBytes = bytes;
            buffer->mAdpcm = true;
            buffer->mCoefficients = sound->mCoefficients[0];
            buffer->mBlockSamples = header.mBlockFrames * CtrPack::sSamplesPerFrame;
            buffer->mBlockContexts.resize(header.mNumBlocks);
            for (std::uint32_t b = 0; b < header.mNumBlocks; ++b)
            {
                const CtrPack::AdpcmContext& context = sound->context(b, 0);
                buffer->mBlockContexts[b] = ndspAdpcmData{ context.mPredScale, context.mHistory0, context.mHistory1 };
            }
            return buffer;
        }
    }

    NdspOutput::NdspOutput(SoundManager& mgr)
        : SoundOutput(mgr)
    {
    }

    NdspOutput::~NdspOutput()
    {
        NdspOutput::deinit();
    }

    std::vector<std::string> NdspOutput::enumerate()
    {
        return { "3DS DSP" };
    }

    bool NdspOutput::init(const std::string&, const std::string&, HrtfMode)
    {
        deinit();

        const Result rc = ndspInit();
        if (R_FAILED(rc))
        {
            Log(Debug::Error) << "ndspInit failed (0x" << std::hex << rc << std::dec
                              << "), is /3ds/dspfirm.cdc present? Sound disabled.";
            return false;
        }
        mDeviceInitialised = true;
        ndspSetOutputMode(NDSP_OUTPUT_STEREO);
        ndspSetMasterVol(1.f);

        auto* event = new LightEvent;
        LightEvent_Init(event, RESET_ONESHOT);
        mWakeEvent = event;
        mQuit = false;

        s32 prio = 0x30;
        svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
        prio = std::max<s32>(0x18, prio - 1);
        Thread thread = threadCreate(&NdspOutput::streamThreadMain, this, 64 * 1024, prio, 1, false);
        if (!thread)
            thread = threadCreate(&NdspOutput::streamThreadMain, this, 64 * 1024, prio, -2, false);
        if (!thread)
        {
            Log(Debug::Error) << "Failed to create the audio stream thread";
            deinit();
            return false;
        }
        mThread = thread;
        ndspSetCallback(&NdspOutput::dspFrameCallback, this);

        mInitialized = true;
        Log(Debug::Info) << "NDSP audio output initialised";
        return true;
    }

    void NdspOutput::deinit()
    {
        if (mThread)
        {
            mQuit = true;
            LightEvent_Signal(static_cast<LightEvent*>(mWakeEvent));
            threadJoin(static_cast<Thread>(mThread), U64_MAX);
            threadFree(static_cast<Thread>(mThread));
            mThread = nullptr;
        }
        if (mDeviceInitialised)
            ndspSetCallback(nullptr, nullptr);

        while (!mActiveStreams.empty())
            finishStream(mActiveStreams.back());
        while (!mActiveSounds.empty())
            finishSound(mActiveSounds.back());

        delete static_cast<LightEvent*>(mWakeEvent);
        mWakeEvent = nullptr;

        if (mDeviceInitialised)
        {
            ndspExit();
            mDeviceInitialised = false;
        }
        for (void* ptr : mFreeNow)
            linearFree(ptr);
        for (void* ptr : mPendingFree)
            linearFree(ptr);
        mFreeNow.clear();
        mPendingFree.clear();
        mInitialized = false;
    }

    void NdspOutput::dspFrameCallback(void* data)
    {
        auto* self = static_cast<NdspOutput*>(data);
        LightEvent_Signal(static_cast<LightEvent*>(self->mWakeEvent));
    }

    void NdspOutput::streamThreadMain(void* arg)
    {
        auto* self = static_cast<NdspOutput*>(arg);
        auto* event = static_cast<LightEvent*>(self->mWakeEvent);
        while (!self->mQuit)
        {
            LightEvent_Wait(event);
            if (self->mQuit)
                break;
            std::lock_guard<std::mutex> lock(self->mStreamMutex);
            for (Stream* stream : self->mActiveStreams)
                if (stream->mHandle)
                    self->refill(*static_cast<StreamVoice*>(stream->mHandle));
        }
    }

    int NdspOutput::allocChannel()
    {
        for (int i = 0; i < sNumChannels; ++i)
        {
            if (!mChannelUsed[i])
            {
                mChannelUsed[i] = true;
                return i;
            }
        }
        return -1;
    }

    void NdspOutput::releaseChannel(int channel)
    {
        if (channel >= 0 && channel < sNumChannels)
            mChannelUsed[channel] = false;
    }

    void NdspOutput::stopVoice(Voice& voice)
    {
        for (int* channel : { &voice.mChannel, &voice.mChannel2 })
        {
            if (*channel < 0)
                continue;
            ndspChnWaveBufClear(*channel);
            ndspChnReset(*channel);
            releaseChannel(*channel);
            *channel = -1;
        }
    }

    std::pair<Sound_Handle, size_t> NdspOutput::loadSound(VFS::Path::NormalizedView fname)
    {
        std::vector<char> data;
        int rate = 0;
        ChannelConfig chans = ChannelConfig_Mono;
        SampleType type = SampleType_Int16;

        try
        {
            DecoderPtr decoder = mManager.getDecoder();
            const VFS::Path::Normalized corrected
                = Misc::ResourceHelpers::correctSoundPath(fname, *decoder->mResourceMgr);
            decoder->open(corrected);
            if (Buffer* prebuilt = loadPrebuiltBuffer(*decoder))
            {
                decoder->close();
                return { prebuilt, prebuilt->mBytes };
            }
            decoder->getInfo(&rate, &chans, &type);
            if ((chans == ChannelConfig_Mono || chans == ChannelConfig_Stereo) && type != SampleType_Float32)
                decoder->readAll(data);
            decoder->close();
        }
        catch (const std::exception& e)
        {
            Log(Debug::Error) << "Failed to load audio from " << fname << ": " << e.what();
            data.clear();
        }

        if (data.empty())
        {
            chans = ChannelConfig_Mono;
            type = SampleType_UInt8;
            rate = 8000;
            data.assign(64, static_cast<char>(-128));
        }

        auto buffer = std::make_unique<Buffer>();
        buffer->mFormat = ndspFormat(chans, type);
        buffer->mRate = rate;
        const std::size_t frameBytes = ndspFrameBytes(buffer->mFormat);
        buffer->mFrames = static_cast<u32>(data.size() / frameBytes);
        buffer->mBytes = buffer->mFrames * frameBytes;
        buffer->mData = linearAlloc(buffer->mBytes);
        if (!buffer->mData)
        {
            Log(Debug::Warning) << "Out of linear memory for sound " << fname;
            return { nullptr, 0 };
        }
        std::memcpy(buffer->mData, data.data(), buffer->mBytes);
        if (type == SampleType_UInt8)
        {
            auto* bytes = static_cast<unsigned char*>(buffer->mData);
            for (std::size_t i = 0; i < buffer->mBytes; ++i)
                bytes[i] ^= 0x80;
        }
        DSP_FlushDataCache(buffer->mData, buffer->mBytes);

        const std::size_t size = buffer->mBytes;
        return { buffer.release(), size };
    }

    size_t NdspOutput::unloadSound(Sound_Handle data)
    {
        auto* buffer = static_cast<Buffer*>(data);
        if (!buffer)
            return 0;

        for (Sound* sound : mActiveSounds)
        {
            auto* voice = static_cast<SoundVoice*>(sound->mHandle);
            if (voice && voice->mBuffer == buffer)
            {
                stopVoice(*voice);
                voice->mBuffer = nullptr;
            }
        }

        const std::size_t size = buffer->mBytes;
        mPendingFree.push_back(buffer->mData);
        delete buffer;
        return size;
    }

    float NdspOutput::getTimeScaledPitch(SoundBase& sound) const
    {
        const bool shouldScale = !(sound.mParams.mFlags & PlayMode::NoScaling);
        return shouldScale ? sound.getPitch() * mManager.getSimulationTimeScale() : sound.getPitch();
    }

    void NdspOutput::applyParams(Voice& voice, SoundBase& sound)
    {
        if (voice.mChannel < 0)
            return;

        float gain = sound.getRealVolume();
        float pitch = getTimeScaledPitch(sound);
        float left = 1.f;
        float right = 1.f;

        const bool underwater = sound.getUseEnv() && mListenerEnv == Env_Underwater;
        if (underwater)
        {
            gain *= 0.9f;
            pitch *= 0.7f;
        }

        if (voice.mIs3D)
        {
            const osg::Vec3f offset = sound.getPosition() - mListenerPos;
            const float distance = offset.length();
            const float minDist = std::max(sound.getMinDistance(), 1.f);
            const float maxDist = std::max(sound.getMaxDistance(), minDist);
            if (distance > maxDist)
                gain = 0.f;
            else
            {
                const float clamped = std::clamp(distance, minDist, maxDist);
                gain *= minDist / (minDist + (clamped - minDist));

                if (distance > 1.f)
                {
                    osg::Vec3f rightDir = mListenerDir ^ mListenerUp;
                    rightDir.normalize();
                    const float pan = std::clamp((offset * rightDir) / distance, -1.f, 1.f);
                    left = pan > 0.f ? 1.f - 0.7f * pan : 1.f;
                    right = pan < 0.f ? 1.f + 0.7f * pan : 1.f;
                }
            }
        }

        float mix[12] = {};
        mix[0] = gain * left;
        mix[1] = gain * right;
        if (voice.mChannel2 >= 0)
        {
            float mixRight[12] = {};
            mixRight[1] = mix[1];
            mix[1] = 0.f;
            ndspChnSetMix(voice.mChannel2, mixRight);
        }
        ndspChnSetMix(voice.mChannel, mix);
        const float rate = static_cast<float>(voice.mRate) * std::max(pitch, 0.01f);
        const bool filterChanged = underwater != voice.mFilterOn;
        voice.mFilterOn = underwater;
        for (const int channel : { voice.mChannel, voice.mChannel2 })
        {
            if (channel < 0)
                continue;
            ndspChnSetRate(channel, rate);
            if (filterChanged)
            {
                if (underwater)
                    ndspChnIirMonoSetParamsLowPassFilter(channel, sUnderwaterCutoff);
                ndspChnIirMonoSetEnable(channel, underwater);
            }
        }
    }

    bool NdspOutput::startSound(Sound* sound, Buffer* buffer, float offset, bool is3D)
    {
        if (!buffer)
            return false;
        const int channel = allocChannel();
        if (channel < 0)
        {
            Log(Debug::Warning) << "No free sources!";
            return false;
        }

        auto* voice = new SoundVoice;
        voice->mChannel = channel;
        voice->mIs3D = is3D;
        voice->mBuffer = buffer;
        voice->mRate = buffer->mRate;
        voice->mFormat = buffer->mFormat;

        u32 startFrame = 0;
        if (offset > 0.f && !sound->getIsLooping())
            startFrame = std::min<u32>(static_cast<u32>(offset * buffer->mRate), buffer->mFrames);
        if (startFrame >= buffer->mFrames)
            startFrame = 0;

        if (buffer->mAdpcm)
            setupAdpcmChannel(channel, buffer->mCoefficients);
        else
        {
            ndspChnReset(channel);
            ndspChnSetInterp(channel, NDSP_INTERP_LINEAR);
            ndspChnSetFormat(channel, buffer->mFormat);
        }

        sound->mHandle = voice;
        applyParams(*voice, *sound);

        if (buffer->mAdpcm)
        {
            const u32 block = std::min<u32>(startFrame / std::max<u32>(buffer->mBlockSamples, 1),
                static_cast<u32>(buffer->mBlockContexts.size()) - 1);
            startFrame = block * buffer->mBlockSamples;
            voice->mAdpcmStart = buffer->mBlockContexts[block];
            voice->mWave.data_vaddr = static_cast<const char*>(buffer->mData)
                + startFrame / CtrPack::sSamplesPerFrame * CtrPack::sBytesPerFrame;
            voice->mWave.adpcm_data = &voice->mAdpcmStart;
        }
        else
            voice->mWave.data_vaddr
                = static_cast<const char*>(buffer->mData) + startFrame * ndspFrameBytes(buffer->mFormat);
        voice->mWave.nsamples = buffer->mFrames - startFrame;
        voice->mWave.looping = sound->getIsLooping();
        ndspChnWaveBufAdd(channel, &voice->mWave);

        mActiveSounds.push_back(sound);
        return true;
    }

    bool NdspOutput::playSound(Sound* sound, Sound_Handle data, float offset)
    {
        return startSound(sound, static_cast<Buffer*>(data), offset, false);
    }

    bool NdspOutput::playSound3D(Sound* sound, Sound_Handle data, float offset)
    {
        return startSound(sound, static_cast<Buffer*>(data), offset, true);
    }

    void NdspOutput::finishSound(Sound* sound)
    {
        auto* voice = static_cast<SoundVoice*>(sound->mHandle);
        if (!voice)
            return;
        stopVoice(*voice);
        delete voice;
        sound->mHandle = nullptr;
        mActiveSounds.erase(std::remove(mActiveSounds.begin(), mActiveSounds.end(), sound), mActiveSounds.end());
    }

    bool NdspOutput::isSoundPlaying(Sound* sound)
    {
        auto* voice = static_cast<SoundVoice*>(sound->mHandle);
        if (!voice || voice->mChannel < 0 || !voice->mBuffer)
            return false;
        return voice->mPaused || isBusy(voice->mWave);
    }

    void NdspOutput::updateSound(Sound* sound)
    {
        if (auto* voice = static_cast<SoundVoice*>(sound->mHandle))
            applyParams(*voice, *sound);
    }

    void NdspOutput::refill(StreamVoice& stream)
    {
        if (stream.mChannel < 0)
            return;
        for (std::size_t i = 0; i < sStreamBuffers; ++i)
        {
            ndspWaveBuf& wave = stream.mWaves[i];
            if (!stream.mCounted[i] && !isBusy(wave) && wave.status == NDSP_WBUF_DONE)
            {
                stream.mFramesDone += wave.nsamples;
                stream.mCounted[i] = true;
            }
        }
        if (stream.mAdpcm)
        {
            refillAdpcm(stream);
            return;
        }

        while (!stream.mEof)
        {
            ndspWaveBuf& wave = stream.mWaves[stream.mNext];
            if (isBusy(wave))
                break;
            if (!stream.mCounted[stream.mNext] && wave.status == NDSP_WBUF_DONE)
                stream.mFramesDone += wave.nsamples;

            char* memory = static_cast<char*>(stream.mMemory)
                + stream.mNext * sStreamBufferFrames * stream.mFrameBytes;
            std::size_t got = 0;
            try
            {
                got = stream.mDecoder->read(memory, sStreamBufferFrames * stream.mFrameBytes);
            }
            catch (const std::exception& e)
            {
                Log(Debug::Error) << "Stream decoding error: " << e.what();
            }
            const std::size_t frames = got / stream.mFrameBytes;
            if (frames == 0)
            {
                stream.mEof = true;
                break;
            }

            DSP_FlushDataCache(memory, frames * stream.mFrameBytes);
            wave = ndspWaveBuf{};
            wave.data_vaddr = memory;
            wave.nsamples = static_cast<u32>(frames);
            wave.looping = false;
            stream.mCounted[stream.mNext] = false;
            ndspChnWaveBufAdd(stream.mChannel, &wave);
            stream.mFramesQueued += frames;
            stream.mNext = (stream.mNext + 1) % sStreamBuffers;
        }
    }

    void NdspOutput::refillAdpcm(StreamVoice& stream)
    {
        const PrebuiltAdpcm& sound = *stream.mAdpcm;
        while (!stream.mEof)
        {
            const std::size_t slot = stream.mNext;
            ndspWaveBuf& wave = stream.mWaves[slot];
            if (isBusy(wave) || (stream.mChannel2 >= 0 && isBusy(stream.mWaves2[slot])))
                break;
            if (!stream.mCounted[slot] && wave.status == NDSP_WBUF_DONE)
                stream.mFramesDone += wave.nsamples;

            const std::uint32_t block = stream.mNextBlock;
            const std::uint32_t frames = sound.blockFrames(block);
            const std::size_t channelBytes = static_cast<std::size_t>(frames) * CtrPack::sBytesPerFrame;
            u8* memory = static_cast<u8*>(stream.mMemory) + slot * stream.mBufferBytes;
            if (frames == 0)
            {
                stream.mEof = true;
                break;
            }
            if (!sound.mPack->read(sound.blockOffset(block), memory, channelBytes * sound.mHeader.mChannels))
            {
                Log(Debug::Error) << "Failed to read a prebuilt sound stream";
                stream.mEof = true;
                break;
            }
            DSP_FlushDataCache(memory, channelBytes * sound.mHeader.mChannels);

            const u32 samples = sound.blockSamples(block);
            auto queue = [&](int channel, ndspWaveBuf& buffer, ndspAdpcmData& state, int index) {
                const CtrPack::AdpcmContext& context = sound.context(block, index);
                state = ndspAdpcmData{ context.mPredScale, context.mHistory0, context.mHistory1 };
                buffer = ndspWaveBuf{};
                buffer.data_adpcm = memory + index * channelBytes;
                buffer.nsamples = samples;
                buffer.adpcm_data = &state;
                ndspChnWaveBufAdd(channel, &buffer);
            };
            queue(stream.mChannel, wave, stream.mContexts[slot], 0);
            if (stream.mChannel2 >= 0)
                queue(stream.mChannel2, stream.mWaves2[slot], stream.mContexts2[slot], 1);
            stream.mCounted[slot] = false;
            stream.mFramesQueued += samples;
            stream.mNext = (slot + 1) % sStreamBuffers;
            ++stream.mNextBlock;
        }
    }

    bool NdspOutput::startAdpcmStream(
        DecoderPtr decoder, std::shared_ptr<const PrebuiltAdpcm> prebuilt, Stream* sound, bool is3D)
    {
        const bool stereo = prebuilt->mHeader.mChannels == 2;
        const int channel = allocChannel();
        const int channel2 = stereo && channel >= 0 ? allocChannel() : -1;
        if (channel < 0 || (stereo && channel2 < 0))
        {
            releaseChannel(channel);
            Log(Debug::Warning) << "No free sources!";
            return false;
        }

        auto* stream = new StreamVoice;
        stream->mChannel = channel;
        stream->mChannel2 = channel2;
        stream->mIs3D = is3D;
        stream->mRate = static_cast<int>(prebuilt->mHeader.mSampleRate);
        stream->mFormat = NDSP_FORMAT_MONO_ADPCM;
        stream->mBufferBytes = static_cast<std::size_t>(prebuilt->mHeader.mBlockFrames) * CtrPack::sBytesPerFrame
            * prebuilt->mHeader.mChannels;
        stream->mMemory = linearAlloc(sStreamBuffers * stream->mBufferBytes);
        if (!stream->mMemory)
        {
            releaseChannel(channel);
            releaseChannel(channel2);
            delete stream;
            Log(Debug::Warning) << "Out of linear memory for a stream";
            return false;
        }
        stream->mDecoder = std::move(decoder);
        stream->mAdpcm = std::move(prebuilt);

        setupAdpcmChannel(channel, stream->mAdpcm->mCoefficients[0]);
        if (stereo)
            setupAdpcmChannel(channel2, stream->mAdpcm->mCoefficients[1]);

        std::lock_guard<std::mutex> lock(mStreamMutex);
        sound->mHandle = stream;
        applyParams(*stream, *sound);
        refill(*stream);
        mActiveStreams.push_back(sound);
        return true;
    }

    bool NdspOutput::startStream(DecoderPtr decoder, Stream* sound, bool is3D)
    {
        if (auto* ctr = dynamic_cast<CtrAudioDecoder*>(decoder.get()))
            if (std::shared_ptr<const PrebuiltAdpcm> prebuilt = ctr->getPrebuilt())
                return startAdpcmStream(std::move(decoder), std::move(prebuilt), sound, is3D);

        int rate = 0;
        ChannelConfig chans = ChannelConfig_Mono;
        SampleType type = SampleType_Int16;
        try
        {
            decoder->getInfo(&rate, &chans, &type);
        }
        catch (const std::exception& e)
        {
            Log(Debug::Error) << "Failed to stream " << decoder->getName() << ": " << e.what();
            return false;
        }
        if ((chans != ChannelConfig_Mono && chans != ChannelConfig_Stereo) || type == SampleType_Float32)
        {
            Log(Debug::Error) << "Unsupported stream format in " << decoder->getName();
            return false;
        }

        const int channel = allocChannel();
        if (channel < 0)
        {
            Log(Debug::Warning) << "No free sources!";
            return false;
        }

        auto* stream = new StreamVoice;
        stream->mChannel = channel;
        stream->mIs3D = is3D;
        stream->mRate = rate;
        stream->mFormat = ndspFormat(chans, type);
        stream->mFrameBytes = ndspFrameBytes(stream->mFormat);
        stream->mMemory = linearAlloc(sStreamBuffers * sStreamBufferFrames * stream->mFrameBytes);
        if (!stream->mMemory || type != SampleType_Int16)
        {
            releaseChannel(channel);
            if (stream->mMemory)
                linearFree(stream->mMemory);
            delete stream;
            Log(Debug::Warning) << "Can't stream " << decoder->getName();
            return false;
        }
        stream->mDecoder = std::move(decoder);

        ndspChnReset(channel);
        ndspChnSetInterp(channel, NDSP_INTERP_LINEAR);
        ndspChnSetFormat(channel, stream->mFormat);

        std::lock_guard<std::mutex> lock(mStreamMutex);
        sound->mHandle = stream;
        applyParams(*stream, *sound);
        refill(*stream);
        mActiveStreams.push_back(sound);
        return true;
    }

    bool NdspOutput::streamSound(DecoderPtr decoder, Stream* sound, bool)
    {
        return startStream(std::move(decoder), sound, false);
    }

    bool NdspOutput::streamSound3D(DecoderPtr decoder, Stream* sound, bool)
    {
        return startStream(std::move(decoder), sound, true);
    }

    void NdspOutput::finishStream(Stream* sound)
    {
        StreamVoice* stream = nullptr;
        {
            std::lock_guard<std::mutex> lock(mStreamMutex);
            stream = static_cast<StreamVoice*>(sound->mHandle);
            if (!stream)
                return;
            stopVoice(*stream);
            sound->mHandle = nullptr;
            mActiveStreams.erase(
                std::remove(mActiveStreams.begin(), mActiveStreams.end(), sound), mActiveStreams.end());
        }
        mPendingFree.push_back(stream->mMemory);
        if (stream->mDecoder)
            stream->mDecoder->close();
        delete stream;
    }

    double NdspOutput::getStreamDelay(Stream* sound)
    {
        auto* stream = static_cast<StreamVoice*>(sound->mHandle);
        if (!stream || stream->mRate <= 0)
            return 0.0;
        std::lock_guard<std::mutex> lock(mStreamMutex);
        const std::size_t played = stream->mFramesDone + (stream->mChannel >= 0 ? ndspChnGetSamplePos(stream->mChannel) : 0);
        const std::size_t pending = stream->mFramesQueued > played ? stream->mFramesQueued - played : 0;
        return static_cast<double>(pending) / stream->mRate;
    }

    float NdspOutput::getStreamOffset(Stream* sound)
    {
        auto* stream = static_cast<StreamVoice*>(sound->mHandle);
        if (!stream || stream->mRate <= 0)
            return 0.f;
        std::lock_guard<std::mutex> lock(mStreamMutex);
        const std::size_t played = stream->mFramesDone + (stream->mChannel >= 0 ? ndspChnGetSamplePos(stream->mChannel) : 0);
        return static_cast<float>(played) / stream->mRate;
    }

    float NdspOutput::getStreamLoudness(Stream* /*sound*/)
    {
        return 0.f;
    }

    bool NdspOutput::isStreamPlaying(Stream* sound)
    {
        auto* stream = static_cast<StreamVoice*>(sound->mHandle);
        if (!stream || stream->mChannel < 0)
            return false;
        std::lock_guard<std::mutex> lock(mStreamMutex);
        if (stream->mPaused || !stream->mEof)
            return true;
        for (const ndspWaveBuf& wave : stream->mWaves)
            if (isBusy(wave))
                return true;
        return false;
    }

    void NdspOutput::updateStream(Stream* sound)
    {
        if (auto* stream = static_cast<StreamVoice*>(sound->mHandle))
            applyParams(*stream, *sound);
    }

    void NdspOutput::finishUpdate()
    {
        for (void* ptr : mFreeNow)
            linearFree(ptr);
        mFreeNow.swap(mPendingFree);
        mPendingFree.clear();
    }

    void NdspOutput::updateListener(
        const osg::Vec3f& pos, const osg::Vec3f& atdir, const osg::Vec3f& updir, const osg::Vec3f&, Environment env)
    {
        mListenerPos = pos;
        mListenerDir = atdir;
        mListenerUp = updir;
        mListenerEnv = env;
    }

    void NdspOutput::pauseSounds(int types)
    {
        for (Sound* sound : mActiveSounds)
        {
            auto* voice = static_cast<SoundVoice*>(sound->mHandle);
            if (voice && voice->mChannel >= 0 && (types & sound->getPlayType()))
            {
                ndspChnSetPaused(voice->mChannel, true);
                voice->mPaused = true;
            }
        }
        std::lock_guard<std::mutex> lock(mStreamMutex);
        for (Stream* sound : mActiveStreams)
        {
            auto* stream = static_cast<StreamVoice*>(sound->mHandle);
            if (stream && stream->mChannel >= 0 && (types & sound->getPlayType()))
            {
                ndspChnSetPaused(stream->mChannel, true);
                if (stream->mChannel2 >= 0)
                    ndspChnSetPaused(stream->mChannel2, true);
                stream->mPaused = true;
            }
        }
    }

    void NdspOutput::resumeSounds(int types)
    {
        for (Sound* sound : mActiveSounds)
        {
            auto* voice = static_cast<SoundVoice*>(sound->mHandle);
            if (voice && voice->mChannel >= 0 && voice->mPaused && (types & sound->getPlayType()))
            {
                ndspChnSetPaused(voice->mChannel, false);
                voice->mPaused = false;
            }
        }
        std::lock_guard<std::mutex> lock(mStreamMutex);
        for (Stream* sound : mActiveStreams)
        {
            auto* stream = static_cast<StreamVoice*>(sound->mHandle);
            if (stream && stream->mChannel >= 0 && stream->mPaused && (types & sound->getPlayType()))
            {
                ndspChnSetPaused(stream->mChannel, false);
                if (stream->mChannel2 >= 0)
                    ndspChnSetPaused(stream->mChannel2, false);
                stream->mPaused = false;
            }
        }
    }

    void NdspOutput::pauseActiveDevice()
    {
        if (mDeviceInitialised)
            ndspSetMasterVol(0.f);
    }

    void NdspOutput::resumeActiveDevice()
    {
        if (mDeviceInitialised)
            ndspSetMasterVol(1.f);
    }
}
