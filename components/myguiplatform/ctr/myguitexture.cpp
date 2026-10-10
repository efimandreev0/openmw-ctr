#include "myguitexture.hpp"

#include <stdexcept>

#include <components/ctr/gpu/imagedecode.hpp>
#include <components/debug/debuglog.hpp>
#include <components/vfs/pathutil.hpp>

#include "myguirendermanager.hpp"

namespace MyGUIPlatform
{
    OSGTexture::OSGTexture(const std::string& name, void* /*unused*/)
        : mName(name)
        , mFormat(MyGUI::PixelFormat::Unknow)
        , mUsage(MyGUI::TextureUsage::Default)
    {
    }

    OSGTexture::OSGTexture(CtrGpu::TexturePtr texture, int width, int height)
        : mFormat(MyGUI::PixelFormat::Unknow)
        , mUsage(MyGUI::TextureUsage::Default)
    {
        setTexture(std::move(texture), width, height);
    }

    OSGTexture::~OSGTexture() = default;

    void OSGTexture::setTexture(CtrGpu::TexturePtr texture, int width, int height)
    {
        mTexture = std::move(texture);
        mWidth = width > 0 ? width : (mTexture ? mTexture->getWidth() : 0);
        mHeight = height > 0 ? height : (mTexture ? mTexture->getHeight() : 0);
    }

    void OSGTexture::createManual(int width, int height, MyGUI::TextureUsage usage, MyGUI::PixelFormat format)
    {
        std::size_t elems = 0;
        switch (format.getValue())
        {
            case MyGUI::PixelFormat::L8:
                elems = 1;
                break;
            case MyGUI::PixelFormat::L8A8:
                elems = 2;
                break;
            case MyGUI::PixelFormat::R8G8B8:
                elems = 3;
                break;
            case MyGUI::PixelFormat::R8G8B8A8:
                elems = 4;
                break;
        }
        if (elems == 0)
            throw std::runtime_error("Texture format not supported");
        mWidth = width;
        mHeight = height;
        mFormat = format;
        mUsage = usage;
        mNumElemBytes = elems;
        mTexture.reset();
    }

    void OSGTexture::destroy()
    {
        mTexture.reset();
        mLockBuffer.clear();
        mLockBuffer.shrink_to_fit();
        mFormat = MyGUI::PixelFormat::Unknow;
        mUsage = MyGUI::TextureUsage::Default;
        mNumElemBytes = 0;
        mWidth = 0;
        mHeight = 0;
        mLocked = false;
    }

    void OSGTexture::loadFromFile(const std::string& fname)
    {
        const VFS::Manager* vfs = RenderManager::getInstance().getVFS();
        CtrGpu::RgbaImage image;
        if (vfs == nullptr || !CtrGpu::loadImage(*vfs, VFS::Path::Normalized(fname), sMaxFileTextureSize, image))
        {
            image.mWidth = image.mHeight = 8;
            image.mPixels.assign(8 * 8 * 4, 0);
            for (std::size_t i = 0; i < image.mPixels.size(); i += 4)
            {
                image.mPixels[i] = 255;
                image.mPixels[i + 2] = 255;
                image.mPixels[i + 3] = 255;
            }
        }
        const CtrGpu::TextureFormat format
            = CtrGpu::chooseFormat(image.mPixels.data(), image.mWidth, image.mHeight);
        mTexture = CtrGpu::createTextureFromRGBA8(
            image.mPixels.data(), image.mWidth, image.mHeight, format, sMaxFileTextureSize, false);
        mWidth = image.mWidth;
        mHeight = image.mHeight;
        mUsage = MyGUI::TextureUsage::Static;
        mFormat = MyGUI::PixelFormat::R8G8B8A8;
        mNumElemBytes = 4;
    }

    void OSGTexture::saveToFile(const std::string& fname)
    {
        Log(Debug::Warning) << "Saving GUI textures is not supported on the 3DS (" << fname << ")";
    }

    void* OSGTexture::lock(MyGUI::TextureUsage /*access*/)
    {
        if (mNumElemBytes == 0)
            throw std::runtime_error("Texture is not created");
        if (mLocked)
            throw std::runtime_error("Texture already locked");
        mLockBuffer.assign(static_cast<std::size_t>(mWidth) * mHeight * mNumElemBytes, 0);
        mLocked = true;
        return mLockBuffer.data();
    }

    void OSGTexture::unlock()
    {
        if (!mLocked)
            throw std::runtime_error("Texture not locked");
        mLocked = false;

        std::vector<std::uint8_t> rgba(static_cast<std::size_t>(mWidth) * mHeight * 4);
        const std::size_t count = static_cast<std::size_t>(mWidth) * mHeight;
        for (std::size_t i = 0; i < count; ++i)
        {
            const std::uint8_t* s = &mLockBuffer[i * mNumElemBytes];
            std::uint8_t* d = &rgba[i * 4];
            switch (mNumElemBytes)
            {
                case 1:
                    d[0] = d[1] = d[2] = s[0];
                    d[3] = 255;
                    break;
                case 2:
                    d[0] = d[1] = d[2] = s[0];
                    d[3] = s[1];
                    break;
                case 3:
                    d[0] = s[0];
                    d[1] = s[1];
                    d[2] = s[2];
                    d[3] = 255;
                    break;
                default:
                    d[0] = s[0];
                    d[1] = s[1];
                    d[2] = s[2];
                    d[3] = s[3];
                    break;
            }
        }
        mLockBuffer.clear();
        mLockBuffer.shrink_to_fit();

        const CtrGpu::TextureFormat format = CtrGpu::chooseFormat(rgba.data(), mWidth, mHeight);
        mTexture = CtrGpu::createTextureFromRGBA8(rgba.data(), mWidth, mHeight, format, 1024, false);
    }
}
