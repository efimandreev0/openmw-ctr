#include "texconvert.hpp"

#include <algorithm>

namespace CtrGpu
{
    namespace
    {
        inline std::uint8_t luminance(const std::uint8_t* p)
        {
            return static_cast<std::uint8_t>((p[0] * 77 + p[1] * 150 + p[2] * 29) >> 8);
        }

        std::vector<std::uint8_t> halve(const std::uint8_t* src, int width, int height)
        {
            const int w = std::max(1, width / 2);
            const int h = std::max(1, height / 2);
            std::vector<std::uint8_t> out(static_cast<std::size_t>(w) * h * 4);
            for (int y = 0; y < h; ++y)
            {
                for (int x = 0; x < w; ++x)
                {
                    for (int c = 0; c < 4; ++c)
                    {
                        const int x0 = std::min(x * 2, width - 1), x1 = std::min(x * 2 + 1, width - 1);
                        const int y0 = std::min(y * 2, height - 1), y1 = std::min(y * 2 + 1, height - 1);
                        const int sum = src[(y0 * width + x0) * 4 + c] + src[(y0 * width + x1) * 4 + c]
                            + src[(y1 * width + x0) * 4 + c] + src[(y1 * width + x1) * 4 + c];
                        out[(static_cast<std::size_t>(y) * w + x) * 4 + c] = static_cast<std::uint8_t>((sum + 2) / 4);
                    }
                }
            }
            return out;
        }

        std::vector<std::uint8_t> resample(const std::uint8_t* src, int width, int height, int newWidth, int newHeight)
        {
            std::vector<std::uint8_t> out(static_cast<std::size_t>(newWidth) * newHeight * 4);
            const float sx = static_cast<float>(width) / newWidth;
            const float sy = static_cast<float>(height) / newHeight;
            for (int y = 0; y < newHeight; ++y)
            {
                const float fy = std::max(0.f, (y + 0.5f) * sy - 0.5f);
                const int y0 = std::min(static_cast<int>(fy), height - 1);
                const int y1 = std::min(y0 + 1, height - 1);
                const float ty = fy - y0;
                for (int x = 0; x < newWidth; ++x)
                {
                    const float fx = std::max(0.f, (x + 0.5f) * sx - 0.5f);
                    const int x0 = std::min(static_cast<int>(fx), width - 1);
                    const int x1 = std::min(x0 + 1, width - 1);
                    const float tx = fx - x0;
                    for (int c = 0; c < 4; ++c)
                    {
                        const float a = src[(y0 * width + x0) * 4 + c] * (1 - tx) + src[(y0 * width + x1) * 4 + c] * tx;
                        const float b = src[(y1 * width + x0) * 4 + c] * (1 - tx) + src[(y1 * width + x1) * 4 + c] * tx;
                        out[(static_cast<std::size_t>(y) * newWidth + x) * 4 + c]
                            = static_cast<std::uint8_t>(a * (1 - ty) + b * ty + 0.5f);
                    }
                }
            }
            return out;
        }
    }

    std::size_t bitsPerPixel(TextureFormat format)
    {
        switch (format)
        {
            case TextureFormat::RGBA8:
                return 32;
            case TextureFormat::RGB8:
                return 24;
            case TextureFormat::RGBA5551:
            case TextureFormat::RGB565:
            case TextureFormat::RGBA4:
            case TextureFormat::LA8:
                return 16;
            case TextureFormat::A8:
            case TextureFormat::L8:
            case TextureFormat::LA4:
            case TextureFormat::ETC1A4:
                return 8;
            case TextureFormat::A4:
            case TextureFormat::ETC1:
                return 4;
        }
        return 32;
    }

    std::uint8_t toGpuColorFormat(TextureFormat format)
    {
        switch (format)
        {
            case TextureFormat::RGBA8:
                return 0x0;
            case TextureFormat::RGB8:
                return 0x1;
            case TextureFormat::RGBA5551:
                return 0x2;
            case TextureFormat::RGB565:
                return 0x3;
            case TextureFormat::RGBA4:
                return 0x4;
            case TextureFormat::LA8:
                return 0x5;
            case TextureFormat::L8:
                return 0x7;
            case TextureFormat::A8:
                return 0x8;
            case TextureFormat::LA4:
                return 0x9;
            case TextureFormat::A4:
                return 0xB;
            case TextureFormat::ETC1:
                return 0xC;
            case TextureFormat::ETC1A4:
                return 0xD;
        }
        return 0x0;
    }

    bool fromGpuColorFormat(std::uint8_t value, TextureFormat& format)
    {
        static const TextureFormat all[] = { TextureFormat::RGBA8, TextureFormat::RGB8, TextureFormat::RGBA5551,
            TextureFormat::RGB565, TextureFormat::RGBA4, TextureFormat::LA8, TextureFormat::A8, TextureFormat::L8,
            TextureFormat::LA4, TextureFormat::A4, TextureFormat::ETC1, TextureFormat::ETC1A4 };
        for (TextureFormat candidate : all)
        {
            if (toGpuColorFormat(candidate) == value)
            {
                format = candidate;
                return true;
            }
        }
        return false;
    }

    TextureFormat chooseFormat(const std::uint8_t* rgba, int width, int height)
    {
        bool opaque = true;
        bool whiteWithAlpha = true;
        const std::size_t count = static_cast<std::size_t>(width) * height;
        for (std::size_t i = 0; i < count; ++i)
        {
            const std::uint8_t* p = rgba + i * 4;
            if (p[3] != 255)
                opaque = false;
            if (p[3] != 0 && (p[0] < 240 || p[1] < 240 || p[2] < 240))
                whiteWithAlpha = false;
            if (!opaque && !whiteWithAlpha)
                break;
        }
        if (whiteWithAlpha && !opaque)
            return TextureFormat::LA4;
        if (opaque)
            return TextureFormat::RGB565;
        return TextureFormat::RGBA4;
    }

    TextureLayout computeTextureLayout(int width, int height, int maxSize, bool mipmaps)
    {
        maxSize = std::clamp(maxSize, 8, 1024);
        int targetW = nextPowerOfTwo(width);
        int targetH = nextPowerOfTwo(height);
        while (targetW > maxSize)
            targetW /= 2;
        while (targetH > maxSize)
            targetH /= 2;
        if (targetW > width && targetW / 2 >= 8 && width - targetW / 2 < targetW / 16)
            targetW /= 2;
        if (targetH > height && targetH / 2 >= 8 && height - targetH / 2 < targetH / 16)
            targetH /= 2;

        TextureLayout layout;
        layout.mWidth = targetW;
        layout.mHeight = targetH;
        if (mipmaps)
            for (int side = std::min(targetW, targetH); side > 8; side /= 2)
                ++layout.mMipLevels;
        return layout;
    }

    std::size_t tiledTextureSize(int width, int height, int mipLevels, TextureFormat format)
    {
        std::size_t total = 0;
        for (int l = 0; l <= mipLevels; ++l)
            total += static_cast<std::size_t>(width >> l) * (height >> l) * bitsPerPixel(format) / 8;
        return total;
    }

    std::vector<std::uint8_t> scaleImage(const std::uint8_t* rgba, int width, int height, int newWidth, int newHeight)
    {
        std::vector<std::uint8_t> scaled;
        const std::uint8_t* source = rgba;
        int w = width, h = height;
        while (w >= newWidth * 2 && h >= newHeight * 2)
        {
            std::vector<std::uint8_t> tmp = halve(source, w, h);
            w /= 2;
            h /= 2;
            scaled.swap(tmp);
            source = scaled.data();
        }
        if (w != newWidth || h != newHeight)
        {
            std::vector<std::uint8_t> tmp = resample(source, w, h, newWidth, newHeight);
            scaled.swap(tmp);
        }
        else if (scaled.empty())
            scaled.assign(rgba, rgba + static_cast<std::size_t>(width) * height * 4);
        return scaled;
    }

    void convertLevel(const std::uint8_t* rgba, int width, int height, TextureFormat format, std::uint8_t* dst)
    {
        for (int y = 0; y < height; ++y)
        {
            const std::uint8_t* row = rgba + static_cast<std::size_t>(y) * width * 4;
            for (int x = 0; x < width; ++x)
            {
                const std::uint8_t* p = row + x * 4;
                const std::uint32_t i = tiledPixelIndex(x, height - 1 - y, width);
                switch (format)
                {
                    case TextureFormat::RGBA8:
                        dst[i * 4 + 0] = p[3];
                        dst[i * 4 + 1] = p[2];
                        dst[i * 4 + 2] = p[1];
                        dst[i * 4 + 3] = p[0];
                        break;
                    case TextureFormat::RGB8:
                        dst[i * 3 + 0] = p[2];
                        dst[i * 3 + 1] = p[1];
                        dst[i * 3 + 2] = p[0];
                        break;
                    case TextureFormat::RGBA5551:
                    {
                        const std::uint16_t v = static_cast<std::uint16_t>(
                            ((p[0] >> 3) << 11) | ((p[1] >> 3) << 6) | ((p[2] >> 3) << 1) | (p[3] >= 128 ? 1 : 0));
                        dst[i * 2 + 0] = static_cast<std::uint8_t>(v & 0xFF);
                        dst[i * 2 + 1] = static_cast<std::uint8_t>(v >> 8);
                        break;
                    }
                    case TextureFormat::RGB565:
                    {
                        const std::uint16_t v
                            = static_cast<std::uint16_t>(((p[0] >> 3) << 11) | ((p[1] >> 2) << 5) | (p[2] >> 3));
                        dst[i * 2 + 0] = static_cast<std::uint8_t>(v & 0xFF);
                        dst[i * 2 + 1] = static_cast<std::uint8_t>(v >> 8);
                        break;
                    }
                    case TextureFormat::RGBA4:
                    {
                        const std::uint16_t v = static_cast<std::uint16_t>(
                            ((p[0] >> 4) << 12) | ((p[1] >> 4) << 8) | ((p[2] >> 4) << 4) | (p[3] >> 4));
                        dst[i * 2 + 0] = static_cast<std::uint8_t>(v & 0xFF);
                        dst[i * 2 + 1] = static_cast<std::uint8_t>(v >> 8);
                        break;
                    }
                    case TextureFormat::LA8:
                        dst[i * 2 + 0] = p[3];
                        dst[i * 2 + 1] = luminance(p);
                        break;
                    case TextureFormat::A8:
                        dst[i] = p[3];
                        break;
                    case TextureFormat::L8:
                        dst[i] = luminance(p);
                        break;
                    case TextureFormat::LA4:
                        dst[i] = static_cast<std::uint8_t>(((luminance(p) >> 4) << 4) | (p[3] >> 4));
                        break;
                    case TextureFormat::A4:
                    {
                        std::uint8_t& b = dst[i >> 1];
                        const std::uint8_t a = p[3] >> 4;
                        b = (i & 1) ? static_cast<std::uint8_t>((b & 0x0F) | (a << 4))
                                    : static_cast<std::uint8_t>((b & 0xF0) | a);
                        break;
                    }
                    case TextureFormat::ETC1:
                    case TextureFormat::ETC1A4:
                        return;
                }
            }
        }
    }

    void buildTiledTexture(
        const std::uint8_t* rgba, int width, int height, int mipLevels, TextureFormat format, std::uint8_t* dst)
    {
        std::vector<std::uint8_t> level;
        const std::uint8_t* source = rgba;
        for (int l = 0; l <= mipLevels; ++l)
        {
            convertLevel(source, width, height, format, dst);
            dst += static_cast<std::size_t>(width) * height * bitsPerPixel(format) / 8;
            if (l < mipLevels)
            {
                std::vector<std::uint8_t> next = halve(source, width, height);
                level.swap(next);
                source = level.data();
                width = std::max(8, width / 2);
                height = std::max(8, height / 2);
            }
        }
    }
}
