#ifndef OPENMW_COMPONENTS_CTR_GPU_CTRGPU_H
#define OPENMW_COMPONENTS_CTR_GPU_CTRGPU_H


#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>

#include <citro3d.h>

#include "texconvert.hpp"

namespace CtrGpu
{
    constexpr int sTopWidth = 400;
    constexpr int sTopHeight = 240;
    constexpr int sBottomWidth = 320;
    constexpr int sBottomHeight = 240;

    bool init();
    void shutdown();
    bool isInitialised();

    void beginFrame();
    void endFrame();
    std::uint32_t getFrameNumber();

    C3D_RenderTarget* getTopTarget();
    C3D_RenderTarget* getBottomTarget();

    void makeScreenOrtho(C3D_Mtx& out, float width, float height);

    void* linearAllocChecked(std::size_t size);
    void deferLinearFree(void* ptr);

    bool reserveDraw();
    float commandBufferUsage();
    std::uint32_t getSkippedDraws();
    void flushLinear(const void* ptr, std::size_t size);
    std::size_t linearFree();


    class Texture
    {
    public:
        Texture() = default;
        ~Texture();
        Texture(const Texture&) = delete;
        Texture& operator=(const Texture&) = delete;

        bool create(int width, int height, TextureFormat format, bool allowVram, int mipLevels = 0);

        void uploadRGBA8(const std::uint8_t* rgba);

        void uploadTiled(const void* data, std::size_t size);

        bool fillTiled(std::size_t size, const std::function<bool(void* dst, std::size_t size)>& fill);

        C3D_Tex* get() { return &mTex; }
        bool valid() const { return mValid; }
        int getWidth() const { return mTex.width; }
        int getHeight() const { return mTex.height; }
        TextureFormat getFormat() const { return mFormat; }
        bool inVram() const { return mVram; }
        std::size_t getByteSize() const;
        std::size_t totalSize() const { return C3D_TexCalcTotalSize(mTex.size, mTex.maxLevel); }

        void setLinearFilter(bool linear);
        void setWrap(bool repeat);

        C3D_RenderTarget* getRenderTarget();

    private:
        C3D_Tex mTex{};
        C3D_RenderTarget* mTarget = nullptr;
        TextureFormat mFormat = TextureFormat::RGBA8;
        bool mValid = false;
        bool mVram = false;
    };

    using TexturePtr = std::shared_ptr<Texture>;

    TexturePtr createTextureFromRGBA8(const std::uint8_t* rgba, int width, int height, TextureFormat format,
        int maxSize, bool allowVram, bool mipmaps = false);

    TexturePtr createTexture(int width, int height, TextureFormat format, int mipLevels, bool allowVram);

    Texture& getWhiteTexture();
}

#endif
