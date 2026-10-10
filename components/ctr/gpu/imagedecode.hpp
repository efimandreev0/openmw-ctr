#ifndef OPENMW_COMPONENTS_CTR_GPU_IMAGEDECODE_H
#define OPENMW_COMPONENTS_CTR_GPU_IMAGEDECODE_H

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#ifndef OPENMW_CTR_PREP
#include <components/vfs/pathutil.hpp>

namespace VFS
{
    class Manager;
}
#endif

namespace CtrGpu
{
    struct RgbaImage
    {
        int mWidth = 0;
        int mHeight = 0;
        std::vector<std::uint8_t> mPixels;
    };

    bool decodeImage(const std::uint8_t* data, std::size_t size, std::string_view extension, int maxSize, RgbaImage& out);

#ifndef OPENMW_CTR_PREP
    bool loadImage(const VFS::Manager& vfs, VFS::Path::NormalizedView path, int maxSize, RgbaImage& out);
#endif
}

#endif
