#include "adpcmpack.hpp"

#include <algorithm>
#include <cstring>

#include <components/debug/debuglog.hpp>

namespace MWSound
{
    std::shared_ptr<const PrebuiltAdpcm> PrebuiltAdpcm::find(VFS::Path::NormalizedView path)
    {
        CtrPack::PackFile* pack = CtrPack::getSoundPack();
        CtrPack::IndexEntry entry;
        if (pack == nullptr || !pack->find(path.value(), entry))
            return nullptr;

        auto sound = std::make_shared<PrebuiltAdpcm>();
        sound->mPack = pack;
        sound->mOffset = entry.mOffset;
        CtrPack::AdpcmHeader& header = sound->mHeader;
        if (entry.mSize < sizeof(header) || !pack->read(entry.mOffset, &header, sizeof(header)))
            return nullptr;
        const std::uint32_t channels = header.mChannels;
        const std::uint32_t frames
            = (header.mNumSamples + CtrPack::sSamplesPerFrame - 1) / CtrPack::sSamplesPerFrame;
        const bool valid = std::memcmp(header.mMagic, "DSPA", 4) == 0 && (channels == 1 || channels == 2)
            && header.mSampleRate > 0 && header.mBlockFrames > 0 && header.mNumSamples > 0
            && header.mNumBlocks == (frames + header.mBlockFrames - 1) / header.mBlockFrames
            && header.mDataOffset
                == sizeof(header) + 32 * channels + sizeof(CtrPack::AdpcmContext) * header.mNumBlocks * channels
            && static_cast<std::uint64_t>(header.mDataOffset) + static_cast<std::uint64_t>(frames) * 8 * channels
                == entry.mSize;
        if (!valid)
        {
            Log(Debug::Warning) << "Bad prebuilt sound " << path;
            return nullptr;
        }
        sound->mContexts.resize(header.mNumBlocks * channels);
        if (!pack->read(entry.mOffset + sizeof(header), sound->mCoefficients.data(), 32 * channels)
            || !pack->read(entry.mOffset + sizeof(header) + 32 * channels, sound->mContexts.data(),
                sound->mContexts.size() * sizeof(CtrPack::AdpcmContext)))
            return nullptr;
        return sound;
    }

    std::uint32_t PrebuiltAdpcm::blockFrames(std::uint32_t block) const
    {
        const std::uint32_t frames = (mHeader.mNumSamples + CtrPack::sSamplesPerFrame - 1) / CtrPack::sSamplesPerFrame;
        const std::uint32_t first = block * mHeader.mBlockFrames;
        return first >= frames ? 0 : std::min<std::uint32_t>(mHeader.mBlockFrames, frames - first);
    }

    std::uint32_t PrebuiltAdpcm::blockSamples(std::uint32_t block) const
    {
        const std::uint32_t first = block * mHeader.mBlockFrames * CtrPack::sSamplesPerFrame;
        return first >= mHeader.mNumSamples
            ? 0
            : std::min<std::uint32_t>(mHeader.mBlockFrames * CtrPack::sSamplesPerFrame, mHeader.mNumSamples - first);
    }

    std::uint32_t PrebuiltAdpcm::blockOffset(std::uint32_t block) const
    {
        return mOffset + mHeader.mDataOffset
            + block * mHeader.mBlockFrames * CtrPack::sBytesPerFrame * mHeader.mChannels;
    }

    void AdpcmPackDecoder::getInfo(int* samplerate, ChannelConfig* chans, SampleType* type)
    {
        *samplerate = static_cast<int>(mSound->mHeader.mSampleRate);
        *chans = mSound->mHeader.mChannels == 2 ? ChannelConfig_Stereo : ChannelConfig_Mono;
        *type = SampleType_Int16;
    }

    bool AdpcmPackDecoder::decodeBlock()
    {
        const PrebuiltAdpcm& sound = *mSound;
        const std::uint32_t frames = sound.blockFrames(mNextBlock);
        if (frames == 0)
            return false;
        const int channels = sound.mHeader.mChannels;
        const std::uint32_t samples = sound.blockSamples(mNextBlock);
        std::vector<std::uint8_t> data(static_cast<std::size_t>(frames) * CtrPack::sBytesPerFrame * channels);
        if (!sound.mPack->read(sound.blockOffset(mNextBlock), data.data(), data.size()))
            return false;

        mPcm.assign(static_cast<std::size_t>(samples) * channels, 0);
        for (int c = 0; c < channels; ++c)
        {
            const CtrPack::AdpcmContext& context = sound.context(mNextBlock, c);
            const auto& coefficients = sound.mCoefficients[c];
            std::int32_t history0 = context.mHistory0;
            std::int32_t history1 = context.mHistory1;
            const std::uint8_t* frame = data.data() + static_cast<std::size_t>(c) * frames * CtrPack::sBytesPerFrame;
            for (std::uint32_t i = 0; i < samples; ++i)
            {
                const std::uint32_t n = i % CtrPack::sSamplesPerFrame;
                if (n == 0 && i > 0)
                    frame += CtrPack::sBytesPerFrame;
                const int predictor = frame[0] >> 4;
                const int scale = frame[0] & 0xF;
                const std::uint8_t byte = frame[1 + n / 2];
                int nibble = (n & 1) ? (byte & 0xF) : (byte >> 4);
                if (nibble >= 8)
                    nibble -= 16;
                const std::int32_t c1 = static_cast<std::int16_t>(coefficients[predictor * 2]);
                const std::int32_t c2 = static_cast<std::int16_t>(coefficients[predictor * 2 + 1]);
                const std::int32_t value
                    = ((nibble * (1 << scale)) * 2048 + 1024 + c1 * history0 + c2 * history1) >> 11;
                const std::int32_t sample = std::clamp<std::int32_t>(value, -32768, 32767);
                mPcm[static_cast<std::size_t>(i) * channels + c] = static_cast<std::int16_t>(sample);
                history1 = history0;
                history0 = sample;
            }
        }
        mPcmPos = 0;
        ++mNextBlock;
        return true;
    }

    size_t AdpcmPackDecoder::read(char* buffer, size_t bytes)
    {
        const std::size_t channels = mSound->mHeader.mChannels;
        const std::size_t frameBytes = channels * sizeof(std::int16_t);
        std::size_t wanted = bytes / frameBytes;
        std::size_t done = 0;
        while (wanted > 0)
        {
            const std::size_t available = mPcm.size() / channels - mPcmPos;
            if (available == 0)
            {
                if (!decodeBlock())
                    break;
                continue;
            }
            const std::size_t n = std::min(wanted, available);
            std::memcpy(buffer + done * frameBytes, mPcm.data() + mPcmPos * channels, n * frameBytes);
            mPcmPos += n;
            mSamplesDone += n;
            done += n;
            wanted -= n;
        }
        return done * frameBytes;
    }
}
