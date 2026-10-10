#ifndef OPENMW_COMPONENTS_MYGUIPLATFORM_CTR_MYGUIRENDERMANAGER_H
#define OPENMW_COMPONENTS_MYGUIPLATFORM_CTR_MYGUIRENDERMANAGER_H

#include <map>
#include <memory>
#include <set>
#include <string>

#include <MyGUI_RenderManager.h>

#include <components/ctr/gpu/shaders.hpp>

namespace VFS
{
    class Manager;
}

namespace MyGUIPlatform
{
    class OSGTexture;

    class RenderManager : public MyGUI::RenderManager, public MyGUI::IRenderTarget
    {
    public:
        RenderManager(const VFS::Manager* vfs, float scalingFactor);
        ~RenderManager() override;

        void initialise();
        void shutdown();

        static RenderManager& getInstance() { return *getInstancePtr(); }
        static RenderManager* getInstancePtr()
        {
            return static_cast<RenderManager*>(MyGUI::RenderManager::getInstancePtr());
        }

        bool checkTexture(MyGUI::ITexture* texture) override;
        const MyGUI::IntSize& getViewSize() const override { return mViewSize; }
        MyGUI::VertexColourType getVertexFormat() const override { return MyGUI::VertexColourType::ColourABGR; }
        bool isFormatSupported(MyGUI::PixelFormat format, MyGUI::TextureUsage usage) override;

        MyGUI::IVertexBuffer* createVertexBuffer() override;
        void destroyVertexBuffer(MyGUI::IVertexBuffer* buffer) override;

        MyGUI::ITexture* createTexture(const std::string& name) override;
        void destroyTexture(MyGUI::ITexture* texture) override;
        MyGUI::ITexture* getTexture(const std::string& name) override;

        void update();

        void render(bool bottomScreen = false);

        void setBottomScreenLayers(std::set<std::string> layers, std::set<std::string> bothScreens = {})
        {
            mBottomLayers = std::move(layers);
            mBothScreenLayers = std::move(bothScreens);
        }
        bool hasBottomScreenLayers() const { return !mBottomLayers.empty(); }

        int getBottomScreenOffset() const;

        void begin() override {}
        void end() override {}
        void doRender(MyGUI::IVertexBuffer* buffer, MyGUI::ITexture* texture, size_t count) override;
        const MyGUI::RenderTargetInfo& getInfo() const override { return mInfo; }

        void setViewSize(int width, int height) override;

        void registerShader(const std::string& shaderName, const std::string& vertexProgramFile,
            const std::string& fragmentProgramFile) override;

        void setAdditiveBlend(bool additive);

        const VFS::Manager* getVFS() const { return mVFS; }

    private:
        void applyState();

        const VFS::Manager* mVFS;
        MyGUI::IntSize mViewSize;
        MyGUI::RenderTargetInfo mInfo;
        std::map<std::string, OSGTexture> mTextures;
        float mInvScalingFactor;
        bool mUpdate = false;
        bool mUpdateThisFrame = false;
        bool mBottomPass = false;
        std::set<std::string> mBottomLayers;
        std::set<std::string> mBothScreenLayers;
        bool mIsInitialise = false;
        bool mAdditive = false;
        bool mStateApplied = false;
        CtrGpu::ShaderProgram mShader;
        int mProjectionLocation = -1;
        unsigned long mLastTime = 0;
    };
}

#endif
