#ifndef GAME_SOUND_NSADECODER_H
#define GAME_SOUND_NSADECODER_H

#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "sounddecoder.hpp"

namespace MWSound
{
    struct PrebuiltAdpcm;

    /* .nsa IMA4-ADPCM audio cache for the 3DS port.
     *
     * scripts/ctr/preprocess_audio_nsa.py converts every VFS audio file
     * (WAV SFX, MP3 voices/music) into IMA4 blocks laid out exactly the way
     * openal-soft's FmtIMA4 mixer reads them, named
     * cache/snd/<fnv1a(vfs_path)>.nsa.
     *
     * Consumers:
     *  - OpenALOutput::loadSound uploads the blocks DIRECTLY as an
     *    AL_FORMAT_*_IMA4 buffer (readFile) — 4x less buffer RAM than PCM16
     *    and no FFmpeg decode;
     *  - voice/music streams run through NsaDecoder (a SoundDecoder that
     *    inflates IMA4 -> PCM16 in a few integer ops per sample, replacing
     *    the FFmpeg mp3 decoder), via CtrAudioDecoder which falls back to
     *    FFmpeg when no cache entry exists.
     */

    struct NsaHeader
    {
        uint32_t mRate = 0;
        uint32_t mChannels = 0;
        uint32_t mSamplesPerBlock = 0;
        uint32_t mBlockBytes = 0; // all channels
        uint32_t mNumFrames = 0; // true length; the last block may be padded
        uint32_t mDataSize = 0;
    };

    class NsaDecoder final : public SoundDecoder
    {
        FILE* mFile = nullptr;
        NsaHeader mHeader;
        std::vector<int16_t> mBlockPcm; // one decoded block, interleaved
        size_t mBlockFrames = 0; // valid frames in mBlockPcm
        size_t mBlockPos = 0; // consumed frames in mBlockPcm
        size_t mFramesDone = 0; // total frames handed out

        bool decodeNextBlock();

    public:
        explicit NsaDecoder(const VFS::Manager* resourceMgr)
            : SoundDecoder(resourceMgr)
        {
        }
        ~NsaDecoder() override { close(); }

        /* cache/snd path for a normalized VFS path ("" when not on 3DS). */
        static std::string cachePathFor(VFS::Path::NormalizedView fname);

        /* Whole-file read for direct AL IMA4 buffer upload. */
        struct Blob
        {
            NsaHeader mHeader;
            std::vector<char> mData;
        };
        static bool readFile(const std::string& cachePath, Blob& out);

        void open(VFS::Path::NormalizedView fname) override;
        void close() override;

        std::string getName() override;
        void getInfo(int* samplerate, ChannelConfig* chans, SampleType* type) override;

        size_t read(char* buffer, size_t bytes) override;
        size_t getSampleOffset() override { return mFramesDone; }
    };

    /* getDecoder() implementation for the 3DS: serve sounds.ctp (DSP-ADPCM
     * from apps/ctrprep) or .nsa when baked, otherwise decode the file. */
    class CtrAudioDecoder final : public SoundDecoder
    {
        std::shared_ptr<SoundDecoder> mActive;

    public:
        explicit CtrAudioDecoder(const VFS::Manager* resourceMgr)
            : SoundDecoder(resourceMgr)
        {
        }

        void open(VFS::Path::NormalizedView fname) override;
        void close() override
        {
            if (mActive)
                mActive->close();
        }

        std::string getName() override { return mActive ? mActive->getName() : std::string(); }

#ifdef OPENMW_CTR_NATIVE
        std::shared_ptr<const PrebuiltAdpcm> getPrebuilt() const;
#endif
        void getInfo(int* samplerate, ChannelConfig* chans, SampleType* type) override
        {
            mActive->getInfo(samplerate, chans, type);
        }

        size_t read(char* buffer, size_t bytes) override { return mActive->read(buffer, bytes); }
        void readAll(std::vector<char>& output) override { mActive->readAll(output); }
        size_t getSampleOffset() override { return mActive ? mActive->getSampleOffset() : 0; }
    };
}

#endif
