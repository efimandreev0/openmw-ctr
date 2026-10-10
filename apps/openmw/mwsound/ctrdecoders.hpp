#ifndef GAME_SOUND_CTRDECODERS_H
#define GAME_SOUND_CTRDECODERS_H


#include <cstdint>
#include <istream>
#include <memory>
#include <string>

#include <components/files/istreamptr.hpp>

#include "sounddecoder.hpp"

typedef struct mpg123_handle_struct mpg123_handle;

namespace MWSound
{
    class WavDecoder final : public SoundDecoder
    {
    public:
        explicit WavDecoder(const VFS::Manager* resourceMgr)
            : SoundDecoder(resourceMgr)
        {
        }
        ~WavDecoder() override { close(); }

        void open(VFS::Path::NormalizedView fname) override;
        void close() override;

        std::string getName() override { return mName; }
        void getInfo(int* samplerate, ChannelConfig* chans, SampleType* type) override;

        size_t read(char* buffer, size_t bytes) override;
        size_t getSampleOffset() override;

    private:
        Files::IStreamPtr mStream;
        std::string mName;
        int mRate = 0;
        ChannelConfig mChannels = ChannelConfig_Mono;
        SampleType mType = SampleType_Int16;
        std::uint32_t mDataSize = 0;
        std::uint32_t mDataRead = 0;
    };

    class Mp3Decoder final : public SoundDecoder
    {
    public:
        explicit Mp3Decoder(const VFS::Manager* resourceMgr)
            : SoundDecoder(resourceMgr)
        {
        }
        ~Mp3Decoder() override { close(); }

        void open(VFS::Path::NormalizedView fname) override;
        void close() override;

        std::string getName() override { return mName; }
        void getInfo(int* samplerate, ChannelConfig* chans, SampleType* type) override;

        size_t read(char* buffer, size_t bytes) override;
        size_t getSampleOffset() override;

    private:
        Files::IStreamPtr mStream;
        std::string mName;
        mpg123_handle* mHandle = nullptr;
        int mRate = 0;
        int mChannelCount = 1;
        std::size_t mFramesDone = 0;
    };

    std::shared_ptr<SoundDecoder> makeCtrFileDecoder(const VFS::Manager* resourceMgr, VFS::Path::NormalizedView fname);
}

#endif
