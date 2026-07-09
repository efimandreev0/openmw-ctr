#include "nsadecoder.hpp"

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <string_view>

#include "ffmpegdecoder.hpp"

namespace MWSound
{
    namespace
    {
        constexpr uint32_t sNsaMagic = 0x3141534E; // 'NSA1'
        constexpr uint32_t sNsaVersion = 1;

        /* Identical tables to openal-soft's core/voice.cpp — the .nsa bake is
         * validated against that decoder, this one must match it bit for bit
         * in the index sequence (sample values may differ by rounding). */
        constexpr int sStepTable[89] = { 7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
            50, 55, 60, 66, 73, 80, 88, 97, 105, 115, 127, 139, 153, 168, 185, 202, 222, 244, 269, 296, 327, 362, 400,
            441, 484, 529, 584, 646, 713, 788, 876, 967, 1060, 1166, 1282, 1408, 1552, 1707, 1876, 2066, 2272, 2499,
            2751, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635,
            13899, 15289, 16818, 18500, 20350, 22358, 24633, 27086, 29794, 32767 };
        constexpr int sIndexAdjust[16] = { -1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8 };

        uint32_t fnv1a(std::string_view s)
        {
            uint32_t h = 2166136261u;
            for (const char c : s)
            {
                h ^= static_cast<unsigned char>(c);
                h *= 16777619u;
            }
            return h;
        }

        /* Decode one channel of one IMA4 block into an interleaved output.
         * Layout (mirrors LoadSamples<IMA4Data>): per-channel 4-byte headers
         * first (s16 first sample, s16 step index), then nibble data in
         * per-channel 4-byte groups, low nibble first. */
        void decodeBlockChannel(const unsigned char* block, const uint32_t channels, const uint32_t chan,
            const uint32_t samplesPerBlock, int16_t* out)
        {
            int sample = static_cast<int16_t>(block[chan * 4] | (block[chan * 4 + 1] << 8));
            int index = static_cast<int16_t>(block[chan * 4 + 2] | (block[chan * 4 + 3] << 8));
            index = std::clamp(index, 0, 88);

            const unsigned char* nibbles = block + channels * 4 + chan * 4;
            out[0] = static_cast<int16_t>(sample);

            for (uint32_t n = 0; n < samplesPerBlock - 1; ++n)
            {
                const uint32_t byteShift = (n & 1) * 4;
                const uint32_t wordOffset = (n >> 1) & ~3u;
                const uint32_t byteOffset = wordOffset * channels + ((n >> 1) & 3);

                const int code = (nibbles[byteOffset] >> byteShift) & 0xF;
                const int diff = (2 * (code & 7) + 1) * sStepTable[index] / 8;
                sample += (code & 8) ? -diff : diff;
                sample = std::clamp(sample, -32768, 32767);
                index = std::clamp(index + sIndexAdjust[code], 0, 88);

                out[(n + 1) * channels] = static_cast<int16_t>(sample);
            }
        }

        bool readHeader(FILE* f, NsaHeader& hdr)
        {
            uint32_t raw[8];
            if (std::fread(raw, sizeof(raw), 1, f) != 1)
                return false;
            if (raw[0] != sNsaMagic || raw[1] != sNsaVersion)
                return false;
            hdr.mRate = raw[2];
            hdr.mChannels = raw[3];
            hdr.mSamplesPerBlock = raw[4];
            hdr.mBlockBytes = raw[5];
            hdr.mNumFrames = raw[6];
            hdr.mDataSize = raw[7];
            if (hdr.mChannels < 1 || hdr.mChannels > 2 || hdr.mSamplesPerBlock < 2 || hdr.mNumFrames == 0)
                return false;
            if (((hdr.mSamplesPerBlock - 1) & 7) != 0)
                return false;
            if (hdr.mBlockBytes != (4 + (hdr.mSamplesPerBlock - 1) / 2) * hdr.mChannels)
                return false;
            if (hdr.mRate < 4000 || hdr.mRate > 48000)
                return false;
            return true;
        }
    }

    std::string NsaDecoder::cachePathFor(VFS::Path::NormalizedView fname)
    {
#ifdef __3DS__
        char buf[96];
        std::snprintf(buf, sizeof(buf), "sdmc:/3ds/openmw/cache/snd/%08x.nsa", fnv1a(fname.value()));
        return buf;
#else
        (void)fname;
        return {};
#endif
    }

    bool NsaDecoder::readFile(const std::string& cachePath, Blob& out)
    {
        if (cachePath.empty())
            return false;
        FILE* f = std::fopen(cachePath.c_str(), "rb");
        if (!f)
            return false;
        if (!readHeader(f, out.mHeader))
        {
            std::fclose(f);
            return false;
        }
        out.mData.resize(out.mHeader.mDataSize);
        const size_t got = std::fread(out.mData.data(), 1, out.mData.size(), f);
        std::fclose(f);
        return got == out.mData.size() && !out.mData.empty();
    }

    void NsaDecoder::open(VFS::Path::NormalizedView fname)
    {
        close();
        const std::string path = cachePathFor(fname);
        if (path.empty())
            throw std::runtime_error("NSA cache not available");
        mFile = std::fopen(path.c_str(), "rb");
        if (!mFile)
            throw std::runtime_error("No NSA cache entry for " + std::string(fname.value()));
        if (!readHeader(mFile, mHeader))
        {
            std::fclose(mFile);
            mFile = nullptr;
            throw std::runtime_error("Bad NSA header for " + std::string(fname.value()));
        }
        setvbuf(mFile, nullptr, _IOFBF, 32 * 1024);
        mBlockPcm.resize(size_t{ mHeader.mSamplesPerBlock } * mHeader.mChannels);
        mBlockFrames = mBlockPos = 0;
        mFramesDone = 0;
    }

    void NsaDecoder::close()
    {
        if (mFile)
            std::fclose(mFile);
        mFile = nullptr;
        mBlockPcm.clear();
        mBlockFrames = mBlockPos = 0;
        mFramesDone = 0;
    }

    std::string NsaDecoder::getName()
    {
        return "nsa";
    }

    void NsaDecoder::getInfo(int* samplerate, ChannelConfig* chans, SampleType* type)
    {
        *samplerate = static_cast<int>(mHeader.mRate);
        *chans = (mHeader.mChannels == 2) ? ChannelConfig_Stereo : ChannelConfig_Mono;
        *type = SampleType_Int16;
    }

    bool NsaDecoder::decodeNextBlock()
    {
        if (mFramesDone >= mHeader.mNumFrames)
            return false;

        unsigned char block[144]; // spb=65 stereo = 72 bytes; headroom for bigger blocks
        if (mHeader.mBlockBytes > sizeof(block))
            return false;
        if (std::fread(block, mHeader.mBlockBytes, 1, mFile) != 1)
            return false;

        for (uint32_t c = 0; c < mHeader.mChannels; ++c)
            decodeBlockChannel(block, mHeader.mChannels, c, mHeader.mSamplesPerBlock, mBlockPcm.data() + c);

        /* The final block may carry encoder padding past the true length. */
        mBlockFrames = std::min<size_t>(mHeader.mSamplesPerBlock, mHeader.mNumFrames - mFramesDone);
        mBlockPos = 0;
        return true;
    }

    size_t NsaDecoder::read(char* buffer, size_t bytes)
    {
        if (!mFile)
            return 0;
        const size_t frameBytes = sizeof(int16_t) * mHeader.mChannels;
        size_t wanted = bytes / frameBytes;
        size_t done = 0;
        while (wanted > 0)
        {
            if (mBlockPos >= mBlockFrames)
            {
                if (!decodeNextBlock())
                    break;
            }
            const size_t n = std::min(wanted, mBlockFrames - mBlockPos);
            std::memcpy(buffer + done * frameBytes, mBlockPcm.data() + mBlockPos * mHeader.mChannels, n * frameBytes);
            mBlockPos += n;
            mFramesDone += n;
            done += n;
            wanted -= n;
        }
        return done * frameBytes;
    }

    void CtrAudioDecoder::open(VFS::Path::NormalizedView fname)
    {
        auto nsa = std::make_shared<NsaDecoder>(mResourceMgr);
        try
        {
            nsa->open(fname);
            mActive = std::move(nsa);
            return;
        }
        catch (const std::exception&)
        {
            // no cache entry (or a bad one) — use the stock FFmpeg path
        }
        // Through the base pointer: FFmpegDecoder declares its overrides
        // private (OpenMW style), access is via the SoundDecoder interface.
        std::shared_ptr<SoundDecoder> ffmpeg = std::make_shared<FFmpegDecoder>(mResourceMgr);
        ffmpeg->open(fname);
        mActive = std::move(ffmpeg);
    }
}
