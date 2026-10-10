#ifndef OPENMW_COMPONENTS_CTR_GPU_TEXCONVERT_H
#define OPENMW_COMPONENTS_CTR_GPU_TEXCONVERT_H


#include <cstddef>
#include <cstdint>
#include <vector>

namespace CtrGpu
{
    enum class TextureFormat
    {
        RGBA8,
        RGB8,
        RGBA5551,
        RGB565,
        RGBA4,
        LA8,
        A8,
        L8,
        LA4,
        A4,
        ETC1,
        ETC1A4,
    };

    std::size_t bitsPerPixel(TextureFormat format);

    std::uint8_t toGpuColorFormat(TextureFormat format);
    bool fromGpuColorFormat(std::uint8_t value, TextureFormat& format);

    inline std::uint32_t tiledPixelIndex(std::uint32_t x, std::uint32_t y, std::uint32_t width)
    {
        std::uint32_t i = (x & 7) | ((y & 7) << 8);
        i = (i ^ (i << 2)) & 0x1313;
        i = (i ^ (i << 1)) & 0x1515;
        i = (i | (i >> 7)) & 0x3F;
        return ((y & ~7u) * width) + ((x & ~7u) << 3) + i;
    }

    inline int nextPowerOfTwo(int value, int minimum = 8)
    {
        int result = minimum;
        while (result < value)
            result <<= 1;
        return result;
    }

    TextureFormat chooseFormat(const std::uint8_t* rgba, int width, int height);

    struct TextureLayout
    {
        int mWidth = 0;
        int mHeight = 0;
        int mMipLevels = 0;
    };
    TextureLayout computeTextureLayout(int width, int height, int maxSize, bool mipmaps);

    std::size_t tiledTextureSize(int width, int height, int mipLevels, TextureFormat format);

    std::vector<std::uint8_t> scaleImage(const std::uint8_t* rgba, int width, int height, int newWidth, int newHeight);

    void convertLevel(const std::uint8_t* rgba, int width, int height, TextureFormat format, std::uint8_t* dst);

    void buildTiledTexture(
        const std::uint8_t* rgba, int width, int height, int mipLevels, TextureFormat format, std::uint8_t* dst);
}

#endif
