#include "packfile.hpp"

#include <algorithm>
#include <cstring>

#include <components/debug/debuglog.hpp>

namespace CtrPack
{
    PackFile::~PackFile()
    {
        if (mFile != nullptr)
            std::fclose(mFile);
    }

    bool PackFile::open(const char* path, const char (&magic)[8])
    {
        std::FILE* file = std::fopen(path, "rb");
        if (file == nullptr)
            return false;
        PackHeader header{};
        long fileSize = -1;
        if (std::fread(&header, sizeof(header), 1, file) == 1 && std::fseek(file, 0, SEEK_END) == 0)
            fileSize = std::ftell(file);
        const std::uint64_t indexEnd
            = static_cast<std::uint64_t>(header.mIndexOffset) + static_cast<std::uint64_t>(header.mCount) * 16;
        if (std::memcmp(header.mMagic, magic, sizeof(header.mMagic)) != 0 || header.mVersion != sVersion
            || fileSize < 0 || indexEnd != static_cast<std::uint64_t>(fileSize))
        {
            Log(Debug::Warning) << path << " is not a pack of this version (rebuild it with ctrprep); ignored";
            std::fclose(file);
            return false;
        }

        std::vector<std::uint64_t> sparse;
        sparse.reserve((header.mCount + sSparseStep - 1) / sSparseStep);
        for (std::uint32_t i = 0; i < header.mCount; i += sSparseStep)
        {
            IndexEntry entry;
            if (std::fseek(file, static_cast<long>(header.mIndexOffset + i * sizeof(IndexEntry)), SEEK_SET) != 0
                || std::fread(&entry, sizeof(entry), 1, file) != 1)
            {
                std::fclose(file);
                return false;
            }
            sparse.push_back(entry.mHash);
        }

        mFile = file;
        mHeader = header;
        mSparse = std::move(sparse);
        return true;
    }

    bool PackFile::find(std::string_view path, IndexEntry& entry)
    {
        if (mFile == nullptr || mSparse.empty())
            return false;
        const std::uint64_t hash = hashPath(normalizePath(path));
        const auto slice = std::upper_bound(mSparse.begin(), mSparse.end(), hash);
        if (slice == mSparse.begin())
            return false;
        const std::size_t first = static_cast<std::size_t>(slice - mSparse.begin() - 1) * sSparseStep;
        const std::size_t count = std::min<std::size_t>(sSparseStep, mHeader.mCount - first);

        IndexEntry entries[sSparseStep];
        if (!read(static_cast<std::uint32_t>(mHeader.mIndexOffset + first * sizeof(IndexEntry)), entries,
                count * sizeof(IndexEntry)))
            return false;
        const IndexEntry* begin = entries;
        const IndexEntry* end = entries + count;
        const IndexEntry* found = std::lower_bound(
            begin, end, hash, [](const IndexEntry& e, std::uint64_t h) { return e.mHash < h; });
        if (found == end || found->mHash != hash)
            return false;
        entry = *found;
        return true;
    }

    bool PackFile::read(std::uint32_t offset, void* dst, std::size_t size)
    {
        if (mFile == nullptr)
            return false;
        const std::lock_guard<std::mutex> lock(mMutex);
        if (std::fseek(mFile, static_cast<long>(offset), SEEK_SET) != 0)
            return false;
        return std::fread(dst, 1, size, mFile) == size;
    }

    namespace
    {
        PackFile* openPack(const char* path, const char (&magic)[8], const char* what)
        {
            static_assert(sizeof(IndexEntry) == 16);
            auto* pack = new PackFile;
            if (!pack->open(path, magic))
            {
                delete pack;
                Log(Debug::Info) << "No prebuilt " << what << " (" << path << ")";
                return nullptr;
            }
            Log(Debug::Info) << "Prebuilt " << what << ": " << pack->getHeader().mCount << " entries";
            return pack;
        }
    }

    PackFile* getTexturePack()
    {
        static PackFile* const pack = openPack(sTexturePackPath, sTextureMagic, "textures");
        return pack;
    }

    PackFile* getSoundPack()
    {
        static PackFile* const pack = openPack(sSoundPackPath, sSoundMagic, "sounds");
        return pack;
    }
}
