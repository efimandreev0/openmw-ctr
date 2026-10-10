#include "ctrgpu.hpp"

#include <3ds.h>

#include <algorithm>
#include <cstring>
#include <map>
#include <vector>

#include <components/debug/debuglog.hpp>

extern "C" void C3Di_RenderQueueWaitDone(void);

namespace CtrGpu
{
    namespace
    {
        constexpr std::uint32_t sDisplayTransferFlagsTop = GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0)
            | GX_TRANSFER_RAW_COPY(0) | GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8)
            | GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB8) | GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO);
        constexpr std::uint32_t sDisplayTransferFlagsBottom = GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0)
            | GX_TRANSFER_RAW_COPY(0) | GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8)
            | GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB565) | GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO);

        constexpr std::size_t sCommandBufferSize = 0x100000;
        constexpr u32 sDrawReserveWords = 2048;
        constexpr std::size_t sRenderTargetVramReserve = 1024 * 1024;
        std::uint32_t sSkippedDraws = 0;
        std::uint32_t sSkippedDrawsTotal = 0;
        u32* sCommandBase = nullptr;
        u32 sCommandWords = 0;

        std::uint32_t commandWordsUsed()
        {
            if (gpuCmdBuf == nullptr || sCommandBase == nullptr)
                return 0;
            return static_cast<std::uint32_t>(gpuCmdBuf - sCommandBase) + gpuCmdBufOffset;
        }

        struct PendingLinear
        {
            void* mPtr;
            std::uint32_t mFrame;
        };

        struct PendingTexture
        {
            C3D_Tex mTex;
            std::uint32_t mFrame;
        };

        std::vector<C3D_RenderTarget*> sPendingTargets;

        bool sInitialised = false;
        bool sInFrame = false;
        std::uint32_t sFrame = 0;
        int sFrameCopies = 0;
        constexpr int sMaxFrameCopies = 8;
        C3D_RenderTarget* sTop = nullptr;
        C3D_RenderTarget* sBottom = nullptr;
        std::vector<PendingLinear> sPendingLinear;
        std::vector<PendingTexture> sPendingTextures;
        std::unique_ptr<Texture> sWhite;

        void releaseRetired(std::uint32_t before)
        {
            auto expired = [before](std::uint32_t frame) { return frame < before; };
            std::erase_if(sPendingLinear, [&](const PendingLinear& p) {
                if (!expired(p.mFrame))
                    return false;
                ::linearFree(p.mPtr);
                return true;
            });
            std::erase_if(sPendingTextures, [&](PendingTexture& p) {
                if (!expired(p.mFrame))
                    return false;
                C3D_TexDelete(&p.mTex);
                return true;
            });
        }

        GPU_TEXCOLOR toGpu(TextureFormat format)
        {
            switch (format)
            {
                case TextureFormat::RGBA8:
                    return GPU_RGBA8;
                case TextureFormat::RGB8:
                    return GPU_RGB8;
                case TextureFormat::RGBA5551:
                    return GPU_RGBA5551;
                case TextureFormat::RGB565:
                    return GPU_RGB565;
                case TextureFormat::RGBA4:
                    return GPU_RGBA4;
                case TextureFormat::LA8:
                    return GPU_LA8;
                case TextureFormat::A8:
                    return GPU_A8;
                case TextureFormat::L8:
                    return GPU_L8;
                case TextureFormat::LA4:
                    return GPU_LA4;
                case TextureFormat::A4:
                    return GPU_A4;
                case TextureFormat::ETC1:
                    return GPU_ETC1;
                case TextureFormat::ETC1A4:
                    return GPU_ETC1A4;
            }
            return GPU_RGBA8;
        }

    }

    bool init()
    {
        if (sInitialised)
            return true;
        if (!C3D_Init(sCommandBufferSize))
        {
            Log(Debug::Error) << "C3D_Init failed";
            return false;
        }
        sTop = C3D_RenderTargetCreate(sTopHeight, sTopWidth, GPU_RB_RGBA8, GPU_RB_DEPTH24_STENCIL8);
        sBottom = C3D_RenderTargetCreate(sBottomHeight, sBottomWidth, GPU_RB_RGBA8, C3D_DEPTHTYPE(-1));
        if (sTop == nullptr || sBottom == nullptr)
        {
            Log(Debug::Error) << "Failed to create the citro3d render targets";
            return false;
        }
        C3D_RenderTargetSetOutput(sTop, GFX_TOP, GFX_LEFT, sDisplayTransferFlagsTop);
        C3D_RenderTargetSetOutput(sBottom, GFX_BOTTOM, GFX_LEFT, sDisplayTransferFlagsBottom);
        sInitialised = true;
        return true;
    }

    void shutdown()
    {
        if (!sInitialised)
            return;
        sWhite.reset();
        C3Di_RenderQueueWaitDone();
        releaseRetired(~0u);
        C3D_RenderTargetDelete(sTop);
        C3D_RenderTargetDelete(sBottom);
        sTop = sBottom = nullptr;
        C3D_Fini();
        sInitialised = false;
    }

    bool isInitialised()
    {
        return sInitialised;
    }

    void beginFrame()
    {
        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
        sInFrame = true;
        sFrameCopies = 0;
        sCommandBase = gpuCmdBuf;
        sCommandWords = gpuCmdBufSize;
        ++sFrame;
        releaseRetired(sFrame);
    }

    void endFrame()
    {
        static std::uint32_t peakWords = 0;
        peakWords = std::max<std::uint32_t>(peakWords, commandWordsUsed());
        if (sFrame % 600 == 0 || (sSkippedDraws != 0 && peakWords != 0 && sFrame % 60 == 0))
        {
            if (sSkippedDraws != 0 || sFrame % 600 == 0)
                Log(Debug::Info) << "[GPU] command buffer peak " << peakWords * 100 / std::max<u32>(sCommandWords, 1)
                                 << "%, skipped draws " << sSkippedDraws;
            peakWords = 0;
            sSkippedDraws = 0;
        }
        C3D_FrameEnd(0);
        sInFrame = false;
        for (C3D_RenderTarget* target : sPendingTargets)
            C3D_RenderTargetDelete(target);
        sPendingTargets.clear();
    }

    std::uint32_t getFrameNumber()
    {
        return sFrame;
    }

    C3D_RenderTarget* getTopTarget()
    {
        return sTop;
    }

    C3D_RenderTarget* getBottomTarget()
    {
        return sBottom;
    }

    void makeScreenOrtho(C3D_Mtx& out, float width, float height)
    {
        Mtx_OrthoTilt(&out, 0.f, width, height, 0.f, 0.f, 1.f, true);
    }

    void* linearAllocChecked(std::size_t size)
    {
        void* ptr = linearAlloc(size);
        if (ptr == nullptr && sInitialised && !sInFrame)
        {
            C3Di_RenderQueueWaitDone();
            releaseRetired(~0u);
            ptr = linearAlloc(size);
        }
        if (ptr == nullptr)
            Log(Debug::Error) << "Out of linear memory allocating " << size << " bytes (" << linearSpaceFree()
                              << " free)";
        return ptr;
    }

    float commandBufferUsage()
    {
        return static_cast<float>(commandWordsUsed()) / static_cast<float>(std::max<u32>(sCommandWords, 1));
    }

    bool reserveDraw()
    {
        if (gpuCmdBuf != nullptr && gpuCmdBufSize - gpuCmdBufOffset > sDrawReserveWords)
            return true;
        ++sSkippedDraws;
        ++sSkippedDrawsTotal;
        return false;
    }

    std::uint32_t getSkippedDraws()
    {
        return sSkippedDrawsTotal;
    }

    void deferLinearFree(void* ptr)
    {
        if (ptr != nullptr)
            sPendingLinear.push_back({ ptr, sFrame });
    }

    std::size_t linearFree()
    {
        return linearSpaceFree();
    }

    Texture::~Texture()
    {
        if (mTarget != nullptr)
        {
            if (sInitialised)
                sPendingTargets.push_back(mTarget);
            else
                C3D_RenderTargetDelete(mTarget);
        }
        if (!mValid)
            return;
        if (sInitialised)
            sPendingTextures.push_back({ mTex, sFrame });
        else
            C3D_TexDelete(&mTex);
    }

    bool Texture::create(int width, int height, TextureFormat format, bool allowVram, int mipLevels)
    {
        mFormat = format;
        C3D_TexInitParams params{};
        params.width = static_cast<u16>(width);
        params.height = static_cast<u16>(height);
        params.maxLevel = static_cast<u8>(mipLevels);
        params.format = toGpu(format);
        params.type = GPU_TEX_2D;
        params.onVram = allowVram;
        mValid = C3D_TexInitWithParams(&mTex, nullptr, params);
        mVram = mValid && allowVram;
        if (!mValid && allowVram)
        {
            params.onVram = false;
            mValid = C3D_TexInitWithParams(&mTex, nullptr, params);
            mVram = false;
        }
        if (!mValid)
            Log(Debug::Error) << "Failed to allocate a " << width << "x" << height << " texture";
        else
        {
            setLinearFilter(true);
            setWrap(false);
        }
        return mValid;
    }

    std::size_t Texture::getByteSize() const
    {
        return mValid ? totalSize() : 0;
    }

    void Texture::setLinearFilter(bool linear)
    {
        const GPU_TEXTURE_FILTER_PARAM filter = linear ? GPU_LINEAR : GPU_NEAREST;
        C3D_TexSetFilter(&mTex, filter, filter);
        if (mTex.maxLevel > 0)
            C3D_TexSetFilterMipmap(&mTex, GPU_LINEAR);
    }

    void Texture::setWrap(bool repeat)
    {
        const GPU_TEXTURE_WRAP_PARAM wrap = repeat ? GPU_REPEAT : GPU_CLAMP_TO_EDGE;
        C3D_TexSetWrap(&mTex, wrap, wrap);
    }

    C3D_RenderTarget* Texture::getRenderTarget()
    {
        if (mTarget != nullptr || !mValid)
            return mTarget;
        mTarget = C3D_RenderTargetCreateFromTex(&mTex, GPU_TEXFACE_2D, 0, C3D_DEPTHTYPE(-1));
        if (mTarget == nullptr)
        {
            Log(Debug::Error) << "No render target for a " << mTex.width << "x" << mTex.height
                              << " texture (in VRAM: " << mVram << ")";
            return nullptr;
        }
        static std::map<std::uint32_t, void*> sharedDepth;
        void*& depth = sharedDepth[(static_cast<std::uint32_t>(mTex.width) << 16) | mTex.height];
        if (depth == nullptr)
            depth = vramAlloc(C3D_CalcDepthBufSize(mTex.width, mTex.height, GPU_RB_DEPTH16));
        if (depth == nullptr)
            Log(Debug::Error) << "No VRAM for a " << mTex.width << "x" << mTex.height << " depth buffer";
        else
            C3D_FrameBufDepth(&mTarget->frameBuf, depth, GPU_RB_DEPTH16);
        return mTarget;
    }

    void Texture::uploadRGBA8(const std::uint8_t* rgba)
    {
        const int width = mTex.width;
        const int height = mTex.height;
        const int levels = mTex.maxLevel;
        const TextureFormat format = mFormat;
        fillTiled(totalSize(), [&](void* dst, std::size_t) {
            buildTiledTexture(rgba, width, height, levels, format, static_cast<std::uint8_t*>(dst));
            return true;
        });
    }

    void Texture::uploadTiled(const void* data, std::size_t size)
    {
        fillTiled(size, [&](void* dst, std::size_t bytes) {
            std::memcpy(dst, data, bytes);
            return true;
        });
    }

    bool Texture::fillTiled(std::size_t size, const std::function<bool(void* dst, std::size_t size)>& fill)
    {
        if (!mValid)
            return false;
        size = std::min<std::size_t>(size, totalSize());
        if (!mVram)
        {
            if (!fill(mTex.data, size))
                return false;
            GSPGPU_FlushDataCache(mTex.data, size);
            return true;
        }
        void* staging = linearAllocChecked(size);
        if (staging == nullptr)
            return false;
        if (!fill(staging, size))
        {
            ::linearFree(staging);
            return false;
        }
        GSPGPU_FlushDataCache(staging, size);
        C3D_SyncTextureCopy(static_cast<u32*>(staging), 0, static_cast<u32*>(mTex.data), 0, size, 8);
        if (sInFrame)
        {
            if (++sFrameCopies == sMaxFrameCopies)
                Log(Debug::Warning) << "Many texture uploads inside one GPU frame";
            deferLinearFree(staging);
        }
        else
        {
            C3Di_RenderQueueWaitDone();
            ::linearFree(staging);
        }
        return true;
    }

    void flushLinear(const void* ptr, std::size_t size)
    {
        if (ptr != nullptr && size > 0)
            GSPGPU_FlushDataCache(ptr, size);
    }

    TexturePtr createTextureFromRGBA8(const std::uint8_t* rgba, int width, int height, TextureFormat format,
        int maxSize, bool allowVram, bool mipmaps)
    {
        if (rgba == nullptr || width <= 0 || height <= 0)
            return nullptr;
        const TextureLayout layout = computeTextureLayout(width, height, maxSize, mipmaps);
        std::vector<std::uint8_t> scaled;
        const std::uint8_t* source = rgba;
        if (layout.mWidth != width || layout.mHeight != height)
        {
            scaled = scaleImage(rgba, width, height, layout.mWidth, layout.mHeight);
            source = scaled.data();
        }

        auto texture = createTexture(layout.mWidth, layout.mHeight, format, layout.mMipLevels, allowVram);
        if (texture)
            texture->uploadRGBA8(source);
        return texture;
    }

    TexturePtr createTexture(int width, int height, TextureFormat format, int mipLevels, bool allowVram)
    {
        const std::size_t bytes = tiledTextureSize(width, height, mipLevels, format);
        if (allowVram && vramSpaceFree() < bytes + sRenderTargetVramReserve)
            allowVram = false;

        auto texture = std::make_shared<Texture>();
        if (!texture->create(width, height, format, allowVram, mipLevels))
            return nullptr;
        return texture;
    }

    Texture& getWhiteTexture()
    {
        if (!sWhite)
        {
            sWhite = std::make_unique<Texture>();
            std::uint8_t pixels[8 * 8 * 4];
            std::memset(pixels, 0xFF, sizeof(pixels));
            if (sWhite->create(8, 8, TextureFormat::RGBA4, false))
                sWhite->uploadRGBA8(pixels);
        }
        return *sWhite;
    }
}
