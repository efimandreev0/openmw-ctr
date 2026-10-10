#include "texturecache.hpp"

#include <components/ctr/gpu/imagedecode.hpp>
#include <components/ctr/gpu/packfile.hpp>
#include <components/debug/debuglog.hpp>
#include <components/misc/resourcehelpers.hpp>
#include <components/vfs/manager.hpp>
#include <components/vfs/pathutil.hpp>

namespace CtrScene
{
    namespace
    {
        CtrGpu::TexturePtr loadPrebuilt(std::string_view path, int maxSize)
        {
            CtrPack::PackFile* pack = CtrPack::getTexturePack();
            if (pack == nullptr || pack->getHeader().mMaxSize != static_cast<std::uint32_t>(maxSize))
                return nullptr;
            CtrPack::IndexEntry entry;
            if (!pack->find(path, entry))
                return nullptr;
            constexpr std::size_t headerSize = 21;
            std::uint8_t header[headerSize];
            if (entry.mSize < headerSize || !pack->read(entry.mOffset, header, headerSize))
                return nullptr;
            const int subTextures = header[0] | (header[1] << 8);
            const int width = 1 << ((header[2] & 7) + 3);
            const int height = 1 << (((header[2] >> 3) & 7) + 3);
            const bool cube = (header[2] & 0x40) != 0;
            const int mipLevels = header[4];
            const std::size_t size = header[18] | (header[19] << 8) | (static_cast<std::size_t>(header[20]) << 16);
            CtrGpu::TextureFormat format;
            if (subTextures != 1 || cube || header[17] != 0 || !CtrGpu::fromGpuColorFormat(header[3], format)
                || size != CtrGpu::tiledTextureSize(width, height, mipLevels, format) || headerSize + size > entry.mSize)
            {
                Log(Debug::Warning) << "Bad prebuilt texture " << path;
                return nullptr;
            }
            CtrGpu::TexturePtr texture = CtrGpu::createTexture(width, height, format, mipLevels, true);
            if (texture == nullptr)
                return nullptr;
            const std::uint32_t dataOffset = entry.mOffset + headerSize;
            if (!texture->fillTiled(
                    size, [&](void* dst, std::size_t bytes) { return pack->read(dataOffset, dst, bytes); }))
                return nullptr;
            return texture;
        }
    }

    TextureCache::TextureCache(const VFS::Manager* vfs, int maxSize)
        : mVFS(vfs)
        , mMaxSize(maxSize)
    {
    }

    CtrGpu::TexturePtr TextureCache::get(std::string_view path)
    {
        if (path.empty())
            return nullptr;
        const std::string key = VFS::Path::normalizeFilename(path);
        auto corrected = mCorrectedPaths.find(key);
        if (corrected == mCorrectedPaths.end())
        {
            const VFS::Path::Normalized fixed
                = Misc::ResourceHelpers::correctTexturePath(VFS::Path::Normalized(key), *mVFS);
            corrected = mCorrectedPaths.emplace(key, fixed.value()).first;
        }
        return getExact(corrected->second);
    }

    CtrGpu::TexturePtr TextureCache::getExact(std::string_view normalizedPath)
    {
        const std::string key(normalizedPath);
        if (auto found = mTextures.find(key); found != mTextures.end())
            return found->second;

        CtrGpu::TexturePtr texture = loadPrebuilt(key, mMaxSize);
        CtrGpu::RgbaImage image;
        if (texture != nullptr)
            texture->setWrap(true);
        else if (CtrGpu::loadImage(*mVFS, VFS::Path::Normalized(key), mMaxSize, image))
        {
            const CtrGpu::TextureFormat format
                = CtrGpu::chooseFormat(image.mPixels.data(), image.mWidth, image.mHeight);
            texture = CtrGpu::createTextureFromRGBA8(
                image.mPixels.data(), image.mWidth, image.mHeight, format, mMaxSize, true, true);
            if (texture)
                texture->setWrap(true);
        }
        mTextures.emplace(key, texture);
        return texture;
    }

    void TextureCache::purgeUnused()
    {
        for (auto it = mTextures.begin(); it != mTextures.end();)
        {
            if (it->second == nullptr || it->second.use_count() == 1)
                it = mTextures.erase(it);
            else
                ++it;
        }
    }

    void TextureCache::clear()
    {
        mTextures.clear();
        mCorrectedPaths.clear();
    }

    std::size_t TextureCache::getByteSize() const
    {
        std::size_t total = 0;
        for (const auto& [path, texture] : mTextures)
            if (texture != nullptr)
                total += texture->getByteSize();
        return total;
    }
}
