#ifndef GAME_SOUND_ADPCMPACK_H
#define GAME_SOUND_ADPCMPACK_H


#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include <components/ctr/gpu/packfile.hpp>

#include "sounddecoder.hpp"

namespace MWSound
{
    struct PrebuiltAdpcm
    {
        CtrPack::PackFile* mPack = nullptr;
        std::uint32_t mOffset = 0; ///< of the entry in the pack
        CtrPack::AdpcmHeader mHeader{};
        std::array<std::array<std::uint16_t, 16>, 2> mCoefficients{};
        std::vector<CtrPack::AdpcmContext> mContexts; ///< [block * channels + channel]

        static std::shared_ptr<const PrebuiltAdpcm> find(VFS::Path::NormalizedView path);

        std::uint32_t blockFrames(std::uint32_t block) const;
        std::uint32_t blockSamples(std::uint32_t block) const;
        std::uint32_t blockOffset(std::uint32_t block) const;
        const CtrPack::AdpcmContext& context(std::uint32_t block, int channel) const
        {
            return mContexts[block * mHeader.mChannels + channel];
        }
    };

    class AdpcmPackDecoder final : public SoundDecoder
    {
    public:
        AdpcmPackDecoder(const VFS::Manager* resourceMgr, std::shared_ptr<const PrebuiltAdpcm> sound)
            : SoundDecoder(resourceMgr)
            , mSound(std::move(sound))
        {
        }

        const std::shared_ptr<const PrebuiltAdpcm>& getSound() const { return mSound; }

        void open(VFS::Path::NormalizedView) override {}
        void close() override {}
        std::string getName() override { return "prebuilt ADPCM"; }
        void getInfo(int* samplerate, ChannelConfig* chans, SampleType* type) override;
        size_t read(char* buffer, size_t bytes) override;
        size_t getSampleOffset() override { return mSamplesDone; }

    private:
        bool decodeBlock();

        std::shared_ptr<const PrebuiltAdpcm> mSound;
        std::uint32_t mNextBlock = 0;
        std::vector<std::int16_t> mPcm; ///< the current block, interleaved
        std::size_t mPcmPos = 0; ///< in frames
        std::size_t mSamplesDone = 0;
    };
}

#endif
