#ifndef OPENMW_COMPONENTS_CTR_GPU_PACKFILE_H
#define OPENMW_COMPONENTS_CTR_GPU_PACKFILE_H

#include "ctrpack.hpp"

#include <cstdio>
#include <mutex>
#include <string_view>
#include <vector>

namespace CtrPack
{
    constexpr const char* sPrebuiltDir = "sdmc:/3ds/openmw/prebuilt";
    constexpr const char* sTexturePackPath = "sdmc:/3ds/openmw/prebuilt/textures.ctp";
    constexpr const char* sSoundPackPath = "sdmc:/3ds/openmw/prebuilt/sounds.ctp";

    class PackFile
    {
    public:
        PackFile() = default;
        ~PackFile();
        PackFile(const PackFile&) = delete;
        PackFile& operator=(const PackFile&) = delete;

        bool open(const char* path, const char (&magic)[8]);
        bool isOpen() const { return mFile != nullptr; }
        const PackHeader& getHeader() const { return mHeader; }

        bool find(std::string_view path, IndexEntry& entry);

        bool read(std::uint32_t offset, void* dst, std::size_t size);

    private:
        static constexpr std::size_t sSparseStep = 64;

        std::FILE* mFile = nullptr;
        PackHeader mHeader{};
        std::vector<std::uint64_t> mSparse;
        std::mutex mMutex;
    };

    PackFile* getTexturePack();
    PackFile* getSoundPack();
}

#endif
