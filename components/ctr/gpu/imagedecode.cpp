#include "imagedecode.hpp"

#include <algorithm>
#include <cstring>

#ifndef OPENMW_CTR_PREP
#include <components/debug/debuglog.hpp>
#include <components/vfs/manager.hpp>
#endif

#include <istream>

namespace CtrGpu
{
    namespace
    {
        std::uint32_t read32(const std::uint8_t* p)
        {
            return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8)
                | (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
        }

        std::uint16_t read16(const std::uint8_t* p)
        {
            return static_cast<std::uint16_t>(p[0] | (p[1] << 8));
        }

        void rgb565(std::uint16_t c, std::uint8_t* out)
        {
            const int r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
            out[0] = static_cast<std::uint8_t>((r << 3) | (r >> 2));
            out[1] = static_cast<std::uint8_t>((g << 2) | (g >> 4));
            out[2] = static_cast<std::uint8_t>((b << 3) | (b >> 2));
            out[3] = 255;
        }

        void decodeColourBlock(const std::uint8_t* block, std::uint8_t (*pixels)[4], bool allowTransparent)
        {
            const std::uint16_t c0 = read16(block), c1 = read16(block + 2);
            std::uint8_t palette[4][4];
            rgb565(c0, palette[0]);
            rgb565(c1, palette[1]);
            if (c0 > c1 || !allowTransparent)
            {
                for (int k = 0; k < 3; ++k)
                {
                    palette[2][k] = static_cast<std::uint8_t>((2 * palette[0][k] + palette[1][k] + 1) / 3);
                    palette[3][k] = static_cast<std::uint8_t>((palette[0][k] + 2 * palette[1][k] + 1) / 3);
                }
                palette[2][3] = palette[3][3] = 255;
            }
            else
            {
                for (int k = 0; k < 3; ++k)
                    palette[2][k] = static_cast<std::uint8_t>((palette[0][k] + palette[1][k]) / 2);
                palette[2][3] = 255;
                palette[3][0] = palette[3][1] = palette[3][2] = palette[3][3] = 0;
            }
            const std::uint32_t indices = read32(block + 4);
            for (int i = 0; i < 16; ++i)
                std::memcpy(pixels[i], palette[(indices >> (2 * i)) & 3], 4);
        }

        void decodeDxt(const std::uint8_t* src, int width, int height, int format, std::uint8_t* dst)
        {
            const int blockBytes = format == 1 ? 8 : 16;
            const int bw = std::max(1, (width + 3) / 4), bh = std::max(1, (height + 3) / 4);
            std::uint8_t pixels[16][4];
            for (int by = 0; by < bh; ++by)
            {
                for (int bx = 0; bx < bw; ++bx)
                {
                    const std::uint8_t* block = src + (static_cast<std::size_t>(by) * bw + bx) * blockBytes;
                    if (format == 1)
                        decodeColourBlock(block, pixels, true);
                    else
                    {
                        decodeColourBlock(block + 8, pixels, false);
                        if (format == 3)
                        {
                            for (int i = 0; i < 16; ++i)
                            {
                                const int a = (block[i / 2] >> ((i & 1) * 4)) & 15;
                                pixels[i][3] = static_cast<std::uint8_t>(a * 17);
                            }
                        }
                        else
                        {
                            const int a0 = block[0], a1 = block[1];
                            int alphas[8];
                            alphas[0] = a0;
                            alphas[1] = a1;
                            if (a0 > a1)
                                for (int k = 1; k < 7; ++k)
                                    alphas[k + 1] = ((7 - k) * a0 + k * a1 + 3) / 7;
                            else
                            {
                                for (int k = 1; k < 5; ++k)
                                    alphas[k + 1] = ((5 - k) * a0 + k * a1 + 2) / 5;
                                alphas[6] = 0;
                                alphas[7] = 255;
                            }
                            std::uint64_t bits = 0;
                            for (int k = 0; k < 6; ++k)
                                bits |= static_cast<std::uint64_t>(block[2 + k]) << (8 * k);
                            for (int i = 0; i < 16; ++i)
                                pixels[i][3] = static_cast<std::uint8_t>(alphas[(bits >> (3 * i)) & 7]);
                        }
                    }
                    for (int py = 0; py < 4; ++py)
                    {
                        const int y = by * 4 + py;
                        if (y >= height)
                            break;
                        for (int px = 0; px < 4; ++px)
                        {
                            const int x = bx * 4 + px;
                            if (x >= width)
                                break;
                            std::memcpy(dst + (static_cast<std::size_t>(y) * width + x) * 4, pixels[py * 4 + px], 4);
                        }
                    }
                }
            }
        }

        int maskShift(std::uint32_t mask)
        {
            if (mask == 0)
                return 0;
            int shift = 0;
            while (!(mask & 1))
            {
                mask >>= 1;
                ++shift;
            }
            return shift;
        }

        std::uint8_t maskValue(std::uint32_t pixel, std::uint32_t mask)
        {
            if (mask == 0)
                return 255;
            const int shift = maskShift(mask);
            const std::uint32_t m = mask >> shift;
            const std::uint32_t v = (pixel & mask) >> shift;
            return static_cast<std::uint8_t>(m == 255 ? v : (v * 255 + m / 2) / m);
        }

        bool decodeDds(const std::uint8_t* data, std::size_t size, int maxSize, RgbaImage& out)
        {
            if (size < 128 || std::memcmp(data, "DDS ", 4) != 0)
                return false;
            const std::uint8_t* h = data + 4;
            const int height = static_cast<int>(read32(h + 8));
            const int width = static_cast<int>(read32(h + 12));
            int mipCount = static_cast<int>(read32(h + 24));
            const std::uint8_t* pf = h + 72;
            const std::uint32_t pfFlags = read32(pf + 4);
            const std::uint32_t fourCC = read32(pf + 8);
            const std::uint32_t bitCount = read32(pf + 12);
            const std::uint32_t rMask = read32(pf + 16), gMask = read32(pf + 20), bMask = read32(pf + 24),
                                aMask = read32(pf + 28);
            if (width <= 0 || height <= 0 || width > 8192 || height > 8192)
                return false;
            if (mipCount < 1)
                mipCount = 1;

            int dxt = 0;
            if (pfFlags & 0x4) // DDPF_FOURCC
            {
                if (fourCC == 0x31545844) // DXT1
                    dxt = 1;
                else if (fourCC == 0x33545844 || fourCC == 0x32545844) // DXT3 / DXT2
                    dxt = 3;
                else if (fourCC == 0x35545844 || fourCC == 0x34545844) // DXT5 / DXT4
                    dxt = 5;
                else
                    return false;
            }
            const int bytesPerPixel = dxt ? 0 : static_cast<int>(bitCount / 8);
            if (!dxt && (bytesPerPixel < 1 || bytesPerPixel > 4))
                return false;

            auto levelSize = [&](int w, int hgt) -> std::size_t {
                if (dxt)
                    return static_cast<std::size_t>(std::max(1, (w + 3) / 4)) * std::max(1, (hgt + 3) / 4)
                        * (dxt == 1 ? 8 : 16);
                return static_cast<std::size_t>(w) * hgt * bytesPerPixel;
            };

            std::size_t offset = 128;
            int w = width, hgt = height;
            int level = 0;
            while (level + 1 < mipCount && (w > maxSize || hgt > maxSize))
            {
                offset += levelSize(w, hgt);
                w = std::max(1, w / 2);
                hgt = std::max(1, hgt / 2);
                ++level;
            }
            const std::size_t needed = levelSize(w, hgt);
            if (offset + needed > size)
                return false;

            out.mWidth = w;
            out.mHeight = hgt;
            out.mPixels.assign(static_cast<std::size_t>(w) * hgt * 4, 0);
            const std::uint8_t* src = data + offset;
            if (dxt)
            {
                decodeDxt(src, w, hgt, dxt, out.mPixels.data());
                return true;
            }
            const bool luminance = (pfFlags & 0x20000) != 0; // DDPF_LUMINANCE
            const bool hasAlpha = (pfFlags & 0x1) != 0;      // DDPF_ALPHAPIXELS
            const bool alphaOnly = (pfFlags & 0x2) != 0;     // DDPF_ALPHA
            for (std::size_t i = 0, n = static_cast<std::size_t>(w) * hgt; i < n; ++i)
            {
                std::uint32_t pixel = 0;
                for (int b = 0; b < bytesPerPixel; ++b)
                    pixel |= static_cast<std::uint32_t>(src[i * bytesPerPixel + b]) << (8 * b);
                std::uint8_t* p = &out.mPixels[i * 4];
                if (alphaOnly)
                {
                    p[0] = p[1] = p[2] = 255;
                    p[3] = maskValue(pixel, aMask ? aMask : 0xFF);
                }
                else if (luminance)
                {
                    p[0] = p[1] = p[2] = maskValue(pixel, rMask ? rMask : 0xFF);
                    p[3] = hasAlpha ? maskValue(pixel, aMask) : 255;
                }
                else
                {
                    p[0] = maskValue(pixel, rMask);
                    p[1] = maskValue(pixel, gMask);
                    p[2] = maskValue(pixel, bMask);
                    p[3] = hasAlpha ? maskValue(pixel, aMask) : 255;
                }
            }
            return true;
        }

        bool decodeTga(const std::uint8_t* data, std::size_t size, RgbaImage& out)
        {
            if (size < 18)
                return false;
            const int idLength = data[0];
            const int colorMapType = data[1];
            const int imageType = data[2];
            const int cmFirst = read16(data + 3);
            const int cmLength = read16(data + 5);
            const int cmDepth = data[7];
            const int width = read16(data + 12);
            const int height = read16(data + 14);
            const int depth = data[16];
            const int descriptor = data[17];
            const bool rle = imageType >= 9;
            const int baseType = rle ? imageType - 8 : imageType;
            if (width <= 0 || height <= 0 || (baseType != 1 && baseType != 2 && baseType != 3))
                return false;

            std::size_t pos = 18 + idLength;
            std::vector<std::uint8_t> palette;
            if (colorMapType == 1)
            {
                const int entryBytes = (cmDepth + 7) / 8;
                if (pos + static_cast<std::size_t>(cmLength) * entryBytes > size)
                    return false;
                palette.resize(static_cast<std::size_t>(cmFirst + cmLength) * 4, 255);
                for (int i = 0; i < cmLength; ++i)
                {
                    const std::uint8_t* e = data + pos + i * entryBytes;
                    std::uint8_t* p = &palette[(cmFirst + i) * 4];
                    if (entryBytes >= 3)
                    {
                        p[0] = e[2];
                        p[1] = e[1];
                        p[2] = e[0];
                        p[3] = entryBytes == 4 ? e[3] : 255;
                    }
                    else if (entryBytes == 2)
                    {
                        const int v = read16(e);
                        p[0] = static_cast<std::uint8_t>(((v >> 10) & 31) * 255 / 31);
                        p[1] = static_cast<std::uint8_t>(((v >> 5) & 31) * 255 / 31);
                        p[2] = static_cast<std::uint8_t>((v & 31) * 255 / 31);
                    }
                }
                pos += static_cast<std::size_t>(cmLength) * entryBytes;
            }

            const int bytesPerPixel = (depth + 7) / 8;
            if (bytesPerPixel < 1 || bytesPerPixel > 4)
                return false;
            out.mWidth = width;
            out.mHeight = height;
            out.mPixels.assign(static_cast<std::size_t>(width) * height * 4, 255);

            auto writePixel = [&](std::size_t index, const std::uint8_t* src) {
                const int row = static_cast<int>(index / width);
                const int col = static_cast<int>(index % width);
                const int y = (descriptor & 0x20) ? row : height - 1 - row;
                std::uint8_t* p = &out.mPixels[(static_cast<std::size_t>(y) * width + col) * 4];
                if (baseType == 1)
                {
                    const int idx = bytesPerPixel == 2 ? read16(src) : src[0];
                    if (static_cast<std::size_t>(idx) * 4 + 3 < palette.size())
                        std::memcpy(p, &palette[idx * 4], 4);
                }
                else if (baseType == 3)
                {
                    p[0] = p[1] = p[2] = src[0];
                    p[3] = bytesPerPixel == 2 ? src[1] : 255;
                }
                else if (bytesPerPixel == 2)
                {
                    const int v = read16(src);
                    p[0] = static_cast<std::uint8_t>(((v >> 10) & 31) * 255 / 31);
                    p[1] = static_cast<std::uint8_t>(((v >> 5) & 31) * 255 / 31);
                    p[2] = static_cast<std::uint8_t>((v & 31) * 255 / 31);
                    p[3] = 255; // Morrowind ignores the alpha bit of 16bpp TGA files
                }
                else
                {
                    p[0] = src[2];
                    p[1] = src[1];
                    p[2] = src[0];
                    p[3] = bytesPerPixel == 4 ? src[3] : 255;
                }
            };

            const std::size_t total = static_cast<std::size_t>(width) * height;
            if (!rle)
            {
                if (pos + total * bytesPerPixel > size)
                    return false;
                for (std::size_t i = 0; i < total; ++i)
                    writePixel(i, data + pos + i * bytesPerPixel);
                return true;
            }
            std::size_t i = 0;
            while (i < total)
            {
                if (pos >= size)
                    return false;
                const int header = data[pos++];
                const int count = (header & 0x7F) + 1;
                if (header & 0x80)
                {
                    if (pos + bytesPerPixel > size)
                        return false;
                    for (int k = 0; k < count && i < total; ++k)
                        writePixel(i++, data + pos);
                    pos += bytesPerPixel;
                }
                else
                {
                    if (pos + static_cast<std::size_t>(count) * bytesPerPixel > size)
                        return false;
                    for (int k = 0; k < count && i < total; ++k, pos += bytesPerPixel)
                        writePixel(i++, data + pos);
                }
            }
            return true;
        }

        bool decodeBmp(const std::uint8_t* data, std::size_t size, RgbaImage& out)
        {
            if (size < 54 || data[0] != 'B' || data[1] != 'M')
                return false;
            const std::uint32_t pixelOffset = read32(data + 10);
            const std::uint32_t headerSize = read32(data + 14);
            const int width = static_cast<int>(read32(data + 18));
            int height = static_cast<int>(read32(data + 22));
            const int bitCount = read16(data + 28);
            const std::uint32_t compression = read32(data + 30);
            std::uint32_t colours = read32(data + 46);
            const bool topDown = height < 0;
            height = std::abs(height);
            if (width <= 0 || height <= 0 || (compression != 0 && compression != 3))
                return false;
            if (bitCount != 8 && bitCount != 24 && bitCount != 32)
                return false;
            const std::size_t stride = ((static_cast<std::size_t>(width) * bitCount + 31) / 32) * 4;
            if (pixelOffset + stride * height > size)
                return false;
            const std::uint8_t* paletteData = data + 14 + headerSize;
            if (bitCount == 8 && colours == 0)
                colours = 256;
            out.mWidth = width;
            out.mHeight = height;
            out.mPixels.assign(static_cast<std::size_t>(width) * height * 4, 255);
            for (int row = 0; row < height; ++row)
            {
                const std::uint8_t* src = data + pixelOffset + stride * row;
                const int y = topDown ? row : height - 1 - row;
                std::uint8_t* dst = &out.mPixels[static_cast<std::size_t>(y) * width * 4];
                for (int x = 0; x < width; ++x, dst += 4)
                {
                    if (bitCount == 8)
                    {
                        const std::uint32_t idx = src[x];
                        if (idx < colours && paletteData + idx * 4 + 3 <= data + size)
                        {
                            dst[0] = paletteData[idx * 4 + 2];
                            dst[1] = paletteData[idx * 4 + 1];
                            dst[2] = paletteData[idx * 4 + 0];
                        }
                    }
                    else
                    {
                        const std::uint8_t* p = src + x * (bitCount / 8);
                        dst[0] = p[2];
                        dst[1] = p[1];
                        dst[2] = p[0];
                    }
                }
            }
            return true;
        }
    }

    bool decodeImage(const std::uint8_t* data, std::size_t size, std::string_view extension, int maxSize, RgbaImage& out)
    {
        if (data == nullptr || size < 4)
            return false;
        if (std::memcmp(data, "DDS ", 4) == 0)
            return decodeDds(data, size, maxSize, out);
        if (data[0] == 'B' && data[1] == 'M')
            return decodeBmp(data, size, out);
        (void)extension;
        return decodeTga(data, size, out);
    }

#ifndef OPENMW_CTR_PREP
    bool loadImage(const VFS::Manager& vfs, VFS::Path::NormalizedView path, int maxSize, RgbaImage& out)
    {
        Files::IStreamPtr stream = vfs.find(path);
        if (stream == nullptr)
        {
            Log(Debug::Error) << "Image not found: " << path;
            return false;
        }
        std::vector<std::uint8_t> bytes;
        char buffer[4096];
        while (stream->read(buffer, sizeof(buffer)) || stream->gcount() > 0)
            bytes.insert(bytes.end(), buffer, buffer + stream->gcount());
        stream.reset();
        std::string_view name = path.value();
        const std::size_t dot = name.rfind('.');
        const std::string_view extension = dot == std::string_view::npos ? std::string_view() : name.substr(dot + 1);
        if (!decodeImage(bytes.data(), bytes.size(), extension, maxSize, out))
        {
            Log(Debug::Error) << "Unsupported or corrupt image: " << path;
            return false;
        }
        return true;
    }
#endif
}
