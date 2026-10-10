#include "myguirendermanager.hpp"

#include <3ds.h>
#include <citro3d.h>

#include <MyGUI_Gui.h>
#include <MyGUI_ILayer.h>
#include <MyGUI_LayerManager.h>
#include <MyGUI_Timer.h>

#include <components/ctr/gpu/ctrgpu.hpp>
#include <components/debug/debuglog.hpp>

#include <algorithm>
#include <stdexcept>

#include "myguitexture.hpp"


namespace MyGUIPlatform
{
    namespace
    {
        class CtrVertexBuffer final : public MyGUI::IVertexBuffer
        {
        public:
            ~CtrVertexBuffer() override { CtrGpu::deferLinearFree(mData); }

            void setVertexCount(size_t count) override { mCount = count; }
            size_t getVertexCount() const override { return mCount; }

            MyGUI::Vertex* lock() override
            {
                const bool inFlight = mData != nullptr && mUsedFrame == CtrGpu::getFrameNumber();
                if (mData == nullptr || mCapacity < mCount || inFlight)
                {
                    CtrGpu::deferLinearFree(mData);
                    mCapacity = std::max<size_t>(mCount, 6);
                    mData = static_cast<MyGUI::Vertex*>(
                        CtrGpu::linearAllocChecked(mCapacity * sizeof(MyGUI::Vertex)));
                    if (mData == nullptr)
                    {
                        mFallback.resize(mCount);
                        mCapacity = 0;
                        return mFallback.data();
                    }
                }
                return mData;
            }

            void unlock() override
            {
                mFallback.clear();
            }

            const MyGUI::Vertex* data() const { return mCapacity != 0 ? mData : nullptr; }
            void markUsed() { mUsedFrame = CtrGpu::getFrameNumber(); }

        private:
            MyGUI::Vertex* mData = nullptr;
            size_t mCount = 0;
            size_t mCapacity = 0;
            std::uint32_t mUsedFrame = ~0u;
            std::vector<MyGUI::Vertex> mFallback;
        };
    }

    RenderManager::RenderManager(const VFS::Manager* vfs, float scalingFactor)
        : mVFS(vfs)
        , mInvScalingFactor(scalingFactor > 0.f ? 1.f / scalingFactor : 1.f)
    {
    }

    RenderManager::~RenderManager()
    {
        mTextures.clear();
        mShader.unload();
        mIsInitialise = false;
    }

    void RenderManager::initialise()
    {
        if (mIsInitialise)
            throw std::runtime_error("MyGUI render manager initialised twice");

        if (mShader.load(ctr_shader_ui, ctr_shader_ui_size))
            mProjectionLocation = mShader.uniform("projection");

        setViewSize(CtrGpu::sTopWidth, CtrGpu::sTopHeight);
        mLastTime = MyGUI::Timer().getMilliseconds();
        mIsInitialise = true;
    }

    void RenderManager::shutdown() {}

    MyGUI::IVertexBuffer* RenderManager::createVertexBuffer()
    {
        return new CtrVertexBuffer();
    }

    void RenderManager::destroyVertexBuffer(MyGUI::IVertexBuffer* buffer)
    {
        delete buffer;
    }

    void RenderManager::applyState()
    {
        C3D_BindProgram(&mShader.mProgram);

        C3D_AttrInfo* attrInfo = C3D_GetAttrInfo();
        AttrInfo_Init(attrInfo);
        AttrInfo_AddLoader(attrInfo, 0, GPU_FLOAT, 3); // x, y, z
        AttrInfo_AddLoader(attrInfo, 1, GPU_UNSIGNED_BYTE, 4); // colour (ABGR in a u32 = R, G, B, A bytes)
        AttrInfo_AddLoader(attrInfo, 2, GPU_FLOAT, 2); // u, v

        float halfWidth = 1.f;
        if (mBottomPass && mViewSize.width > 0)
            halfWidth = std::min(1.f,
                static_cast<float>(CtrGpu::sBottomWidth) * mInvScalingFactor / static_cast<float>(mViewSize.width));
        C3D_Mtx projection;
        Mtx_OrthoTilt(&projection, -halfWidth, halfWidth, -1.f, 1.f, -1.f, 1.f, true);
        if (mProjectionLocation >= 0)
            C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, mProjectionLocation, &projection);

        C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_COLOR);
        C3D_CullFace(GPU_CULL_NONE);
        C3D_AlphaTest(false, GPU_ALWAYS, 0);
        C3D_StencilTest(false, GPU_ALWAYS, 0, 0, 0);
        C3D_FogGasMode(GPU_NO_FOG, GPU_PLAIN_DENSITY, false);
        C3D_LightEnvBind(nullptr);

        C3D_TexEnv* env = C3D_GetTexEnv(0);
        C3D_TexEnvInit(env);
        C3D_TexEnvSrc(env, C3D_Both, GPU_TEXTURE0, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR);
        C3D_TexEnvFunc(env, C3D_Both, GPU_MODULATE);
        for (int i = 1; i < 6; ++i)
            C3D_TexEnvInit(C3D_GetTexEnv(i));

        mStateApplied = true;
        mAdditive = !mAdditive; // force the blend state below
        setAdditiveBlend(!mAdditive);
    }

    void RenderManager::setAdditiveBlend(bool additive)
    {
        if (additive == mAdditive && mStateApplied)
            return;
        mAdditive = additive;
        if (additive)
            C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE, GPU_SRC_ALPHA, GPU_ONE);
        else
            C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA, GPU_ONE,
                GPU_ONE_MINUS_SRC_ALPHA);
    }

    void RenderManager::doRender(MyGUI::IVertexBuffer* buffer, MyGUI::ITexture* texture, size_t count)
    {
        if (!mShader.mValid || count == 0)
            return;
        CtrVertexBuffer* vb = static_cast<CtrVertexBuffer*>(buffer);
        const MyGUI::Vertex* data = vb->data();
        if (data == nullptr || !CtrGpu::reserveDraw())
            return;
        if (!mStateApplied)
            applyState();

        CtrGpu::Texture* tex = nullptr;
        if (texture != nullptr)
            tex = static_cast<OSGTexture*>(texture)->getTexture();
        if (tex == nullptr || !tex->valid())
            tex = &CtrGpu::getWhiteTexture();
        C3D_TexBind(0, tex->get());

        C3D_BufInfo* bufInfo = C3D_GetBufInfo();
        BufInfo_Init(bufInfo);
        BufInfo_Add(bufInfo, data, sizeof(MyGUI::Vertex), 3, 0x210);

        vb->markUsed();
        C3D_DrawArrays(GPU_TRIANGLES, 0, static_cast<int>(count));
    }

    void RenderManager::update()
    {
        static MyGUI::Timer timer;
        const unsigned long now = timer.getMilliseconds();
        const unsigned long elapsed = now - mLastTime;
        mLastTime = now;
        onFrameEvent(static_cast<float>(static_cast<double>(elapsed) / 1000.0));
        mUpdateThisFrame = mUpdate;
        mUpdate = false;
    }

    void RenderManager::render(bool bottomScreen)
    {
        if (!mIsInitialise)
            return;
        mStateApplied = false;
        mBottomPass = bottomScreen;
        if (mBottomLayers.empty())
        {
            if (!bottomScreen)
                onRenderToTarget(this, mUpdateThisFrame);
            return;
        }
        MyGUI::LayerManager::EnumeratorLayer layers = MyGUI::LayerManager::getInstance().getEnumerator();
        while (layers.next())
        {
            MyGUI::ILayer* layer = layers.current();
            if (mBothScreenLayers.count(layer->getName()) != 0
                || (mBottomLayers.count(layer->getName()) != 0) == bottomScreen)
                layer->renderToTarget(this, mUpdateThisFrame);
        }
    }

    int RenderManager::getBottomScreenOffset() const
    {
        const int bottomWidth = static_cast<int>(CtrGpu::sBottomWidth * mInvScalingFactor);
        return std::max(0, (mViewSize.width - bottomWidth) / 2);
    }

    void RenderManager::setViewSize(int width, int height)
    {
        if (width < 1)
            width = 1;
        if (height < 1)
            height = 1;

        mViewSize.set(static_cast<int>(width * mInvScalingFactor), static_cast<int>(height * mInvScalingFactor));

        mInfo.maximumDepth = 1;
        mInfo.hOffset = 0;
        mInfo.vOffset = 0;
        mInfo.aspectCoef = float(mViewSize.height) / float(mViewSize.width);
        mInfo.pixScaleX = 1.0f / float(mViewSize.width);
        mInfo.pixScaleY = 1.0f / float(mViewSize.height);

        onResizeView(mViewSize);
        mUpdate = true;
    }

    bool RenderManager::isFormatSupported(MyGUI::PixelFormat /*format*/, MyGUI::TextureUsage /*usage*/)
    {
        return true;
    }

    MyGUI::ITexture* RenderManager::createTexture(const std::string& name)
    {
        const auto it = mTextures.insert_or_assign(name, OSGTexture(name)).first;
        return &it->second;
    }

    void RenderManager::destroyTexture(MyGUI::ITexture* texture)
    {
        if (texture == nullptr)
            return;
        const auto item = mTextures.find(texture->getName());
        if (item == mTextures.end())
        {
            Log(Debug::Error) << "MyGUI texture '" << texture->getName() << "' not found";
            return;
        }
        mTextures.erase(item);
    }

    MyGUI::ITexture* RenderManager::getTexture(const std::string& name)
    {
        if (name.empty())
            return nullptr;
        const auto item = mTextures.find(name);
        if (item == mTextures.end())
        {
            MyGUI::ITexture* tex = createTexture(name);
            tex->loadFromFile(name);
            return tex;
        }
        return &item->second;
    }

    bool RenderManager::checkTexture(MyGUI::ITexture* /*texture*/)
    {
        return true;
    }

    void RenderManager::registerShader(const std::string& /*shaderName*/, const std::string& /*vertexProgramFile*/,
        const std::string& /*fragmentProgramFile*/)
    {
        Log(Debug::Warning) << "MyGUI shaders are not supported on the 3DS";
    }
}
