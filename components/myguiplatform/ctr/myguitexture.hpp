#ifndef OPENMW_COMPONENTS_MYGUIPLATFORM_CTR_MYGUITEXTURE_H
#define OPENMW_COMPONENTS_MYGUIPLATFORM_CTR_MYGUITEXTURE_H

#include <cstdint>
#include <string>
#include <vector>

#include <MyGUI_ITexture.h>

#include <components/ctr/gpu/ctrgpu.hpp>

namespace MyGUIPlatform
{
    class OSGTexture final : public MyGUI::ITexture
    {
    public:
        explicit OSGTexture(const std::string& name, void* unused = nullptr);
        explicit OSGTexture(CtrGpu::TexturePtr texture, int width = 0, int height = 0);
        ~OSGTexture() override;

        const std::string& getName() const override { return mName; }

        void createManual(int width, int height, MyGUI::TextureUsage usage, MyGUI::PixelFormat format) override;
        void loadFromFile(const std::string& fname) override;
        void saveToFile(const std::string& fname) override;

        void destroy() override;

        void* lock(MyGUI::TextureUsage access) override;
        void unlock() override;
        bool isLocked() const override { return mLocked; }

        int getWidth() const override { return mWidth; }
        int getHeight() const override { return mHeight; }

        MyGUI::PixelFormat getFormat() const override { return mFormat; }
        MyGUI::TextureUsage getUsage() const override { return mUsage; }
        size_t getNumElemBytes() const override { return mNumElemBytes; }

        MyGUI::IRenderTarget* getRenderTarget() override { return nullptr; }
        void setShader(const std::string& shaderName) override {}

        /*internal:*/
        CtrGpu::Texture* getTexture() const { return mTexture.get(); }
        void setTexture(CtrGpu::TexturePtr texture, int width, int height);

        static constexpr int sMaxFileTextureSize = 512;

    private:
        std::string mName;
        CtrGpu::TexturePtr mTexture;
        std::vector<std::uint8_t> mLockBuffer;
        MyGUI::PixelFormat mFormat;
        MyGUI::TextureUsage mUsage;
        std::size_t mNumElemBytes = 0;
        int mWidth = 0;
        int mHeight = 0;
        bool mLocked = false;
    };
}

#endif
