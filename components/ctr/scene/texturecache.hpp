#ifndef OPENMW_COMPONENTS_CTR_SCENE_TEXTURECACHE_H
#define OPENMW_COMPONENTS_CTR_SCENE_TEXTURECACHE_H

#include <string>
#include <string_view>
#include <unordered_map>

#include <components/ctr/gpu/ctrgpu.hpp>

namespace VFS
{
    class Manager;
}

namespace CtrScene
{
    class TextureCache
    {
    public:
        TextureCache(const VFS::Manager* vfs, int maxSize);

        CtrGpu::TexturePtr get(std::string_view path);

        CtrGpu::TexturePtr getExact(std::string_view normalizedPath);

        void purgeUnused();
        void clear();

        int getMaxSize() const { return mMaxSize; }
        std::size_t getByteSize() const;
        std::size_t size() const { return mTextures.size(); }

    private:
        const VFS::Manager* mVFS;
        int mMaxSize;
        std::unordered_map<std::string, CtrGpu::TexturePtr> mTextures;
        std::unordered_map<std::string, std::string> mCorrectedPaths;
    };
}

#endif
