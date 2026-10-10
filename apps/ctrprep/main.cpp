
#include "dspadpcm.hpp"

#include <components/ctr/gpu/ctrpack.hpp>
#include <components/ctr/gpu/imagedecode.hpp>
#include <components/ctr/gpu/texconvert.hpp>

#define MINIMP3_IMPLEMENTATION
#include "extern/minimp3.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace
{
    std::string utf8(const fs::path& path, bool generic = false)
    {
        const std::u8string text = generic ? path.generic_u8string() : path.u8string();
        return std::string(text.begin(), text.end());
    }


    struct Source
    {
        fs::path mFile;
        std::uint64_t mOffset = 0;
        std::uint64_t mSize = 0;
    };

    using FileMap = std::map<std::string, Source>;

    bool readBytes(const Source& source, std::vector<std::uint8_t>& out)
    {
        std::ifstream file(source.mFile, std::ios::binary);
        if (!file)
            return false;
        file.seekg(static_cast<std::streamoff>(source.mOffset));
        out.resize(source.mSize);
        file.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(source.mSize));
        return file.gcount() == static_cast<std::streamsize>(source.mSize);
    }

    std::uint32_t readU32(std::istream& in)
    {
        unsigned char b[4] = {};
        in.read(reinterpret_cast<char*>(b), 4);
        return b[0] | (b[1] << 8) | (b[2] << 16) | (static_cast<std::uint32_t>(b[3]) << 24);
    }

    bool addArchive(const fs::path& path, FileMap& files)
    {
        std::ifstream in(path, std::ios::binary);
        if (!in)
            return false;
        const std::uint32_t version = readU32(in);
        if (version != 0x100)
        {
            std::cerr << "  " << utf8(path.filename()) << ": not a Morrowind BSA, skipped\n";
            return false;
        }
        const std::uint32_t hashOffset = readU32(in);
        const std::uint32_t count = readU32(in);
        std::vector<std::uint32_t> sizes(count), offsets(count), nameOffsets(count);
        for (std::uint32_t i = 0; i < count; ++i)
        {
            sizes[i] = readU32(in);
            offsets[i] = readU32(in);
        }
        for (std::uint32_t i = 0; i < count; ++i)
            nameOffsets[i] = readU32(in);
        const std::uint64_t namesStart = 12 + count * 12ull;
        const std::uint64_t namesSize = hashOffset - count * 12ull;
        std::vector<char> names(namesSize + 1, '\0');
        in.seekg(static_cast<std::streamoff>(namesStart));
        in.read(names.data(), static_cast<std::streamsize>(namesSize));
        const std::uint64_t dataStart = 12 + hashOffset + count * 8ull;
        if (!in)
            return false;
        for (std::uint32_t i = 0; i < count; ++i)
        {
            if (nameOffsets[i] >= namesSize)
                continue;
            Source source;
            source.mFile = path;
            source.mOffset = dataStart + offsets[i];
            source.mSize = sizes[i];
            files[CtrPack::normalizePath(names.data() + nameOffsets[i])] = source;
        }
        std::cout << "  " << utf8(path.filename()) << ": " << count << " files\n";
        return true;
    }

    void addLooseFiles(const fs::path& dataDir, FileMap& files)
    {
        std::size_t count = 0;
        std::error_code ec;
        for (auto it = fs::recursive_directory_iterator(dataDir, ec); it != fs::recursive_directory_iterator();
             it.increment(ec))
        {
            if (ec || !it->is_regular_file(ec))
                continue;
            const std::string relative = utf8(fs::relative(it->path(), dataDir, ec), true);
            if (ec)
                continue;
            const std::string key = CtrPack::normalizePath(relative);
            if (!key.starts_with("textures/") && !key.starts_with("sound/") && !key.starts_with("music/"))
                continue;
            Source source;
            source.mFile = it->path();
            source.mSize = it->file_size(ec);
            files[key] = source;
            ++count;
        }
        std::cout << "  " << utf8(dataDir) << ": " << count << " loose texture and sound files\n";
    }

    std::string extensionOf(const std::string& key)
    {
        const std::size_t dot = key.rfind('.');
        return dot == std::string::npos ? std::string() : key.substr(dot + 1);
    }


    class PackWriter
    {
    public:
        PackWriter(const fs::path& path, const char (&magic)[8], std::uint32_t maxSize)
            : mFile(path, std::ios::binary | std::ios::trunc)
            , mPath(path)
        {
            std::memcpy(mHeader.mMagic, magic, 8);
            mHeader.mVersion = CtrPack::sVersion;
            mHeader.mMaxSize = maxSize;
            mFile.write(reinterpret_cast<const char*>(&mHeader), sizeof(mHeader));
        }

        bool ok() const { return static_cast<bool>(mFile); }

        void add(const std::string& key, const std::vector<std::uint8_t>& data)
        {
            const std::uint64_t hash = CtrPack::hashPath(key);
            if (!mKeys.emplace(hash, key).second)
            {
                std::cerr << "  hash collision, skipped: " << key << "\n";
                return;
            }
            CtrPack::IndexEntry entry;
            entry.mHash = hash;
            entry.mOffset = static_cast<std::uint32_t>(mFile.tellp());
            entry.mSize = static_cast<std::uint32_t>(data.size());
            mFile.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
            mIndex.push_back(entry);
        }

        std::uint64_t finish()
        {
            std::sort(mIndex.begin(), mIndex.end(),
                [](const CtrPack::IndexEntry& a, const CtrPack::IndexEntry& b) { return a.mHash < b.mHash; });
            mHeader.mCount = static_cast<std::uint32_t>(mIndex.size());
            mHeader.mIndexOffset = static_cast<std::uint32_t>(mFile.tellp());
            mFile.write(reinterpret_cast<const char*>(mIndex.data()),
                static_cast<std::streamsize>(mIndex.size() * sizeof(CtrPack::IndexEntry)));
            const std::uint64_t size = static_cast<std::uint64_t>(mFile.tellp());
            mFile.seekp(0);
            mFile.write(reinterpret_cast<const char*>(&mHeader), sizeof(mHeader));
            mFile.close();
            return size;
        }

    private:
        std::ofstream mFile;
        fs::path mPath;
        CtrPack::PackHeader mHeader{};
        std::vector<CtrPack::IndexEntry> mIndex;
        std::map<std::uint64_t, std::string> mKeys;
    };

    std::mutex sOutputMutex;

    template <class Convert>
    std::size_t runJobs(const std::vector<std::string>& keys, unsigned threads, std::size_t chunk, PackWriter& pack,
        const char* label, Convert convert)
    {
        std::size_t written = 0;
        const auto start = std::chrono::steady_clock::now();
        for (std::size_t base = 0; base < keys.size(); base += chunk)
        {
            const std::size_t end = std::min(keys.size(), base + chunk);
            std::vector<std::optional<std::vector<std::uint8_t>>> results(end - base);
            std::atomic<std::size_t> next{ base };
            std::vector<std::thread> pool;
            for (unsigned t = 0; t < threads; ++t)
                pool.emplace_back([&] {
                    for (std::size_t i = next++; i < end; i = next++)
                    {
                        try
                        {
                            results[i - base] = convert(keys[i]);
                        }
                        catch (const std::exception& e)
                        {
                            const std::lock_guard<std::mutex> lock(sOutputMutex);
                            std::cerr << "\n  " << keys[i] << ": " << e.what() << "\n";
                        }
                    }
                });
            for (std::thread& thread : pool)
                thread.join();
            for (std::size_t i = base; i < end; ++i)
            {
                if (results[i - base])
                {
                    pack.add(keys[i], *results[i - base]);
                    ++written;
                }
            }
            const double seconds
                = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
            std::cout << "\r  " << label << ": " << end << "/" << keys.size() << " (" << static_cast<int>(seconds)
                      << " s)" << std::flush;
        }
        std::cout << "\n";
        return written;
    }


    void put16(std::vector<std::uint8_t>& out, std::uint16_t v)
    {
        out.push_back(static_cast<std::uint8_t>(v & 0xFF));
        out.push_back(static_cast<std::uint8_t>(v >> 8));
    }

    int log2Of(int v)
    {
        int l = 0;
        while ((1 << l) < v)
            ++l;
        return l;
    }

    std::optional<std::vector<std::uint8_t>> convertTexture(const FileMap& files, const std::string& key, int maxSize)
    {
        std::vector<std::uint8_t> bytes;
        if (!readBytes(files.at(key), bytes))
            return std::nullopt;
        CtrGpu::RgbaImage image;
        if (!CtrGpu::decodeImage(bytes.data(), bytes.size(), extensionOf(key), maxSize, image) || image.mWidth <= 0
            || image.mHeight <= 0)
            return std::nullopt;
        const CtrGpu::TextureFormat format
            = CtrGpu::chooseFormat(image.mPixels.data(), image.mWidth, image.mHeight);
        const CtrGpu::TextureLayout layout = CtrGpu::computeTextureLayout(image.mWidth, image.mHeight, maxSize, true);
        std::vector<std::uint8_t> scaled;
        const std::uint8_t* source = image.mPixels.data();
        if (layout.mWidth != image.mWidth || layout.mHeight != image.mHeight)
        {
            scaled = CtrGpu::scaleImage(image.mPixels.data(), image.mWidth, image.mHeight, layout.mWidth, layout.mHeight);
            source = scaled.data();
        }
        const std::size_t size = CtrGpu::tiledTextureSize(layout.mWidth, layout.mHeight, layout.mMipLevels, format);

        std::vector<std::uint8_t> out;
        out.reserve(21 + size);
        put16(out, 1); // subtextures
        out.push_back(static_cast<std::uint8_t>((log2Of(layout.mWidth) - 3) | ((log2Of(layout.mHeight) - 3) << 3)));
        out.push_back(CtrGpu::toGpuColorFormat(format));
        out.push_back(static_cast<std::uint8_t>(layout.mMipLevels));
        put16(out, static_cast<std::uint16_t>(layout.mWidth));
        put16(out, static_cast<std::uint16_t>(layout.mHeight));
        put16(out, 0); // left
        put16(out, 1024); // top
        put16(out, 1024); // right
        put16(out, 0); // bottom
        out.push_back(0);
        out.push_back(static_cast<std::uint8_t>(size & 0xFF));
        out.push_back(static_cast<std::uint8_t>((size >> 8) & 0xFF));
        out.push_back(static_cast<std::uint8_t>((size >> 16) & 0xFF));
        const std::size_t dataStart = out.size();
        out.resize(dataStart + size);
        CtrGpu::buildTiledTexture(source, layout.mWidth, layout.mHeight, layout.mMipLevels, format, out.data() + dataStart);
        return out;
    }


    struct Pcm
    {
        int mRate = 0;
        int mChannels = 0;
        std::vector<std::int16_t> mSamples; ///< interleaved
    };

    bool decodeWav(const std::vector<std::uint8_t>& data, Pcm& out)
    {
        if (data.size() < 12 || std::memcmp(data.data(), "RIFF", 4) != 0 || std::memcmp(data.data() + 8, "WAVE", 4) != 0)
            return false;
        int format = 0, channels = 0, rate = 0, bits = 0;
        std::size_t pos = 12;
        auto u16 = [&](std::size_t p) { return data[p] | (data[p + 1] << 8); };
        auto u32 = [&](std::size_t p) {
            return static_cast<std::uint32_t>(data[p] | (data[p + 1] << 8) | (data[p + 2] << 16))
                | (static_cast<std::uint32_t>(data[p + 3]) << 24);
        };
        while (pos + 8 <= data.size())
        {
            const std::uint32_t size = u32(pos + 4);
            const std::size_t body = pos + 8;
            const std::size_t available = std::min<std::size_t>(size, data.size() - body);
            if (std::memcmp(data.data() + pos, "fmt ", 4) == 0 && available >= 16)
            {
                format = u16(body);
                channels = u16(body + 2);
                rate = static_cast<int>(u32(body + 4));
                bits = u16(body + 14);
                if (format == 0xFFFE && available >= 26)
                    format = u16(body + 24); // WAVE_FORMAT_EXTENSIBLE: the subformat GUID starts with the tag
            }
            else if (std::memcmp(data.data() + pos, "data", 4) == 0)
            {
                if (format != 1 || channels < 1 || channels > 2 || rate <= 0 || (bits != 8 && bits != 16 && bits != 24))
                    return false;
                const int sampleBytes = bits / 8;
                const std::size_t count = available / sampleBytes;
                out.mRate = rate;
                out.mChannels = channels;
                out.mSamples.resize(count - count % channels);
                for (std::size_t i = 0; i < out.mSamples.size(); ++i)
                {
                    const std::uint8_t* p = data.data() + body + i * sampleBytes;
                    if (bits == 8)
                        out.mSamples[i] = static_cast<std::int16_t>((p[0] - 128) << 8);
                    else if (bits == 16)
                        out.mSamples[i] = static_cast<std::int16_t>(p[0] | (p[1] << 8));
                    else
                        out.mSamples[i] = static_cast<std::int16_t>(p[1] | (p[2] << 8));
                }
                return !out.mSamples.empty();
            }
            pos = body + size + (size & 1);
        }
        return false;
    }

    bool readInfoFrame(const std::uint8_t* frame, std::size_t size, std::size_t& skip, std::size_t& padding)
    {
        for (std::size_t i = 4; i + 8 <= std::min<std::size_t>(size, 48); ++i)
        {
            if (std::memcmp(frame + i, "Xing", 4) != 0 && std::memcmp(frame + i, "Info", 4) != 0)
                continue;
            const std::uint32_t flags = frame[i + 7];
            const std::size_t lame = i + 8 + ((flags & 1) ? 4 : 0) + ((flags & 2) ? 4 : 0) + ((flags & 4) ? 100 : 0)
                + ((flags & 8) ? 4 : 0);
            if (lame + 24 <= size
                && (std::memcmp(frame + lame, "LAME", 4) == 0 || std::memcmp(frame + lame, "Lavc", 4) == 0
                    || std::memcmp(frame + lame, "Lavf", 4) == 0))
            {
                const std::uint8_t* d = frame + lame + 21;
                skip = static_cast<std::size_t>((d[0] << 4) | (d[1] >> 4)) + 529;
                const std::size_t pad = static_cast<std::size_t>(((d[1] & 0xF) << 8) | d[2]);
                padding = pad > 529 ? pad - 529 : 0;
            }
            return true;
        }
        return false;
    }

    bool decodeMp3(const std::vector<std::uint8_t>& data, Pcm& out)
    {
        std::size_t pos = 0;
        if (data.size() > 10 && std::memcmp(data.data(), "ID3", 3) == 0)
            pos = 10 + ((data[6] & 0x7F) << 21 | (data[7] & 0x7F) << 14 | (data[8] & 0x7F) << 7 | (data[9] & 0x7F));
        mp3dec_t decoder;
        mp3dec_init(&decoder);
        std::vector<mp3d_sample_t> frame(MINIMP3_MAX_SAMPLES_PER_FRAME);
        std::size_t skip = 0;
        std::size_t padding = 0;
        bool first = true;
        while (pos < data.size())
        {
            mp3dec_frame_info_t info{};
            const int samples = mp3dec_decode_frame(&decoder, data.data() + pos,
                static_cast<int>(std::min<std::size_t>(data.size() - pos, 1 << 20)), frame.data(), &info);
            if (info.frame_bytes == 0)
                break;
            const std::uint8_t* frameData = data.data() + pos + info.frame_offset;
            const std::size_t frameSize = static_cast<std::size_t>(info.frame_bytes - info.frame_offset);
            pos += info.frame_bytes;
            if (samples == 0)
                continue;
            if (first)
            {
                first = false;
                if (readInfoFrame(frameData, frameSize, skip, padding))
                    continue;
            }
            if (out.mChannels == 0)
            {
                out.mChannels = info.channels;
                out.mRate = info.hz;
            }
            if (info.channels == out.mChannels)
                out.mSamples.insert(out.mSamples.end(), frame.begin(), frame.begin() + samples * info.channels);
        }
        if (out.mChannels < 1 || out.mChannels > 2)
            return false;
        const std::size_t frames = out.mSamples.size() / out.mChannels;
        if (skip + padding < frames)
        {
            out.mSamples.resize((frames - padding) * out.mChannels);
            out.mSamples.erase(out.mSamples.begin(), out.mSamples.begin() + skip * out.mChannels);
        }
        return !out.mSamples.empty();
    }

    void resample(Pcm& pcm, int rate)
    {
        if (rate <= 0 || rate >= pcm.mRate)
            return;
        const std::size_t frames = pcm.mSamples.size() / pcm.mChannels;
        const std::size_t outFrames = static_cast<std::size_t>(static_cast<double>(frames) * rate / pcm.mRate);
        std::vector<std::int16_t> out(outFrames * pcm.mChannels);
        const double step = static_cast<double>(pcm.mRate) / rate;
        for (std::size_t i = 0; i < outFrames; ++i)
        {
            const double position = i * step;
            const std::size_t i0 = std::min(static_cast<std::size_t>(position), frames - 1);
            const std::size_t i1 = std::min(i0 + 1, frames - 1);
            const double t = position - i0;
            for (int c = 0; c < pcm.mChannels; ++c)
                out[i * pcm.mChannels + c] = static_cast<std::int16_t>(std::lround(
                    pcm.mSamples[i0 * pcm.mChannels + c] * (1 - t) + pcm.mSamples[i1 * pcm.mChannels + c] * t));
        }
        pcm.mSamples.swap(out);
        pcm.mRate = rate;
    }

    template <class T>
    void append(std::vector<std::uint8_t>& out, const T& value)
    {
        const auto* p = reinterpret_cast<const std::uint8_t*>(&value);
        out.insert(out.end(), p, p + sizeof(T));
    }

    std::optional<std::vector<std::uint8_t>> convertSound(const FileMap& files, const std::string& key, int musicRate)
    {
        std::vector<std::uint8_t> bytes;
        if (!readBytes(files.at(key), bytes))
            return std::nullopt;
        Pcm pcm;
        const std::string ext = extensionOf(key);
        const bool ok = ext == "mp3" ? decodeMp3(bytes, pcm) : decodeWav(bytes, pcm);
        if (!ok)
            return std::nullopt;
        if (key.starts_with("music/"))
            resample(pcm, musicRate);

        const std::size_t samples = pcm.mSamples.size() / pcm.mChannels;
        std::vector<CtrPrep::AdpcmChannel> channels;
        std::vector<std::int16_t> mono(samples);
        for (int c = 0; c < pcm.mChannels; ++c)
        {
            for (std::size_t i = 0; i < samples; ++i)
                mono[i] = pcm.mSamples[i * pcm.mChannels + c];
            channels.push_back(CtrPrep::encodeAdpcm(mono.data(), samples));
        }

        constexpr std::uint16_t blockFrames = 1024;
        const std::size_t frames = (samples + CtrPack::sSamplesPerFrame - 1) / CtrPack::sSamplesPerFrame;
        const std::uint32_t blocks = static_cast<std::uint32_t>((frames + blockFrames - 1) / blockFrames);

        CtrPack::AdpcmHeader header{};
        std::memcpy(header.mMagic, "DSPA", 4);
        header.mSampleRate = static_cast<std::uint32_t>(pcm.mRate);
        header.mNumSamples = static_cast<std::uint32_t>(samples);
        header.mChannels = static_cast<std::uint16_t>(pcm.mChannels);
        header.mBlockFrames = blockFrames;
        header.mNumBlocks = blocks;
        header.mDataOffset = static_cast<std::uint32_t>(
            sizeof(header) + 32 * pcm.mChannels + sizeof(CtrPack::AdpcmContext) * blocks * pcm.mChannels);

        std::vector<std::uint8_t> out;
        out.reserve(header.mDataOffset + frames * 8 * pcm.mChannels);
        append(out, header);
        for (const CtrPrep::AdpcmChannel& channel : channels)
            for (std::int16_t coefficient : channel.mCoefficients)
                append(out, coefficient);
        for (std::uint32_t b = 0; b < blocks; ++b)
        {
            for (const CtrPrep::AdpcmChannel& channel : channels)
            {
                const std::size_t frame = static_cast<std::size_t>(b) * blockFrames;
                CtrPack::AdpcmContext context{};
                context.mPredScale = channel.mFrames[frame * 8];
                context.mHistory0 = channel.mHistory0[frame];
                context.mHistory1 = channel.mHistory1[frame];
                append(out, context);
            }
        }
        for (std::uint32_t b = 0; b < blocks; ++b)
        {
            const std::size_t first = static_cast<std::size_t>(b) * blockFrames;
            const std::size_t count = std::min<std::size_t>(blockFrames, frames - first);
            for (const CtrPrep::AdpcmChannel& channel : channels)
                out.insert(out.end(), channel.mFrames.begin() + first * 8, channel.mFrames.begin() + (first + count) * 8);
        }
        return out;
    }


    struct Options
    {
        std::vector<fs::path> mDataDirs;
        std::vector<fs::path> mArchives;
        fs::path mOut;
        int mTextureSize = 128;
        int mMusicRate = 0;
        bool mTextures = true;
        bool mSounds = true;
        unsigned mThreads = 0;
    };

    void printUsage()
    {
        std::cout << "ctrprep - prebuilds Morrowind textures and sounds for the OpenMW 3DS port\n\n"
                     "  ctrprep [--data <Data Files>]... [--archive <name.bsa>]... [--out <folder>]\n"
                     "          [--texture-size 128] [--no-textures] [--no-sounds] [--music-rate <Hz>]\n"
                     "          [--threads <n>]\n\n"
                     "Without --data, \"Data Files\" next to the program or in the current folder is used.\n"
                     "Without --archive, the folder's BSAs are read: Morrowind, Tribunal, Bloodmoon, then the others\n"
                     "by name (later archives and then loose files override earlier ones, as in the game).\n"
                     "Copy textures.ctp and sounds.ctp from the output folder to sdmc:/3ds/openmw/prebuilt/.\n";
    }

    std::vector<fs::path> defaultArchives(const fs::path& dataDir)
    {
        std::vector<fs::path> found;
        std::error_code ec;
        for (const auto& entry : fs::directory_iterator(dataDir, ec))
        {
            std::string ext = utf8(entry.path().extension());
            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
            if (entry.is_regular_file(ec) && ext == ".bsa")
                found.push_back(entry.path());
        }
        auto rank = [](const fs::path& p) {
            std::string name = utf8(p.filename());
            std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return std::tolower(c); });
            if (name == "morrowind.bsa")
                return 0;
            if (name == "tribunal.bsa")
                return 1;
            if (name == "bloodmoon.bsa")
                return 2;
            return 3;
        };
        std::sort(found.begin(), found.end(), [&](const fs::path& a, const fs::path& b) {
            const int ra = rank(a), rb = rank(b);
            if (ra != rb)
                return ra < rb;
            return utf8(a.filename()) < utf8(b.filename());
        });
        return found;
    }

    int run(Options options, const fs::path& self)
    {
        if (options.mDataDirs.empty())
        {
            for (const fs::path& candidate : { self.parent_path() / "Data Files", fs::current_path() / "Data Files" })
            {
                std::error_code ec;
                if (fs::is_directory(candidate, ec))
                {
                    options.mDataDirs.push_back(candidate);
                    break;
                }
            }
        }
        if (options.mDataDirs.empty())
        {
            printUsage();
            std::cerr << "\nNo \"Data Files\" folder found: pass it with --data.\n";
            return 1;
        }
        if (options.mOut.empty())
            options.mOut = fs::current_path() / "3ds_prebuilt";
        if (options.mThreads == 0)
            options.mThreads = std::max(1u, std::thread::hardware_concurrency());
        std::error_code ec;
        fs::create_directories(options.mOut, ec);

        std::cout << "Reading the game data\n";
        FileMap files;
        for (const fs::path& dataDir : options.mDataDirs)
        {
            std::vector<fs::path> archives;
            if (options.mArchives.empty())
                archives = defaultArchives(dataDir);
            else
                for (const fs::path& name : options.mArchives)
                    if (fs::exists(dataDir / name, ec))
                        archives.push_back(dataDir / name);
            for (const fs::path& archive : archives)
                addArchive(archive, files);
        }
        for (const fs::path& dataDir : options.mDataDirs)
            addLooseFiles(dataDir, files);

        std::vector<std::string> textures, sounds;
        for (const auto& [key, source] : files)
        {
            const std::string ext = extensionOf(key);
            if (key.starts_with("textures/") && (ext == "dds" || ext == "tga" || ext == "bmp"))
                textures.push_back(key);
            else if ((key.starts_with("sound/") || key.starts_with("music/")) && (ext == "wav" || ext == "mp3"))
                sounds.push_back(key);
        }

        if (options.mTextures)
        {
            std::cout << "Textures (" << options.mTextureSize << " px) -> textures.ctp\n";
            PackWriter pack(options.mOut / "textures.ctp", CtrPack::sTextureMagic,
                static_cast<std::uint32_t>(options.mTextureSize));
            if (!pack.ok())
            {
                std::cerr << "Cannot write " << utf8(options.mOut / "textures.ctp") << "\n";
                return 1;
            }
            const std::size_t done = runJobs(textures, options.mThreads, 256, pack, "textures",
                [&](const std::string& key) { return convertTexture(files, key, options.mTextureSize); });
            const std::uint64_t size = pack.finish();
            std::cout << "  " << done << " of " << textures.size() << " converted, " << size / (1024 * 1024)
                      << " MB\n";
        }
        if (options.mSounds)
        {
            std::cout << "Sounds -> sounds.ctp (DSP-ADPCM)\n";
            PackWriter pack(options.mOut / "sounds.ctp", CtrPack::sSoundMagic, 0);
            if (!pack.ok())
            {
                std::cerr << "Cannot write " << utf8(options.mOut / "sounds.ctp") << "\n";
                return 1;
            }
            const std::size_t done = runJobs(sounds, options.mThreads, 64, pack, "sounds",
                [&](const std::string& key) { return convertSound(files, key, options.mMusicRate); });
            const std::uint64_t size = pack.finish();
            std::cout << "  " << done << " of " << sounds.size() << " converted, " << size / (1024 * 1024) << " MB\n";
        }
        std::cout << "\nDone. Copy the .ctp files from\n  " << utf8(options.mOut)
                  << "\nto sdmc:/3ds/openmw/prebuilt/ on the SD card.\n";
        return 0;
    }

    template <class Char>
    int parseAndRun(int argc, Char** argv)
    {
        Options options;
        auto arg = [&](int i) { return fs::path(argv[i]); };
        for (int i = 1; i < argc; ++i)
        {
            const std::string a = utf8(arg(i));
            auto value = [&]() -> fs::path {
                if (i + 1 >= argc)
                {
                    std::cerr << a << " needs a value\n";
                    std::exit(1);
                }
                return arg(++i);
            };
            auto number = [&](const fs::path& text) {
                const std::string digits = utf8(text);
                if (digits.empty() || digits.find_first_not_of("0123456789") != std::string::npos)
                {
                    std::cerr << a << " needs a number, not " << digits << "\n";
                    std::exit(1);
                }
                return std::stoi(digits);
            };
            if (a == "--data")
                options.mDataDirs.push_back(value());
            else if (a == "--archive")
                options.mArchives.push_back(value());
            else if (a == "--out")
                options.mOut = value();
            else if (a == "--texture-size")
                options.mTextureSize = number(value());
            else if (a == "--music-rate")
                options.mMusicRate = number(value());
            else if (a == "--threads")
                options.mThreads = static_cast<unsigned>(number(value()));
            else if (a == "--no-textures")
                options.mTextures = false;
            else if (a == "--no-sounds")
                options.mSounds = false;
            else if (a == "--help" || a == "-h" || a == "/?")
            {
                printUsage();
                return 0;
            }
            else
            {
                std::cerr << "Unknown option " << a << "\n\n";
                printUsage();
                return 1;
            }
        }
        int result = 1;
        try
        {
            result = run(std::move(options), argc > 0 ? arg(0) : fs::path());
        }
        catch (const std::exception& e)
        {
            std::cerr << "\nError: " << e.what() << "\n";
        }
#ifdef _WIN32
        if (argc <= 1)
        {
            std::cout << "\nPress Enter to close.";
            std::cin.get();
        }
#endif
        return result;
    }
}

#ifdef _WIN32
int wmain(int argc, wchar_t** argv)
{
    SetConsoleOutputCP(CP_UTF8);
    return parseAndRun(argc, argv);
}
#else
int main(int argc, char** argv)
{
    return parseAndRun(argc, argv);
}
#endif
