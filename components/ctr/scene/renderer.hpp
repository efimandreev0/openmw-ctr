#ifndef OPENMW_COMPONENTS_CTR_SCENE_RENDERER_H
#define OPENMW_COMPONENTS_CTR_SCENE_RENDERER_H

#include <cstdint>
#include <vector>

#include <osg/BoundingSphere>
#include <osg/Matrixf>
#include <osg/Vec3f>
#include <osg/Vec4f>

#include "model.hpp"

namespace CtrScene
{
    struct PointLight
    {
        osg::Vec3f mPosition; ///< world space
        osg::Vec3f mColor;
        float mRadius = 0.f;
    };

    constexpr int sMaxLightsPerDraw = 4;

    struct SceneParams
    {
        osg::Matrixf mView; ///< world to view (OpenGL convention: looking down -Z)
        float mFovY = 60.f; ///< degrees
        float mNear = 5.f;
        float mFar = 7168.f;
        osg::Vec3f mAmbient{ 0.3f, 0.3f, 0.3f };
        osg::Vec3f mSunDirection{ 0.f, 0.f, 1.f }; ///< world space, towards the sun
        osg::Vec3f mSunColor{ 1.f, 1.f, 1.f };
        osg::Vec4f mFogColor{ 0.5f, 0.5f, 0.5f, 1.f };
        float mFogStart = 2048.f;
        float mFogEnd = 7168.f;
        osg::Vec4f mClearColor{ 0.5f, 0.5f, 0.5f, 1.f };
        bool mFog = true;
        bool mClearColorBuffer = true;
        float mTime = 0.f;
        bool mParticles = true;

        C3D_RenderTarget* mTarget = nullptr;
        float mOrthoHalfSize = 0.f;
        float mTargetAspect = 1.f;
    };

    struct DrawParams
    {
        float mAlpha = 1.f;
        bool mFullAmbient = false;
        const PointLight* mLights[sMaxLightsPerDraw] = {};
        int mNumLights = 0;
        float mUvOffsetU = 0.f;
        float mUvOffsetV = 0.f;
        const CtrGpu::Texture* mTextureOverride = nullptr;
    };

    struct RenderStats
    {
        std::uint32_t mDraws = 0;
        std::uint32_t mTriangles = 0;
        std::uint32_t mCulled = 0;
    };

    class Renderer
    {
    public:
        Renderer();
        ~Renderer();

        bool init();
        void shutdown();

        void beginScene(const SceneParams& params);
        void endScene();

        void setFirstPersonDepth(bool firstPerson);

        void drawMesh(const Mesh& mesh, const osg::Matrixf& world, const DrawParams& params);

        void drawSkinned(
            const Mesh& mesh, const osg::Matrixf* bones, const osg::Matrixf& object, const DrawParams& params);

        bool isVisible(const osg::BoundingSpheref& sphere) const;
        void setCullingEnabled(bool enabled) { mCullingEnabled = enabled; }
        bool isCullingEnabled() const { return mCullingEnabled; }
        void setDebugNoFog(bool enabled) { mDebugNoFog = enabled; }
        bool getDebugNoFog() const { return mDebugNoFog; }
        void setDebugNoTextures(bool enabled) { mDebugNoTextures = enabled; }
        bool getDebugNoTextures() const { return mDebugNoTextures; }
        void setDebugTwoSided(bool enabled) { mDebugTwoSided = enabled; }
        bool getDebugTwoSided() const { return mDebugTwoSided; }

        const SceneParams& getSceneParams() const { return mScene; }
        const RenderStats& getStats() const { return mStats; }
        const RenderStats& getScreenStats() const { return mScreenStats; }

        bool project(const osg::Vec3f& world, float& x, float& y) const;

    private:
        struct Program;
        struct QueuedDraw
        {
            const Mesh* mMesh;
            int mPart; ///< -1 for rigid
            float mDepth;
            std::uint32_t mMatrices; ///< offset into mArena (1 matrix for rigid, one per part bone for skinned)
            DrawParams mParams;
        };

        bool loadProgram(Program& program, const unsigned char* data, std::size_t size);
        void bindProgram(Program& program);
        void applyMaterial(const Material& material, const DrawParams& params, Program& program, bool blended);
        void setVertexLayout(std::uint8_t format, const void* vertices);
        void setLights(Program& program, const DrawParams& params);
        void drawRigidNow(const Mesh& mesh, const osg::Matrixf& modelView, const DrawParams& params);
        void drawPartNow(const Mesh& mesh, const SkinPart& part, const osg::Matrixf* modelView,
            const DrawParams& params);

        Program* mWorld;
        Program* mSkin;
        Program* mCurrentProgram = nullptr;
        bool mInitialised = false;

        SceneParams mScene;
        RenderStats mStats;
        RenderStats mScreenStats;
        bool mCullingEnabled = true;
        struct LightCache
        {
            const PointLight* mLights[sMaxLightsPerDraw] = {};
            int mCount = 0;
            bool mFullAmbient = false;
            bool operator==(const LightCache& other) const
            {
                if (mCount != other.mCount || mFullAmbient != other.mFullAmbient)
                    return false;
                for (int i = 0; i < mCount; ++i)
                    if (mLights[i] != other.mLights[i])
                        return false;
                return true;
            }
        };
        LightCache mLightCache;
        bool mLightCacheValid = false;
        const Material* mMaterialCache = nullptr;
        float mMaterialCacheAlpha = 1.f;
        bool mMaterialCacheFullAmbient = false;
        bool mMaterialCacheValid = false;
        C3D_FogLut mFogLut;
        bool mDebugNoFog = false;
        bool mDebugNoTextures = false;
        bool mDebugTwoSided = false;
        osg::Vec4f mFrustum[4]; ///< view-space side planes (normal, distance)
        bool mFlipWinding = false;
        osg::Vec3f mSunView;
        std::vector<QueuedDraw> mQueue;
        std::vector<osg::Matrixf> mArena;

        const CtrGpu::Texture* mBoundTexture = nullptr;
        std::uint32_t mStateKey = 0xffffffff;
        std::uint8_t mBoundFormat = 0xff;
    };
}

#endif
