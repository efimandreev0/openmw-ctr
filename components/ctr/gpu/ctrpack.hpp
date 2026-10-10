#ifndef OPENMW_COMPONENTS_CTR_GPU_CTRPACK_H
#define OPENMW_COMPONENTS_CTR_GPU_CTRPACK_H


#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace CtrPack
{
    constexpr char sTextureMagic[8] = { 'O', 'M', 'W', 'T', 'P', 'A', 'K', '1' };
    constexpr char sSoundMagic[8] = { 'O', 'M', 'W', 'S', 'P', 'A', 'K', '1' };
    constexpr std::uint32_t sVersion = 1;

    struct PackHeader
    {
        char mMagic[8];
        std::uint32_t mVersion;
        std::uint32_t mCount;
        std::uint32_t mIndexOffset;
        std::uint32_t mMaxSize;
        std::uint32_t mReserved[2];
    };
    static_assert(sizeof(PackHeader) == 32);

    struct IndexEntry
    {
        std::uint64_t mHash;
        std::uint32_t mOffset;
        std::uint32_t mSize;
    };
    static_assert(sizeof(IndexEntry) == 16);

    inline char normalizeChar(char c)
    {
        if (c == '\\')
            return '/';
        if (c >= 'A' && c <= 'Z')
            return static_cast<char>(c - 'A' + 'a');
        return c;
    }

    inline std::string normalizePath(std::string_view path)
    {
        std::string out;
        out.reserve(path.size());
        for (char c : path)
        {
            c = normalizeChar(c);
            if (c == '/' && (out.empty() || out.back() == '/'))
                continue;
            out.push_back(c);
        }
        return out;
    }

    inline std::uint64_t hashPath(std::string_view normalized)
    {
        std::uint64_t hash = 0xcbf29ce484222325ull;
        for (char c : normalized)
        {
            hash ^= static_cast<unsigned char>(c);
            hash *= 0x100000001b3ull;
        }
        return hash;
    }

    struct AdpcmHeader
    {
        char mMagic[4]; ///< "DSPA"
        std::uint32_t mSampleRate;
        std::uint32_t mNumSamples; ///< per channel
        std::uint16_t mChannels; ///< 1 or 2
        std::uint16_t mBlockFrames;
        std::uint32_t mNumBlocks;
        std::uint32_t mDataOffset; ///< from the start of the entry
        std::uint32_t mReserved[2];
    };
    static_assert(sizeof(AdpcmHeader) == 32);

    struct AdpcmContext
    {
        std::uint16_t mPredScale; ///< the first frame's header byte
        std::int16_t mHistory0; ///< last decoded sample
        std::int16_t mHistory1; ///< the one before it
        std::uint16_t mPad;
    };
    static_assert(sizeof(AdpcmContext) == 8);

    constexpr std::uint32_t sSamplesPerFrame = 14;
    constexpr std::uint32_t sBytesPerFrame = 8;
}

#endif
