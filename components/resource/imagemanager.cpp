#include "imagemanager.hpp"

#include <cassert>
#include <cstdint>
#include <osgDB/Registry>

#include <components/debug/debuglog.hpp>
#include <components/misc/pathhelpers.hpp>
#include <components/sceneutil/glextensions.hpp>
#include <components/vfs/manager.hpp>
#include <components/vfs/pathutil.hpp>
#if defined(__vita__) || defined(__3DS__)
#include <cstring>
#include <ctime>
#include <malloc.h>
#include <components/settings/values.hpp>
#endif

#include "objectcache.hpp"

#if defined(__vita__) || defined(__3DS__)
namespace
{
    // mallinfo() walks the allocator's free-list metadata; at 200+ MB live
    // with typical fragmentation that's 1-4 ms per call. The dynamic
    // texture-tier-down below would otherwise call this on every image
    // load. Cache at 1 Hz — heap pressure changes on the order of seconds.
    int getCachedHeapUsedMB()
    {
        static int s_cachedMB = 0;
        static std::time_t s_lastSec = 0;
        const std::time_t now = std::time(nullptr);
        if (s_lastSec == 0 || now != s_lastSec)
        {
            const struct mallinfo mi = mallinfo();
            s_cachedMB = mi.uordblks / (1024 * 1024);
            s_lastSec = now;
        }
        return s_cachedMB;
    }
}
#endif

#ifdef OSG_LIBRARY_STATIC
// This list of plugins should match with the list in the top-level CMakelists.txt.
USE_OSGPLUGIN(png)
USE_OSGPLUGIN(tga)
USE_OSGPLUGIN(dds)
USE_OSGPLUGIN(jpeg)
USE_OSGPLUGIN(bmp)
#if !defined(__vita__) && !defined(__3DS__)
USE_OSGPLUGIN(osg)
USE_SERIALIZER_WRAPPER_LIBRARY(osg)
#endif
#endif

#ifdef __3DS__
extern "C"
{
    // NovaGL's pre-swizzled texture cache (see NovaGL.h): .nsw files keyed by
    // a 32-bit hash, carrying PICA-native (morton-tiled) texel data plus a
    // full mip chain, produced offline by scripts/ctr/preprocess_textures_nsw.py.
    int nova_texture_cache_has(uint32_t hash);
    int nova_texture_cache_peek(uint32_t hash, int* out_tgt_w, int* out_tgt_h, int* out_orig_w, int* out_orig_h);
}
namespace
{
    // Must match the preprocessor: FNV-1a 32 over the normalized VFS path.
    uint32_t ctrPathHash(std::string_view s)
    {
        uint32_t h = 2166136261u;
        for (char c : s)
        {
            h ^= static_cast<unsigned char>(c);
            h *= 16777619u;
        }
        return h;
    }

    // On a cache hit, hand OSG a 64-byte "NVSWSTUB" placeholder instead of
    // decoded pixels. NovaGL's glTexImage2D recognizes the magic and streams
    // the pre-swizzled .nsw straight into the bound texture: zero decode,
    // zero downscale, zero swizzle on console, and the CPU-side image is 64
    // bytes instead of w*h*4. Dimensions are reported as the cache's POT
    // target size so OSG never tries to rescale the stub.
    osg::ref_ptr<osg::Image> tryLoadNsw(VFS::Path::NormalizedView path)
    {
        const uint32_t hash = ctrPathHash(path.value());
        if (!nova_texture_cache_has(hash))
            return nullptr;
        int tgtW = 0, tgtH = 0, origW = 0, origH = 0;
        if (!nova_texture_cache_peek(hash, &tgtW, &tgtH, &origW, &origH))
            return nullptr;
        if (tgtW < 8 || tgtH < 8)
            return nullptr;

        unsigned char* stub = new unsigned char[64];
        std::memset(stub, 0, 64);
        std::memcpy(stub, "NVSWSTUB", 8);
        std::memcpy(stub + 8, &hash, 4);

        osg::ref_ptr<osg::Image> img = new osg::Image;
        img->setImage(tgtW, tgtH, 1, GL_RGBA, GL_RGBA, GL_UNSIGNED_BYTE, stub, osg::Image::USE_NEW_DELETE);
        return img;
    }
}
#endif

namespace
{

    osg::ref_ptr<osg::Image> createWarningImage()
    {
        osg::ref_ptr<osg::Image> warningImage = new osg::Image;

        int width = 8, height = 8;
        warningImage->allocateImage(width, height, 1, GL_RGB, GL_UNSIGNED_BYTE);
        assert(warningImage->isDataContiguous());
        unsigned char* data = warningImage->data();
        for (int i = 0; i < width * height; ++i)
        {
            data[3 * i] = (255);
            data[3 * i + 1] = (0);
            data[3 * i + 2] = (255);
        }
        return warningImage;
    }

#if defined(__vita__) || defined(__3DS__)
    struct DXT1Block
    {
        uint16_t color_0, color_1;
        uint32_t texels;
    };

    struct DXT3Block
    {
        uint16_t alpha[4];
        uint16_t color_0, color_1;
        uint32_t texels;
    };

    struct DXT5Block
    {
        uint8_t alpha_0, alpha_1;
        uint8_t alphaIdx[6];
        uint16_t color_0, color_1;
        uint32_t texels;
    };

    inline void rgb565to888(uint16_t c, uint8_t& r, uint8_t& g, uint8_t& b)
    {
        r = ((c >> 11) & 0x1F) * 255 / 31;
        g = ((c >> 5) & 0x3F) * 255 / 63;
        b = (c & 0x1F) * 255 / 31;
    }

    void decompressDXT(const osg::Image* src, osg::Image* dst)
    {
        const int w = src->s();
        const int h = src->t();
        const GLenum fmt = src->getPixelFormat();
        const bool isDXT1 = (fmt == GL_COMPRESSED_RGB_S3TC_DXT1_EXT || fmt == GL_COMPRESSED_RGBA_S3TC_DXT1_EXT);
        const bool isDXT3 = (fmt == GL_COMPRESSED_RGBA_S3TC_DXT3_EXT);
        // isDXT5 implied otherwise
        const bool hasAlpha = (dst->getPixelFormat() == GL_RGBA);
        const int bpp = hasAlpha ? 4 : 3;
        const int rowBytes = w * bpp;

        const unsigned char* srcData = src->data();
        unsigned char* dstData = dst->data();

        const int blocksX = (w + 3) / 4;
        const int blocksY = (h + 3) / 4;

        for (int by = 0; by < blocksY; ++by)
        {
            for (int bx = 0; bx < blocksX; ++bx)
            {
                uint8_t colors[4][4]; // [index][r,g,b,a]
                uint8_t blockAlpha[16]; // per-pixel alpha for DXT3/5

                if (isDXT1)
                {
                    const auto* blk = reinterpret_cast<const DXT1Block*>(srcData);
                    srcData += sizeof(DXT1Block);

                    rgb565to888(blk->color_0, colors[0][0], colors[0][1], colors[0][2]);
                    colors[0][3] = 255;
                    rgb565to888(blk->color_1, colors[1][0], colors[1][1], colors[1][2]);
                    colors[1][3] = 255;

                    if (blk->color_0 > blk->color_1)
                    {
                        for (int c = 0; c < 3; ++c)
                        {
                            colors[2][c] = (2 * colors[0][c] + colors[1][c] + 1) / 3;
                            colors[3][c] = (colors[0][c] + 2 * colors[1][c] + 1) / 3;
                        }
                        colors[2][3] = 255;
                        colors[3][3] = 255;
                    }
                    else
                    {
                        for (int c = 0; c < 3; ++c)
                            colors[2][c] = (colors[0][c] + colors[1][c]) / 2;
                        colors[2][3] = 255;
                        colors[3][0] = colors[3][1] = colors[3][2] = 0;
                        colors[3][3] = 0; // transparent black
                    }

                    uint32_t bits = blk->texels;
                    const int maxPy = std::min(4, h - by * 4);
                    const int maxPx = std::min(4, w - bx * 4);
                    for (int py = 0; py < maxPy; ++py)
                    {
                        unsigned char* row = dstData + (by * 4 + py) * rowBytes + bx * 4 * bpp;
                        for (int px = 0; px < maxPx; ++px)
                        {
                            int idx = bits & 3;
                            bits >>= 2;
                            row[px * bpp + 0] = colors[idx][0];
                            row[px * bpp + 1] = colors[idx][1];
                            row[px * bpp + 2] = colors[idx][2];
                            if (hasAlpha)
                                row[px * bpp + 3] = colors[idx][3];
                        }
                        // skip remaining bits in this row if width not multiple of 4
                        for (int px = maxPx; px < 4; ++px)
                            bits >>= 2;
                    }
                }
                else if (isDXT3)
                {
                    const auto* blk = reinterpret_cast<const DXT3Block*>(srcData);
                    srcData += sizeof(DXT3Block);

                    // Decode 4-bit explicit alpha
                    for (int i = 0; i < 4; ++i)
                    {
                        uint16_t a = blk->alpha[i];
                        blockAlpha[i * 4 + 0] = ((a >> 0) & 0xF) * 17;
                        blockAlpha[i * 4 + 1] = ((a >> 4) & 0xF) * 17;
                        blockAlpha[i * 4 + 2] = ((a >> 8) & 0xF) * 17;
                        blockAlpha[i * 4 + 3] = ((a >> 12) & 0xF) * 17;
                    }

                    rgb565to888(blk->color_0, colors[0][0], colors[0][1], colors[0][2]);
                    rgb565to888(blk->color_1, colors[1][0], colors[1][1], colors[1][2]);
                    for (int c = 0; c < 3; ++c)
                    {
                        colors[2][c] = (2 * colors[0][c] + colors[1][c] + 1) / 3;
                        colors[3][c] = (colors[0][c] + 2 * colors[1][c] + 1) / 3;
                    }

                    uint32_t bits = blk->texels;
                    const int maxPy = std::min(4, h - by * 4);
                    const int maxPx = std::min(4, w - bx * 4);
                    for (int py = 0; py < maxPy; ++py)
                    {
                        unsigned char* row = dstData + (by * 4 + py) * rowBytes + bx * 4 * bpp;
                        for (int px = 0; px < maxPx; ++px)
                        {
                            int idx = bits & 3;
                            bits >>= 2;
                            row[px * bpp + 0] = colors[idx][0];
                            row[px * bpp + 1] = colors[idx][1];
                            row[px * bpp + 2] = colors[idx][2];
                            if (hasAlpha)
                                row[px * bpp + 3] = blockAlpha[py * 4 + px];
                        }
                        for (int px = maxPx; px < 4; ++px)
                            bits >>= 2;
                    }
                }
                else // DXT5
                {
                    const auto* blk = reinterpret_cast<const DXT5Block*>(srcData);
                    srcData += sizeof(DXT5Block);

                    // Decode interpolated alpha
                    uint8_t alphaTable[8];
                    alphaTable[0] = blk->alpha_0;
                    alphaTable[1] = blk->alpha_1;
                    if (blk->alpha_0 > blk->alpha_1)
                    {
                        alphaTable[2] = (6 * alphaTable[0] + 1 * alphaTable[1] + 3) / 7;
                        alphaTable[3] = (5 * alphaTable[0] + 2 * alphaTable[1] + 3) / 7;
                        alphaTable[4] = (4 * alphaTable[0] + 3 * alphaTable[1] + 3) / 7;
                        alphaTable[5] = (3 * alphaTable[0] + 4 * alphaTable[1] + 3) / 7;
                        alphaTable[6] = (2 * alphaTable[0] + 5 * alphaTable[1] + 3) / 7;
                        alphaTable[7] = (1 * alphaTable[0] + 6 * alphaTable[1] + 3) / 7;
                    }
                    else
                    {
                        alphaTable[2] = (4 * alphaTable[0] + 1 * alphaTable[1] + 2) / 5;
                        alphaTable[3] = (3 * alphaTable[0] + 2 * alphaTable[1] + 2) / 5;
                        alphaTable[4] = (2 * alphaTable[0] + 3 * alphaTable[1] + 2) / 5;
                        alphaTable[5] = (1 * alphaTable[0] + 4 * alphaTable[1] + 2) / 5;
                        alphaTable[6] = 0;
                        alphaTable[7] = 255;
                    }

                    // 48 bits of alpha indices (3 bits each, 16 pixels)
                    uint64_t alphaBits = 0;
                    for (int i = 0; i < 6; ++i)
                        alphaBits |= static_cast<uint64_t>(blk->alphaIdx[i]) << (8 * i);
                    for (int i = 0; i < 16; ++i)
                    {
                        blockAlpha[i] = alphaTable[(alphaBits >> (3 * i)) & 7];
                    }

                    rgb565to888(blk->color_0, colors[0][0], colors[0][1], colors[0][2]);
                    rgb565to888(blk->color_1, colors[1][0], colors[1][1], colors[1][2]);
                    for (int c = 0; c < 3; ++c)
                    {
                        colors[2][c] = (2 * colors[0][c] + colors[1][c] + 1) / 3;
                        colors[3][c] = (colors[0][c] + 2 * colors[1][c] + 1) / 3;
                    }

                    uint32_t bits = blk->texels;
                    const int maxPy = std::min(4, h - by * 4);
                    const int maxPx = std::min(4, w - bx * 4);
                    for (int py = 0; py < maxPy; ++py)
                    {
                        unsigned char* row = dstData + (by * 4 + py) * rowBytes + bx * 4 * bpp;
                        for (int px = 0; px < maxPx; ++px)
                        {
                            int idx = bits & 3;
                            bits >>= 2;
                            row[px * bpp + 0] = colors[idx][0];
                            row[px * bpp + 1] = colors[idx][1];
                            row[px * bpp + 2] = colors[idx][2];
                            if (hasAlpha)
                                row[px * bpp + 3] = blockAlpha[py * 4 + px];
                        }
                        for (int px = maxPx; px < 4; ++px)
                            bits >>= 2;
                    }
                }
            }
        }
    }
#endif

}

namespace Resource
{

    ImageManager::ImageManager(const VFS::Manager* vfs, double expiryDelay)
        : ResourceManager(vfs, expiryDelay)
        , mWarningImage(createWarningImage())
        , mOptions(new osgDB::Options("dds_flip dds_dxt1_detect_rgba ignoreTga2Fields"))
        , mOptionsNoFlip(new osgDB::Options("dds_dxt1_detect_rgba ignoreTga2Fields"))
    {
    }

    ImageManager::~ImageManager() {}

    bool checkSupported(osg::Image* image)
    {
        switch (image->getPixelFormat())
        {
            case (GL_COMPRESSED_RGB_S3TC_DXT1_EXT):
            case (GL_COMPRESSED_RGBA_S3TC_DXT1_EXT):
            case (GL_COMPRESSED_RGBA_S3TC_DXT3_EXT):
            case (GL_COMPRESSED_RGBA_S3TC_DXT5_EXT):
            {
                if (!SceneUtil::glExtensionsReady())
                    return true; // hashtag yolo (CS might not have context when loading assets)
                osg::GLExtensions& exts = SceneUtil::getGLExtensions();
                if (!exts.isTextureCompressionS3TCSupported
                    // This one works too. Should it be included in isTextureCompressionS3TCSupported()? Submitted as a
                    // patch to OSG.
                    && !osg::isGLExtensionSupported(exts.contextID, "GL_S3_s3tc"))
                {
                    return false;
                }
                break;
            }
            // not bothering with checks for other compression formats right now
            default:
                return true;
        }
        return true;
    }

    osg::ref_ptr<osg::Image> ImageManager::getImage(
        VFS::Path::NormalizedView path, bool disableFlip, bool skipNswCache)
    {
        osg::ref_ptr<osg::Object> obj = mCache->getRefFromObjectCache(path);
        if (obj)
            return osg::ref_ptr<osg::Image>(static_cast<osg::Image*>(obj.get()));
        else
        {
#ifndef __3DS__
            (void)skipNswCache;
#else
            // Preprocessed pre-swizzled cache takes priority over the DDS.
            // The bake targets WORLD textures: it assumes the dds_flip
            // orientation and HALVES / POT-resizes everything. It must not
            // serve consumers that (a) expect the opposite V direction
            // (disableFlip=true) or (b) do pixel-space UV math against the
            // source dimensions (skipNswCache=true — MyGUI skins: menu
            // buttons, the logo and journal art rendered cropped-and-zoomed
            // off the halved bake otherwise).
            if (!disableFlip && !skipNswCache)
            {
                if (osg::ref_ptr<osg::Image> nsw = tryLoadNsw(path))
                {
                    nsw->setFileName(std::string(path.value()));
                    mCache->addEntryToObjectCache(path.value(), nsw);
                    return nsw;
                }
            }
#endif
            Files::IStreamPtr stream;
            try
            {
                stream = mVFS->get(path);
            }
            catch (std::exception& e)
            {
                Log(Debug::Error) << "Failed to open image: " << e.what();
                mCache->addEntryToObjectCache(path.value(), mWarningImage);
                return mWarningImage;
            }

            const std::string ext(Misc::getFileExtension(path.value()));
            osgDB::ReaderWriter* reader = osgDB::Registry::instance()->getReaderWriterForExtension(ext);
            if (!reader)
            {
                Log(Debug::Error) << "Error loading " << path << ": no readerwriter for '" << ext << "' found";
                mCache->addEntryToObjectCache(path.value(), mWarningImage);
                return mWarningImage;
            }

            bool killAlpha = false;
            if (reader->supportedExtensions().count("tga"))
            {
                // Morrowind ignores the alpha channel of 16bpp TGA files even when the header says not to
                unsigned char header[18];
                stream->read((char*)header, 18);
                if (stream->gcount() != 18)
                {
                    Log(Debug::Error) << "Error loading " << path << ": couldn't read TGA header";
                    mCache->addEntryToObjectCache(path.value(), mWarningImage);
                    return mWarningImage;
                }
                int type = header[2];
                int depth;
                if (type == 1 || type == 9)
                    depth = header[7];
                else
                    depth = header[16];
                int alphaBPP = header[17] & 0x0F;
                killAlpha = depth == 16 && alphaBPP == 1;
                stream->seekg(0);
            }

            osgDB::ReaderWriter::ReadResult result
                = reader->readImage(*stream, disableFlip ? mOptionsNoFlip : mOptions);
            if (!result.success())
            {
                Log(Debug::Error) << "Error loading " << path << ": " << result.message() << " code "
                                  << result.status();
                mCache->addEntryToObjectCache(path.value(), mWarningImage);
                return mWarningImage;
            }

            osg::ref_ptr<osg::Image> image = result.getImage();

#if defined(__vita__) || defined(__3DS__)
            // Decompress DXT textures to RGBA on Vita.
            // vitaGL's glCompressedTexImage2D has partial DXT support and our
            // glCompressedTexSubImage2D is a no-op stub — some textures render
            // as solid green (uninitialized VRAM). CPU decompression is safe.
#if 0 // FIXME(3DS): mip-drop pre-pass hangs initUI with full content —
      // offset/size math needs a debugger pass. The decompress+downscale
      // path below is correct (just slower), so this stays disabled.
            // Pre-pass: BSA DDS textures ship full mip chains. Before the CPU
            // decompression below inflates level 0 to RGBA (heap) and the
            // downsampler shrinks it again, just drop the top compressed mip
            // levels — same result, a fraction of the work and peak memory.
            // 256 is a safety ceiling; the exact tier cap still applies later.
            if (image->isCompressed() && image->getNumMipmapLevels() > 1
                && (image->s() > 256 || image->t() > 256))
            {
                const unsigned int levels = image->getNumMipmapLevels();
                unsigned int drop = 0;
                int ms = image->s(), mt = image->t();
                while ((ms > 256 || mt > 256) && ms > 4 && mt > 4 && drop + 1 < levels)
                {
                    ms = std::max(ms / 2, 1);
                    mt = std::max(mt / 2, 1);
                    ++drop;
                }
                if (drop > 0)
                {
                    const osg::Image::MipmapDataType offsets = image->getMipmapLevels();
                    const unsigned int base = offsets[drop - 1];
                    const unsigned int total = image->getTotalSizeInBytesIncludingMipmaps();
                    const unsigned int newSize = total - base;
                    unsigned char* newData = new unsigned char[newSize];
                    std::memcpy(newData, image->data() + base, newSize);

                    osg::Image::MipmapDataType newOffsets;
                    for (unsigned int i = drop; i + 1 < levels; ++i)
                        newOffsets.push_back(offsets[i] - base);

                    image->setImage(ms, mt, image->r(), image->getInternalTextureFormat(),
                        image->getPixelFormat(), image->getDataType(), newData, osg::Image::USE_NEW_DELETE,
                        image->getPacking());
                    image->setMipmapLevels(newOffsets);
                }
            }
#endif

            // 3DS: track whether we inflate this image to an RGBA/RGB CPU copy.
            // Those decompressed buffers are the single biggest resident heap
            // pool (~15-18MB), and setUnRefImageDataAfterApply frees them after
            // GPU upload ONLY if the ImageManager cache is not still holding a
            // ref — so on 3DS we deliberately DO NOT cache the inflated copies.
            // Small ETC1 (.ctex) and already-RGB images stay cached for dedup.
            bool ctrInflatedRGBA = false;

            if (image->isCompressed())
            {
                GLenum fmt = image->getPixelFormat();
                bool isDXT = (fmt == GL_COMPRESSED_RGB_S3TC_DXT1_EXT
                           || fmt == GL_COMPRESSED_RGBA_S3TC_DXT1_EXT
                           || fmt == GL_COMPRESSED_RGBA_S3TC_DXT3_EXT
                           || fmt == GL_COMPRESSED_RGBA_S3TC_DXT5_EXT);

                ctrInflatedRGBA = true;
                osg::ref_ptr<osg::Image> newImage = new osg::Image;
                newImage->setFileName(image->getFileName());
                GLenum outFmt = image->isImageTranslucent() ? GL_RGBA : GL_RGB;
                newImage->allocateImage(image->s(), image->t(), image->r(), outFmt, GL_UNSIGNED_BYTE);

                if (isDXT && image->r() == 1)
                    decompressDXT(image.get(), newImage.get());
                else
                {
                    // Fallback for non-DXT compressed or 3D textures
                    for (int s = 0; s < image->s(); ++s)
                        for (int t = 0; t < image->t(); ++t)
                            for (int r = 0; r < image->r(); ++r)
                                newImage->setColor(image->getColor(s, t, r), s, t, r);
                }
                image = newImage;
            }

            // Cap world textures; skip UI/normal/spec; atlases get a higher
            // cap so individual tiles inside stay usable.
            //
            // Caps come from Settings::general().mVitaTextureDetail. Tuned at
            // boot from the in-game Vita Settings tab; "off" disables the cap
            // entirely. We deliberately re-read the setting on every image
            // load rather than cache: the value is rarely changed mid-session
            // (restart-required UX), the lookup is O(log n) on a string, and
            // skipping the cache avoids any "old textures still using prior
            // cap" surprise after a runtime change.
            {
                std::string_view p = path.value();
                bool isUI = p.find("menu") != std::string_view::npos
                    || p.find("cursor") != std::string_view::npos
                    || p.find("font") != std::string_view::npos
                    || p.find("bookart") != std::string_view::npos
                    || p.find("levelup") != std::string_view::npos
                    || p.find("scroll") != std::string_view::npos
                    || p.find("splash") != std::string_view::npos;

                bool isNormalSpec = p.find("_n.") != std::string_view::npos
                    || p.find("_nm.") != std::string_view::npos
                    || p.find("_s.") != std::string_view::npos;

                bool isAtlas = p.find("/atl/") != std::string_view::npos;

                int worldCap = 64, atlasCap = 512; // performance preset
                bool downsampleEnabled = true;
                const std::string& preset = Settings::general().mVitaTextureDetail.get();
                if (preset == "balanced") { worldCap = 128; atlasCap = 768; }
                else if (preset == "high") { worldCap = 256; atlasCap = 1024; }
                else if (preset == "off")  { downsampleEnabled = false; }

#ifdef __3DS__
                // 3DS: UI textures are NOT exempt — 1024px menu/splash art is
                // 4MB of staging + 4MB of linear heap apiece on a machine with
                // ~110MB total. 256px still looks fine on a 400x240 LCD, and
                // the UI set is worth ~10MB at 512.
                int uiCap = 256;
#endif

                // Dynamic tier-down: when heap pressure is high, clamp NEW
                // texture loads below what the user selected. Existing
                // cached textures hold their size until cache expiry; over
                // a few seconds of high pressure the texture mix shifts
                // toward what the heap can afford. The thresholds sit just
                // below the MemWatchdog flush trigger (budget - 20 =
                // 244 MB on the 320 MB heap config) so tier-down kicks in
                // BEFORE the watchdog fires, easing pressure pre-emptively.
                // "off" stays off — opt-in users get their full textures.
                if (downsampleEnabled)
                {
                    const int heapMB = getCachedHeapUsedMB();
#ifdef __3DS__
                    // 3DS arena is ~110MB total; thresholds scaled accordingly.
                    if (heapMB > 85)
                    {
                        worldCap = std::min(worldCap, 32);
                        atlasCap = std::min(atlasCap, 256);
                        uiCap = 256;
                    }
                    else if (heapMB > 70)
                    {
                        worldCap = std::min(worldCap, 64);
                        atlasCap = std::min(atlasCap, 512);
                    }
#else
                    if (heapMB > 270)
                    {
                        // Emergency: clamp to performance caps regardless
                        // of preset. Same numbers as the "performance"
                        // tier above.
                        worldCap = std::min(worldCap, 64);
                        atlasCap = std::min(atlasCap, 512);
                    }
                    else if (heapMB > 240)
                    {
                        // Moderate pressure: clamp to balanced caps.
                        worldCap = std::min(worldCap, 128);
                        atlasCap = std::min(atlasCap, 768);
                    }
#endif
                }

#ifdef __3DS__
                const int maxEdge = isUI ? uiCap : (isAtlas ? atlasCap : worldCap);
                const bool sizeExempt = isNormalSpec; // UI capped too on 3DS
#else
                const int maxEdge = isAtlas ? atlasCap : worldCap;
                const bool sizeExempt = isUI || isNormalSpec;
#endif
                if (downsampleEnabled && !sizeExempt && !image->isCompressed()
                    && image->s() > 1 && image->t() > 1)
                {
                    int s = image->s(), t = image->t();
                    while ((s > maxEdge || t > maxEdge) && s > 1 && t > 1)
                    {
                        s = std::max(s / 2, 1);
                        t = std::max(t / 2, 1);
                    }
                    if (s != image->s() || t != image->t())
                        image->scaleImage(s, t, image->r());
                }
            }
#endif

            image->setFileName(std::string(path.value()));
            if (!checkSupported(image))
            {
                static bool uncompress = (getenv("OPENMW_DECOMPRESS_TEXTURES") != nullptr);
                if (!uncompress)
                {
                    Log(Debug::Error) << "Error loading " << path << ": no S3TC texture compression support installed";
                    mCache->addEntryToObjectCache(path.value(), mWarningImage);
                    return mWarningImage;
                }
                else
                {
                    // decompress texture in software if not supported by GPU
                    // requires update to getColor() to be released with OSG 3.6
                    ctrInflatedRGBA = true;
                    osg::ref_ptr<osg::Image> newImage = new osg::Image;
                    newImage->setFileName(image->getFileName());
                    newImage->allocateImage(image->s(), image->t(), image->r(),
                        image->isImageTranslucent() ? GL_RGBA : GL_RGB, GL_UNSIGNED_BYTE);
                    for (int s = 0; s < image->s(); ++s)
                        for (int t = 0; t < image->t(); ++t)
                            for (int r = 0; r < image->r(); ++r)
                                newImage->setColor(image->getColor(s, t, r), s, t, r);
                    image = newImage;
                }
            }
            else if (killAlpha)
            {
                osg::ref_ptr<osg::Image> newImage = new osg::Image;
                newImage->setFileName(image->getFileName());
                newImage->allocateImage(image->s(), image->t(), image->r(), GL_RGB, GL_UNSIGNED_BYTE);
                // OSG just won't write the alpha as there's nowhere to put it.
                for (int s = 0; s < image->s(); ++s)
                    for (int t = 0; t < image->t(); ++t)
                        for (int r = 0; r < image->r(); ++r)
                            newImage->setColor(image->getColor(s, t, r), s, t, r);
                image = newImage;
            }

#ifdef __3DS__
            // Do NOT cache inflated RGBA copies: caching pins them resident and
            // defeats setUnRefImageDataAfterApply. Uncached, the copy is freed
            // right after the texture uploads. Cost: a shared texture re-reads +
            // re-decompresses per referencing node during a cell load, but the
            // resident heap saving (~15-18MB) is what keeps us under the ceiling.
            // Once .ctex/ETC1 covers the content set, this branch rarely fires.
            if (ctrInflatedRGBA)
                return image;
#endif
            mCache->addEntryToObjectCache(path.value(), image);
            return image;
        }
    }

    osg::Image* ImageManager::getWarningImage()
    {
        return mWarningImage;
    }

    void ImageManager::reportStats(unsigned int frameNumber, osg::Stats* stats) const
    {
        Resource::reportStats("Image", frameNumber, mCache->getStats(), *stats);
    }

}
