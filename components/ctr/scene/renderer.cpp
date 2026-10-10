#include "renderer.hpp"

#include <algorithm>
#include <cmath>

#include <citro3d.h>

#include <components/ctr/gpu/shaders.hpp>
#include <components/debug/debuglog.hpp>

namespace CtrScene
{
    struct Renderer::Program
    {
        CtrGpu::ShaderProgram mShader;
        int mProjection = -1;
        int mModelView = -1;
        int mPosScale = -1;
        int mPosOffset = -1;
        int mMaterial = -1;
        int mVcMode = -1;
        int mAmbient = -1;
        int mDiffuse = -1;
        int mEmissive = -1;
        int mSunDir = -1;
        int mSunColor = -1;
        int mLightPos = -1;
        int mLightColor = -1;
        int mBones = -1;
        bool mSceneUniformsValid = false;
    };

    namespace
    {
        C3D_Mtx sProjection;

        void writeRows(int location, const osg::Matrixf& m, int rows = 3)
        {
            C3D_FVec* v = C3D_FVUnifWritePtr(GPU_VERTEX_SHADER, location, rows);
            for (int k = 0; k < rows; ++k)
            {
                v[k].x = m(0, k);
                v[k].y = m(1, k);
                v[k].z = m(2, k);
                v[k].w = m(3, k);
            }
        }

        void setVec(int location, const osg::Vec3f& v, float w)
        {
            C3D_FVUnifSet(GPU_VERTEX_SHADER, location, v.x(), v.y(), v.z(), w);
        }

        GPU_BLENDFACTOR toGpuBlend(std::uint8_t nif)
        {
            static const GPU_BLENDFACTOR table[] = { GPU_ONE, GPU_ZERO, GPU_SRC_COLOR, GPU_ONE_MINUS_SRC_COLOR,
                GPU_DST_COLOR, GPU_ONE_MINUS_DST_COLOR, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA, GPU_DST_ALPHA,
                GPU_ONE_MINUS_DST_ALPHA, GPU_SRC_ALPHA_SATURATE };
            return nif < std::size(table) ? table[nif] : GPU_ONE;
        }

        GPU_TESTFUNC toGpuTest(std::uint8_t nif)
        {
            static const GPU_TESTFUNC table[]
                = { GPU_ALWAYS, GPU_LESS, GPU_EQUAL, GPU_LEQUAL, GPU_GREATER, GPU_NOTEQUAL, GPU_GEQUAL, GPU_NEVER };
            return nif < std::size(table) ? table[nif] : GPU_ALWAYS;
        }

        std::uint32_t packRgb(const osg::Vec4f& c)
        {
            auto b = [](float v) { return static_cast<std::uint32_t>(std::clamp(v, 0.f, 1.f) * 255.f + 0.5f); };
            return b(c.r()) | (b(c.g()) << 8) | (b(c.b()) << 16);
        }

        std::uint32_t packRgba(const osg::Vec4f& c)
        {
            auto b = [](float v) { return static_cast<std::uint32_t>(std::clamp(v, 0.f, 1.f) * 255.f + 0.5f); };
            return (b(c.r()) << 24) | (b(c.g()) << 16) | (b(c.b()) << 8) | b(c.a());
        }

        std::uint32_t packClearColor(const osg::Vec4f& c, GPU_COLORBUF format)
        {
            auto bits = [](float v, int n) {
                return static_cast<std::uint32_t>(std::clamp(v, 0.f, 1.f) * float((1 << n) - 1) + 0.5f);
            };
            switch (format)
            {
                case GPU_RB_RGB565:
                    return (bits(c.r(), 5) << 11) | (bits(c.g(), 6) << 5) | bits(c.b(), 5);
                case GPU_RB_RGBA5551:
                    return (bits(c.r(), 5) << 11) | (bits(c.g(), 5) << 6) | (bits(c.b(), 5) << 1) | bits(c.a(), 1);
                case GPU_RB_RGBA4:
                    return (bits(c.r(), 4) << 12) | (bits(c.g(), 4) << 8) | (bits(c.b(), 4) << 4) | bits(c.a(), 4);
                case GPU_RB_RGB8:
                    return (bits(c.r(), 8) << 16) | (bits(c.g(), 8) << 8) | bits(c.b(), 8);
                default:
                    return packRgba(c);
            }
        }

        osg::Vec3f transformDirection(const osg::Vec3f& v, const osg::Matrixf& m)
        {
            return osg::Vec3f(v.x() * m(0, 0) + v.y() * m(1, 0) + v.z() * m(2, 0),
                v.x() * m(0, 1) + v.y() * m(1, 1) + v.z() * m(2, 1),
                v.x() * m(0, 2) + v.y() * m(1, 2) + v.z() * m(2, 2));
        }
    }

    Renderer::Renderer()
        : mWorld(new Program)
        , mSkin(new Program)
    {
    }

    Renderer::~Renderer()
    {
        shutdown();
        delete mWorld;
        delete mSkin;
    }

    bool Renderer::loadProgram(Program& program, const unsigned char* data, std::size_t size)
    {
        if (!program.mShader.load(data, size))
            return false;
        program.mProjection = program.mShader.uniform("projection");
        program.mModelView = program.mShader.uniform("modelView");
        program.mPosScale = program.mShader.uniform("posScale");
        program.mPosOffset = program.mShader.uniform("posOffset");
        program.mMaterial = program.mShader.uniform("material");
        program.mVcMode = program.mShader.uniform("vcMode");
        program.mAmbient = program.mShader.uniform("ambient");
        program.mDiffuse = program.mShader.uniform("diffuse");
        program.mEmissive = program.mShader.uniform("emissive");
        program.mSunDir = program.mShader.uniform("sunDir");
        program.mSunColor = program.mShader.uniform("sunColor");
        program.mLightPos = program.mShader.uniform("lightPos");
        program.mLightColor = program.mShader.uniform("lightColor");
        program.mBones = program.mShader.uniform("bones");
        return true;
    }

    bool Renderer::init()
    {
        if (mInitialised)
            return true;
        if (!loadProgram(*mWorld, ctr_shader_world, ctr_shader_world_size)
            || !loadProgram(*mSkin, ctr_shader_skin, ctr_shader_skin_size))
        {
            Log(Debug::Error) << "Failed to load the world shaders";
            return false;
        }
        mInitialised = true;
        return true;
    }

    void Renderer::shutdown()
    {
        if (!mInitialised)
            return;
        mWorld->mShader.unload();
        mSkin->mShader.unload();
        mQueue.clear();
        mArena.clear();
        mInitialised = false;
    }

    void Renderer::bindProgram(Program& program)
    {
        if (mCurrentProgram == &program)
            return;
        if (mCurrentProgram != nullptr)
            mCurrentProgram->mSceneUniformsValid = false;
        mCurrentProgram = &program;
        C3D_BindProgram(&program.mShader.mProgram);
        if (!program.mSceneUniformsValid)
        {
            C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, program.mProjection, &sProjection);
            setVec(program.mSunDir, mSunView, 0.f);
            program.mSceneUniformsValid = true;
        }
        mStateKey = 0xffffffff;
        mLightCacheValid = false;
        mMaterialCacheValid = false;
    }

    void Renderer::beginScene(const SceneParams& params)
    {
        mScene = params;
        mStats = RenderStats();
        mQueue.clear();
        mArena.clear();
        mCurrentProgram = nullptr;
        mWorld->mSceneUniformsValid = false;
        mSkin->mSceneUniformsValid = false;
        mBoundTexture = nullptr;
        mStateKey = 0xffffffff;
        mBoundFormat = 0xff;
        mLightCacheValid = false;
        mMaterialCacheValid = false;

        setFirstPersonDepth(false);
        C3D_RenderTarget* target = params.mTarget != nullptr ? params.mTarget : CtrGpu::getTopTarget();
        if (params.mTarget != nullptr)
            C3D_FrameSplit(0);
        C3D_RenderTargetClear(target, params.mClearColorBuffer ? C3D_CLEAR_ALL : C3D_CLEAR_DEPTH,
            packClearColor(params.mClearColor, static_cast<GPU_COLORBUF>(target->frameBuf.colorFmt)), 0);
        C3D_FrameDrawOn(target);

        if (params.mOrthoHalfSize > 0.f)
        {
            const float h = params.mOrthoHalfSize;
            Mtx_Ortho(&sProjection, -h, h, h, -h, params.mNear, params.mFar, false);
            mFlipWinding = true;
            mFrustum[0] = osg::Vec4f(1.f, 0.f, 0.f, -h);
            mFrustum[1] = osg::Vec4f(-1.f, 0.f, 0.f, -h);
            mFrustum[2] = osg::Vec4f(0.f, 1.f, 0.f, -h);
            mFrustum[3] = osg::Vec4f(0.f, -1.f, 0.f, -h);
        }
        else
        {
            const float fovy = osg::DegreesToRadians(params.mFovY);
            float aspect = C3D_AspectRatioTop;
            if (params.mTarget != nullptr)
            {
                aspect = params.mTargetAspect;
                Mtx_Persp(&sProjection, fovy, aspect, params.mNear, params.mFar, false);
                sProjection.r[1].x = -sProjection.r[1].x;
                sProjection.r[1].y = -sProjection.r[1].y;
                sProjection.r[1].z = -sProjection.r[1].z;
                sProjection.r[1].w = -sProjection.r[1].w;
                mFlipWinding = true;
            }
            else
            {
                Mtx_PerspTilt(&sProjection, fovy, aspect, params.mNear, params.mFar, false);
                mFlipWinding = false;
            }

            const float ty = std::tan(fovy * 0.5f);
            const float tx = ty * aspect;
            auto plane = [](float x, float y, float z) {
                osg::Vec3f n(x, y, z);
                n.normalize();
                return osg::Vec4f(n, 0.f);
            };
            mFrustum[0] = plane(1.f, 0.f, tx);
            mFrustum[1] = plane(-1.f, 0.f, tx);
            mFrustum[2] = plane(0.f, 1.f, ty);
            mFrustum[3] = plane(0.f, -1.f, ty);
        }

        mSunView = transformDirection(params.mSunDirection, params.mView);
        mSunView.normalize();

        C3D_FogLut& lut = mFogLut;
        float data[256] = {};
        const float range = std::max(1.f, params.mFogEnd - params.mFogStart);
        for (int i = 0; i <= 128; ++i)
        {
            const float distance = params.mFar - (i / 128.f / 0.9f) * (params.mFar - params.mNear);
            const float value = std::clamp((params.mFogEnd - distance) / range, 0.f, 1.f);
            if (i < 128)
                data[i] = value;
            if (i > 0)
                data[i + 127] = value - data[i - 1];
        }
        FogLut_FromArray(&lut, data);
        if (params.mFog && !mDebugNoFog)
        {
            C3D_FogGasMode(GPU_FOG, GPU_PLAIN_DENSITY, false);
            C3D_FogColor(packRgb(params.mFogColor));
            C3D_FogLutBind(&lut);
        }
        else
            C3D_FogGasMode(GPU_NO_FOG, GPU_PLAIN_DENSITY, false);

        C3D_TexEnv* env = C3D_GetTexEnv(0);
        C3D_TexEnvInit(env);
        C3D_TexEnvSrc(env, C3D_Both, GPU_TEXTURE0, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR);
        C3D_TexEnvFunc(env, C3D_Both, GPU_MODULATE);
        for (int i = 1; i < 6; ++i)
            C3D_TexEnvInit(C3D_GetTexEnv(i));
    }

    void Renderer::setFirstPersonDepth(bool firstPerson)
    {
        if (firstPerson)
            C3D_DepthMap(true, -0.1f, 0.9f);
        else if (mScene.mOrthoHalfSize > 0.f)
            C3D_DepthMap(true, -0.9f, 0.f);
        else
            C3D_DepthMap(false, -0.9f / mScene.mNear, 0.f);
    }

    bool Renderer::isVisible(const osg::BoundingSpheref& sphere) const
    {
        if (!sphere.valid() || !mCullingEnabled)
            return true;
        const osg::Vec3f c = sphere.center() * mScene.mView;
        const float r = sphere.radius();
        if (c.z() - r > -mScene.mNear || c.z() + r < -mScene.mFar)
            return false;
        for (const osg::Vec4f& p : mFrustum)
            if (p.x() * c.x() + p.y() * c.y() + p.z() * c.z() + p.w() > r)
                return false;
        return true;
    }

    bool Renderer::project(const osg::Vec3f& world, float& x, float& y) const
    {
        const osg::Vec3f p = world * mScene.mView;
        if (p.z() >= -1e-3f)
            return false;
        const float ty = std::tan(osg::DegreesToRadians(mScene.mFovY) * 0.5f);
        const float tx = ty * C3D_AspectRatioTop;
        const float nx = (p.x() / -p.z()) / tx;
        const float ny = (p.y() / -p.z()) / ty;
        x = (nx + 1.f) * 0.5f;
        y = (1.f - ny) * 0.5f;
        return true;
    }

    void Renderer::setVertexLayout(std::uint8_t format, const void* vertices)
    {
        if (format != mBoundFormat)
        {
            C3D_AttrInfo* attr = C3D_GetAttrInfo();
            AttrInfo_Init(attr);
            AttrInfo_AddLoader(attr, 0, GPU_SHORT, 4);
            AttrInfo_AddLoader(attr, 1, GPU_SHORT, 2);
            AttrInfo_AddLoader(attr, 2, GPU_BYTE, 4);
            if (format & VF_Color)
                AttrInfo_AddLoader(attr, 3, GPU_UNSIGNED_BYTE, 4);
            else
                C3D_FixedAttribSet(AttrInfo_AddFixed(attr, 3), 255.f, 255.f, 255.f, 255.f);
            if (format & VF_Skin)
            {
                AttrInfo_AddLoader(attr, 4, GPU_UNSIGNED_BYTE, 4);
                AttrInfo_AddLoader(attr, 5, GPU_UNSIGNED_BYTE, 4);
            }
            mBoundFormat = format;
        }

        int count;
        u64 permutation;
        if (format & VF_Skin)
        {
            count = (format & VF_Color) ? 6 : 5;
            permutation = (format & VF_Color) ? 0x543210 : 0x54210;
        }
        else
        {
            count = (format & VF_Color) ? 4 : 3;
            permutation = (format & VF_Color) ? 0x3210 : 0x210;
        }
        C3D_BufInfo* buf = C3D_GetBufInfo();
        BufInfo_Init(buf);
        BufInfo_Add(buf, vertices, vertexStride(format), count, permutation);
    }

    void Renderer::setLights(Program& program, const DrawParams& params)
    {
        LightCache key;
        key.mFullAmbient = params.mFullAmbient;
        key.mCount = params.mFullAmbient ? 0 : std::min(params.mNumLights, sMaxLightsPerDraw);
        for (int i = 0; i < key.mCount; ++i)
            key.mLights[i] = params.mLights[i];
        if (mLightCacheValid && key == mLightCache)
            return;
        mLightCache = key;
        mLightCacheValid = true;

        if (params.mFullAmbient)
            setVec(program.mSunColor, osg::Vec3f(), 0.f);
        else
            setVec(program.mSunColor, mScene.mSunColor, 0.f);

        C3D_FVec* pos = C3D_FVUnifWritePtr(GPU_VERTEX_SHADER, program.mLightPos, sMaxLightsPerDraw);
        C3D_FVec* color = C3D_FVUnifWritePtr(GPU_VERTEX_SHADER, program.mLightColor, sMaxLightsPerDraw);
        for (int i = 0; i < sMaxLightsPerDraw; ++i)
        {
            if (i < params.mNumLights && params.mLights[i] != nullptr && !params.mFullAmbient)
            {
                const PointLight& light = *params.mLights[i];
                const osg::Vec3f p = light.mPosition * mScene.mView;
                pos[i].x = p.x();
                pos[i].y = p.y();
                pos[i].z = p.z();
                pos[i].w = light.mRadius > 0.f ? 1.f / (light.mRadius * light.mRadius) : 0.f;
                color[i].x = light.mColor.x();
                color[i].y = light.mColor.y();
                color[i].z = light.mColor.z();
                color[i].w = 0.f;
            }
            else
            {
                pos[i].x = 0.f;
                pos[i].y = 0.f;
                pos[i].z = 1e6f;
                pos[i].w = 0.f;
                color[i].x = color[i].y = color[i].z = color[i].w = 0.f;
            }
        }
    }

    void Renderer::applyMaterial(const Material& material, const DrawParams& params, Program& program, bool blended)
    {
        const CtrGpu::Texture* texture = material.mTexture ? material.mTexture.get() : &CtrGpu::getWhiteTexture();
        if (params.mTextureOverride != nullptr && material.mTexture)
            texture = params.mTextureOverride;
        if (mDebugNoTextures)
            texture = &CtrGpu::getWhiteTexture();
        if (texture != mBoundTexture)
        {
            C3D_TexBind(0, const_cast<CtrGpu::Texture*>(texture)->get());
            mBoundTexture = texture;
        }

        const bool fading = params.mAlpha < 1.f;
        const std::uint32_t key = (material.mFlags & 0x3f) | (blended ? 0x40u : 0u) | (fading ? 0x80u : 0u)
            | (static_cast<std::uint32_t>(material.mSrcBlend) << 8) | (static_cast<std::uint32_t>(material.mDstBlend) << 12)
            | (static_cast<std::uint32_t>(material.mAlphaFunc) << 16)
            | (static_cast<std::uint32_t>(material.mAlphaRef) << 20);
        if (key != mStateKey)
        {
            mStateKey = key;
            if ((material.mFlags & Mat_TwoSided) || mDebugTwoSided)
                C3D_CullFace(GPU_CULL_NONE);
            else if (((material.mFlags & Mat_CullFront) != 0) != mFlipWinding)
                C3D_CullFace(GPU_CULL_FRONT_CCW);
            else
                C3D_CullFace(GPU_CULL_BACK_CCW);

            const bool depthWrite = !blended && !fading && !(material.mFlags & Mat_NoDepthWrite);
            C3D_DepthTest(
                !(material.mFlags & Mat_NoDepthTest), GPU_GREATER, depthWrite ? GPU_WRITE_ALL : GPU_WRITE_COLOR);

            if (material.mFlags & Mat_AlphaTest)
                C3D_AlphaTest(true, toGpuTest(material.mAlphaFunc), material.mAlphaRef);
            else if (blended || fading)
                C3D_AlphaTest(true, GPU_GREATER, 0);
            else
                C3D_AlphaTest(false, GPU_ALWAYS, 0);

            if (blended)
            {
                const GPU_BLENDFACTOR src = toGpuBlend(material.mSrcBlend);
                const GPU_BLENDFACTOR dst = toGpuBlend(material.mDstBlend);
                C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, src, dst, src, dst);
            }
            else if (fading)
                C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA, GPU_SRC_ALPHA,
                    GPU_ONE_MINUS_SRC_ALPHA);
            else
                C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ONE, GPU_ZERO, GPU_ONE, GPU_ZERO);
        }

        const osg::Vec3f sceneAmbient = params.mFullAmbient ? osg::Vec3f(1.f, 1.f, 1.f) : mScene.mAmbient;
        if (mMaterialCacheValid && mMaterialCache == &material && mMaterialCacheAlpha == params.mAlpha
            && mMaterialCacheFullAmbient == params.mFullAmbient)
        {
            setLights(program, params);
            return;
        }
        mMaterialCache = &material;
        mMaterialCacheAlpha = params.mAlpha;
        mMaterialCacheFullAmbient = params.mFullAmbient;
        mMaterialCacheValid = true;

        osg::Vec3f ambient, diffuse;
        osg::Vec3f vcMode(0.f, 1.f, 0.f);
        const osg::Vec3f matDiffuse(material.mDiffuse.r(), material.mDiffuse.g(), material.mDiffuse.b());
        if (material.mFlags & Mat_Unlit)
        {
            if (material.mFlags & Mat_VertexEmissive)
                vcMode.z() = 1.f;
        }
        else if (material.mFlags & Mat_VertexAmbDiff)
        {
            ambient = sceneAmbient;
            diffuse = osg::Vec3f(1.f, 1.f, 1.f);
            vcMode = osg::Vec3f(1.f, 0.f, 0.f);
        }
        else
        {
            ambient = osg::componentMultiply(sceneAmbient, material.mAmbient);
            diffuse = matDiffuse;
            if (material.mFlags & Mat_VertexEmissive)
                vcMode.z() = 1.f;
        }
        setVec(program.mVcMode, vcMode, 0.f);
        setVec(program.mAmbient, ambient, material.mDiffuse.a() * params.mAlpha);
        setVec(program.mDiffuse, diffuse, 0.f);
        setVec(program.mEmissive, material.mEmissive, 0.f);
        setLights(program, params);
    }

    void Renderer::drawRigidNow(const Mesh& mesh, const osg::Matrixf& modelView, const DrawParams& params)
    {
        if (!CtrGpu::reserveDraw())
            return;
        Program& program = *mWorld;
        bindProgram(program);
        const bool blended = (mesh.mMaterial.mFlags & Mat_AlphaBlend) != 0;
        applyMaterial(mesh.mMaterial, params, program, blended);
        writeRows(program.mModelView, modelView);
        setVec(program.mPosScale, mesh.mPosScale, 0.f);
        setVec(program.mPosOffset, mesh.mPosOffset, 0.f);
        C3D_FVUnifSet(GPU_VERTEX_SHADER, program.mMaterial, mesh.mUvScale, params.mUvOffsetU, params.mUvOffsetV, 0.f);
        setVertexLayout(mesh.mVertexFormat, mesh.mGeometry.mVertices);
        C3D_DrawElements(GPU_TRIANGLES, static_cast<int>(mesh.mGeometry.mIndexCount), C3D_UNSIGNED_SHORT,
            mesh.mGeometry.mIndices);
        ++mStats.mDraws;
        mStats.mTriangles += mesh.mGeometry.mIndexCount / 3;
    }

    void Renderer::drawPartNow(
        const Mesh& mesh, const SkinPart& part, const osg::Matrixf* modelView, const DrawParams& params)
    {
        if (!CtrGpu::reserveDraw())
            return;
        Program& program = *mSkin;
        bindProgram(program);
        const bool blended = (mesh.mMaterial.mFlags & Mat_AlphaBlend) != 0;
        applyMaterial(mesh.mMaterial, params, program, blended);
        C3D_FVec* rows = C3D_FVUnifWritePtr(GPU_VERTEX_SHADER, program.mBones, static_cast<int>(part.mBones.size()) * 3);
        for (std::size_t b = 0; b < part.mBones.size(); ++b)
        {
            const osg::Matrixf& m = modelView[b];
            for (int k = 0; k < 3; ++k)
            {
                C3D_FVec& row = rows[b * 3 + k];
                row.x = m(0, k);
                row.y = m(1, k);
                row.z = m(2, k);
                row.w = m(3, k);
            }
        }
        setVec(program.mPosScale, mesh.mPosScale, 0.f);
        setVec(program.mPosOffset, mesh.mPosOffset, 0.f);
        C3D_FVUnifSet(GPU_VERTEX_SHADER, program.mMaterial, mesh.mUvScale, params.mUvOffsetU, params.mUvOffsetV, 0.f);
        setVertexLayout(mesh.mVertexFormat, part.mGeometry.mVertices);
        C3D_DrawElements(GPU_TRIANGLES, static_cast<int>(part.mGeometry.mIndexCount), C3D_UNSIGNED_SHORT,
            part.mGeometry.mIndices);
        ++mStats.mDraws;
        mStats.mTriangles += part.mGeometry.mIndexCount / 3;
    }

    void Renderer::drawMesh(const Mesh& mesh, const osg::Matrixf& world, const DrawParams& params)
    {
        if (mesh.mGeometry.mIndexCount == 0)
            return;
        const osg::Matrixf modelView = world * mScene.mView;
        if ((mesh.mMaterial.mFlags & Mat_AlphaBlend) || params.mAlpha < 1.f)
        {
            const osg::Vec3f center = mesh.mBound.center() * modelView;
            mQueue.push_back({ &mesh, -1, center.z(), static_cast<std::uint32_t>(mArena.size()), params });
            mArena.push_back(modelView);
            return;
        }
        drawRigidNow(mesh, modelView, params);
    }

    void Renderer::drawSkinned(
        const Mesh& mesh, const osg::Matrixf* bones, const osg::Matrixf& object, const DrawParams& params)
    {
        const osg::Matrixf objectView = object * mScene.mView;
        const bool queued = (mesh.mMaterial.mFlags & Mat_AlphaBlend) || params.mAlpha < 1.f;
        osg::Matrixf palette[sMaxBonesPerDraw];
        for (std::size_t p = 0; p < mesh.mSkinParts.size(); ++p)
        {
            const SkinPart& part = mesh.mSkinParts[p];
            if (queued)
            {
                const osg::Vec3f center = mesh.mBound.center() * objectView;
                mQueue.push_back(
                    { &mesh, static_cast<int>(p), center.z(), static_cast<std::uint32_t>(mArena.size()), params });
                for (std::uint8_t bone : part.mBones)
                    mArena.push_back(bones[bone] * objectView);
                continue;
            }
            for (std::size_t b = 0; b < part.mBones.size(); ++b)
                palette[b] = bones[part.mBones[b]] * objectView;
            drawPartNow(mesh, part, palette, params);
        }
    }

    void Renderer::endScene()
    {
        std::stable_sort(
            mQueue.begin(), mQueue.end(), [](const QueuedDraw& a, const QueuedDraw& b) { return a.mDepth < b.mDepth; });
        for (const QueuedDraw& draw : mQueue)
        {
            if (draw.mPart < 0)
                drawRigidNow(*draw.mMesh, mArena[draw.mMatrices], draw.mParams);
            else
                drawPartNow(*draw.mMesh, draw.mMesh->mSkinParts[draw.mPart], &mArena[draw.mMatrices], draw.mParams);
        }
        mQueue.clear();
        mArena.clear();
        if (mScene.mTarget == nullptr)
            mScreenStats = mStats;
        C3D_FogGasMode(GPU_NO_FOG, GPU_PLAIN_DENSITY, false);
        C3D_CullFace(GPU_CULL_NONE);
        C3D_AlphaTest(false, GPU_ALWAYS, 0);
        C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_COLOR);
        mCurrentProgram = nullptr;
    }
}
