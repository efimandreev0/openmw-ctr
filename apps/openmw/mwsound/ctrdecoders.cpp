#include "ctrdecoders.hpp"

#include <algorithm>
#include <cstring>
#include <istream>
#include <stdexcept>

#include <mpg123.h>

#include <components/debug/debuglog.hpp>
#include <components/vfs/manager.hpp>

namespace MWSound
{
    namespace
    {
        std::uint32_t readLe32(const unsigned char* p)
        {
            return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
        }

        std::uint16_t readLe16(const unsigned char* p)
        {
            return static_cast<std::uint16_t>(p[0] | (p[1] << 8));
        }

        bool readExact(std::istream& stream, void* out, std::size_t size)
        {
            stream.read(static_cast<char*>(out), static_cast<std::streamsize>(size));
            return static_cast<std::size_t>(stream.gcount()) == size;
        }

        mpg123_ssize_t mpgRead(void* handle, void* buffer, size_t size)
        {
            auto* stream = static_cast<std::istream*>(handle);
            stream->read(static_cast<char*>(buffer), static_cast<std::streamsize>(size));
            const std::streamsize got = stream->gcount();
            if (stream->eof())
                stream->clear();
            return static_cast<mpg123_ssize_t>(got);
        }

        off_t mpgSeek(void* handle, off_t offset, int whence)
        {
            auto* stream = static_cast<std::istream*>(handle);
            stream->clear();
            std::ios_base::seekdir dir = std::ios_base::beg;
            if (whence == SEEK_CUR)
                dir = std::ios_base::cur;
            else if (whence == SEEK_END)
                dir = std::ios_base::end;
            stream->seekg(offset, dir);
            if (stream->fail())
                return -1;
            return static_cast<off_t>(stream->tellg());
        }

        bool sMpgInitialised = false;
    }


    void WavDecoder::open(VFS::Path::NormalizedView fname)
    {
        close();
        mStream = mResourceMgr->get(fname);
        mName = fname.value();

        unsigned char header[12];
        if (!readExact(*mStream, header, sizeof(header)) || std::memcmp(header, "RIFF", 4) != 0
            || std::memcmp(header + 8, "WAVE", 4) != 0)
            throw std::runtime_error("not a RIFF/WAVE file");

        bool haveFormat = false;
        while (true)
        {
            unsigned char chunk[8];
            if (!readExact(*mStream, chunk, sizeof(chunk)))
                throw std::runtime_error("WAVE file without data chunk");
            const std::uint32_t size = readLe32(chunk + 4);
            if (std::memcmp(chunk, "fmt ", 4) == 0)
            {
                unsigned char fmt[16];
                if (size < sizeof(fmt) || !readExact(*mStream, fmt, sizeof(fmt)))
                    throw std::runtime_error("bad WAVE format chunk");
                const std::uint16_t format = readLe16(fmt);
                const std::uint16_t channels = readLe16(fmt + 2);
                const std::uint16_t bits = readLe16(fmt + 14);
                if (format != 1 || (channels != 1 && channels != 2) || (bits != 8 && bits != 16))
                    throw std::runtime_error("unsupported WAVE encoding");
                mRate = static_cast<int>(readLe32(fmt + 4));
                mChannels = channels == 2 ? ChannelConfig_Stereo : ChannelConfig_Mono;
                mType = bits == 8 ? SampleType_UInt8 : SampleType_Int16;
                haveFormat = true;
                mStream->seekg(size - sizeof(fmt) + (size & 1), std::ios_base::cur);
            }
            else if (std::memcmp(chunk, "data", 4) == 0)
            {
                if (!haveFormat)
                    throw std::runtime_error("WAVE data before format");
                mDataSize = size;
                mDataRead = 0;
                return;
            }
            else
                mStream->seekg(size + (size & 1), std::ios_base::cur);
            if (mStream->fail())
                throw std::runtime_error("truncated WAVE file");
        }
    }

    void WavDecoder::close()
    {
        mStream.reset();
        mDataSize = mDataRead = 0;
    }

    void WavDecoder::getInfo(int* samplerate, ChannelConfig* chans, SampleType* type)
    {
        *samplerate = mRate;
        *chans = mChannels;
        *type = mType;
    }

    size_t WavDecoder::read(char* buffer, size_t bytes)
    {
        if (!mStream)
            return 0;
        const std::size_t frameBytes = framesToBytes(1, mChannels, mType);
        bytes = std::min<std::size_t>(bytes, mDataSize - mDataRead);
        bytes -= bytes % frameBytes;
        mStream->read(buffer, static_cast<std::streamsize>(bytes));
        std::size_t got = static_cast<std::size_t>(mStream->gcount());
        got -= got % frameBytes;
        mDataRead += static_cast<std::uint32_t>(got);
        return got;
    }

    size_t WavDecoder::getSampleOffset()
    {
        return bytesToFrames(mDataRead, mChannels, mType);
    }


    void Mp3Decoder::open(VFS::Path::NormalizedView fname)
    {
        close();
        if (!sMpgInitialised)
        {
            mpg123_init();
            sMpgInitialised = true;
        }

        mStream = mResourceMgr->get(fname);
        mName = fname.value();

        int error = MPG123_OK;
        mHandle = mpg123_new(nullptr, &error);
        if (!mHandle)
            throw std::runtime_error("mpg123_new failed");

        mpg123_param(mHandle, MPG123_FLAGS, MPG123_QUIET | MPG123_GAPLESS, 0.);
        mpg123_format_none(mHandle);
        const long* rates = nullptr;
        size_t rateCount = 0;
        mpg123_rates(&rates, &rateCount);
        for (size_t i = 0; i < rateCount; ++i)
            mpg123_format(mHandle, rates[i], MPG123_MONO | MPG123_STEREO, MPG123_ENC_SIGNED_16);

        if (mpg123_replace_reader_handle(mHandle, mpgRead, mpgSeek, nullptr) != MPG123_OK
            || mpg123_open_handle(mHandle, mStream.get()) != MPG123_OK)
        {
            close();
            throw std::runtime_error("mpg123 can't open the stream");
        }

        long rate = 0;
        int channels = 0;
        int encoding = 0;
        if (mpg123_getformat(mHandle, &rate, &channels, &encoding) != MPG123_OK)
        {
            close();
            throw std::runtime_error("mpg123 can't read the format");
        }
        mpg123_format_none(mHandle);
        mpg123_format(mHandle, rate, channels, MPG123_ENC_SIGNED_16);

        mRate = static_cast<int>(rate);
        mChannelCount = channels;
        mFramesDone = 0;
    }

    void Mp3Decoder::close()
    {
        if (mHandle)
        {
            mpg123_close(mHandle);
            mpg123_delete(mHandle);
            mHandle = nullptr;
        }
        mStream.reset();
    }

    void Mp3Decoder::getInfo(int* samplerate, ChannelConfig* chans, SampleType* type)
    {
        *samplerate = mRate;
        *chans = mChannelCount == 2 ? ChannelConfig_Stereo : ChannelConfig_Mono;
        *type = SampleType_Int16;
    }

    size_t Mp3Decoder::read(char* buffer, size_t bytes)
    {
        if (!mHandle)
            return 0;
        const std::size_t frameBytes = 2 * static_cast<std::size_t>(mChannelCount);
        bytes -= bytes % frameBytes;
        std::size_t total = 0;
        while (total < bytes)
        {
            size_t done = 0;
            const int result = mpg123_read(
                mHandle, reinterpret_cast<unsigned char*>(buffer + total), bytes - total, &done);
            total += done;
            if (result == MPG123_NEW_FORMAT)
                continue;
            if (result != MPG123_OK)
                break;
            if (done == 0)
                break;
        }
        total -= total % frameBytes;
        mFramesDone += total / frameBytes;
        return total;
    }

    size_t Mp3Decoder::getSampleOffset()
    {
        return mFramesDone;
    }


    std::shared_ptr<SoundDecoder> makeCtrFileDecoder(const VFS::Manager* resourceMgr, VFS::Path::NormalizedView fname)
    {
        const std::string_view name = fname.value();
        const bool isMp3 = name.size() > 4 && name.substr(name.size() - 4) == ".mp3";
        std::shared_ptr<SoundDecoder> decoder;
        if (isMp3)
            decoder = std::make_shared<Mp3Decoder>(resourceMgr);
        else
            decoder = std::make_shared<WavDecoder>(resourceMgr);
        try
        {
            decoder->open(fname);
        }
        catch (const std::exception&)
        {
            if (isMp3)
                decoder = std::make_shared<WavDecoder>(resourceMgr);
            else
                decoder = std::make_shared<Mp3Decoder>(resourceMgr);
            decoder->open(fname);
        }
        return decoder;
    }
}
