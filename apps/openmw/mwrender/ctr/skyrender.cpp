#include "skyrender.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include <components/ctr/scene/texturecache.hpp>

#include "sky.hpp"

namespace MWRender
{
    namespace
    {
        constexpr float sDomeRadius = 1000.f;
        constexpr int sSegments = 16;
        constexpr float sRingElevations[] = { -10.f, 6.f, 18.f, 35.f, 60.f, 90.f };
        constexpr int sRings = sizeof(sRingElevations) / sizeof(sRingElevations[0]);
        constexpr float sSunDistance = 900.f;
        constexpr float sSunHalfSize = 60.f;

        std::int16_t quantize(float value, float scale)
        {
            return static_cast<std::int16_t>(std::clamp(std::round(value / scale), -32767.f, 32767.f));
        }

        struct Vertex
        {
            osg::Vec3f mPosition;
            osg::Vec2f mUv;
            osg::Vec3f mNormal;
            float mAlpha;
        };

        void upload(CtrScene::Mesh& mesh, const std::vector<Vertex>& vertices, const std::vector<std::uint16_t>& indices)
        {
            osg::BoundingBoxf box;
            float maxUv = 1e-3f;
            for (const Vertex& v : vertices)
            {
                box.expandBy(v.mPosition);
                maxUv = std::max({ maxUv, std::abs(v.mUv.x()), std::abs(v.mUv.y()) });
            }
            const osg::Vec3f extent = (box._max - box._min) * 0.5f;
            mesh.mVertexFormat = CtrScene::VF_Color;
            mesh.mPosOffset = box.center();
            mesh.mPosScale = osg::Vec3f(std::max(extent.x(), 1e-3f) / 32767.f, std::max(extent.y(), 1e-3f) / 32767.f,
                std::max(extent.z(), 1e-3f) / 32767.f);
            mesh.mUvScale = maxUv / 32767.f;
            mesh.mBound = osg::BoundingSpheref(box.center(), extent.length());

            const std::uint16_t stride = CtrScene::vertexStride(mesh.mVertexFormat);
            auto* out = static_cast<std::uint8_t*>(CtrGpu::linearAllocChecked(vertices.size() * stride));
            auto* index
                = static_cast<std::uint16_t*>(CtrGpu::linearAllocChecked(indices.size() * sizeof(std::uint16_t)));
            if (out == nullptr || index == nullptr)
            {
                CtrGpu::deferLinearFree(out);
                CtrGpu::deferLinearFree(index);
                return;
            }
            mesh.mGeometry.mVertices = out;
            mesh.mGeometry.mIndices = index;
            mesh.mGeometry.mVertexCount = static_cast<std::uint32_t>(vertices.size());
            mesh.mGeometry.mIndexCount = static_cast<std::uint32_t>(indices.size());

            for (const Vertex& v : vertices)
            {
                auto* pos = reinterpret_cast<std::int16_t*>(out);
                pos[0] = quantize(v.mPosition.x() - mesh.mPosOffset.x(), mesh.mPosScale.x());
                pos[1] = quantize(v.mPosition.y() - mesh.mPosOffset.y(), mesh.mPosScale.y());
                pos[2] = quantize(v.mPosition.z() - mesh.mPosOffset.z(), mesh.mPosScale.z());
                pos[3] = 0;
                auto* uv = reinterpret_cast<std::int16_t*>(out + 8);
                uv[0] = quantize(v.mUv.x(), mesh.mUvScale);
                uv[1] = quantize(v.mUv.y(), mesh.mUvScale);
                auto* normal = reinterpret_cast<std::int8_t*>(out + 12);
                normal[0] = static_cast<std::int8_t>(v.mNormal.x() * 127.f);
                normal[1] = static_cast<std::int8_t>(v.mNormal.y() * 127.f);
                normal[2] = static_cast<std::int8_t>(v.mNormal.z() * 127.f);
                normal[3] = 0;
                std::uint8_t* color = out + 16;
                color[0] = color[1] = color[2] = 255;
                color[3] = static_cast<std::uint8_t>(std::clamp(v.mAlpha, 0.f, 1.f) * 255.f + 0.5f);
                out += stride;
            }
            std::copy(indices.begin(), indices.end(), index);
            CtrGpu::flushLinear(mesh.mGeometry.mVertices, vertices.size() * stride);
            CtrGpu::flushLinear(index, indices.size() * sizeof(std::uint16_t));
        }
    }

    CtrSkyRenderer::CtrSkyRenderer(CtrScene::TextureCache& textures)
        : mTextures(textures)
    {
    }

    void CtrSkyRenderer::build()
    {
        mBuilt = true;

        std::vector<Vertex> vertices;
        std::vector<std::uint16_t> indices;
        for (int ring = 0; ring < sRings; ++ring)
        {
            const float elevation = osg::DegreesToRadians(sRingElevations[ring]);
            const float alpha = ring == 0 ? 0.f : (ring == 1 ? 0.25f : 1.f);
            for (int segment = 0; segment <= sSegments; ++segment)
            {
                const float azimuth = 2.f * osg::PIf * segment / sSegments;
                const osg::Vec3f direction(std::cos(elevation) * std::cos(azimuth),
                    std::cos(elevation) * std::sin(azimuth), std::sin(elevation));
                Vertex v;
                v.mPosition = direction * sDomeRadius;
                v.mUv = osg::Vec2f(direction.x(), -direction.y());
                v.mNormal = -direction;
                v.mAlpha = alpha;
                vertices.push_back(v);
            }
        }
        const int side = sSegments + 1;
        for (int ring = 0; ring + 1 < sRings; ++ring)
        {
            for (int segment = 0; segment < sSegments; ++segment)
            {
                const auto a = static_cast<std::uint16_t>(ring * side + segment);
                const auto b = static_cast<std::uint16_t>(a + 1);
                const auto c = static_cast<std::uint16_t>(a + side);
                const auto d = static_cast<std::uint16_t>(c + 1);
                indices.insert(indices.end(), { a, b, d, a, d, c });
            }
        }
        mClouds.mName = "sky clouds";
        mClouds.mMaterial.mFlags = CtrScene::Mat_AlphaBlend | CtrScene::Mat_NoDepthTest | CtrScene::Mat_NoDepthWrite
            | CtrScene::Mat_TwoSided | CtrScene::Mat_Unlit;
        upload(mClouds, vertices, indices);

        vertices.clear();
        indices = { 0, 1, 2, 0, 2, 3 };
        const osg::Vec2f corners[4] = { { -1.f, -1.f }, { 1.f, -1.f }, { 1.f, 1.f }, { -1.f, 1.f } };
        for (const osg::Vec2f& corner : corners)
        {
            Vertex v;
            v.mPosition = osg::Vec3f(corner.x(), corner.y(), 0.f);
            v.mUv = osg::Vec2f((corner.x() + 1.f) * 0.5f, (1.f - corner.y()) * 0.5f);
            v.mNormal = osg::Vec3f(0.f, 0.f, 1.f);
            v.mAlpha = 1.f;
            vertices.push_back(v);
        }
        mSun.mName = "sun";
        mSun.mMaterial.mFlags = CtrScene::Mat_AlphaBlend | CtrScene::Mat_NoDepthTest | CtrScene::Mat_NoDepthWrite
            | CtrScene::Mat_TwoSided | CtrScene::Mat_Unlit;
        mSun.mMaterial.mSrcBlend = 6;
        mSun.mMaterial.mDstBlend = 0;
        mSun.mMaterial.mTexture = mTextures.getExact("textures/tx_sun_05.dds");
        upload(mSun, vertices, indices);
    }

    CtrGpu::TexturePtr CtrSkyRenderer::getCloudTexture(const std::string& name, int slot)
    {
        if (name != mCloudNames[slot])
        {
            mCloudNames[slot] = name;
            mCloudTextures[slot] = name.empty() ? nullptr : mTextures.get(name);
        }
        return mCloudTextures[slot];
    }

    void CtrSkyRenderer::draw(CtrScene::Renderer& renderer, const SkyManager& sky, const osg::Matrixf& view,
        float fovY, const osg::Vec4f& clearColor, const osg::Vec4f& fogColor)
    {
        if (!mBuilt)
            build();

        CtrScene::SceneParams scene;
        scene.mView = view;
        scene.mView.setTrans(0.f, 0.f, 0.f);
        scene.mFovY = fovY;
        scene.mNear = 10.f;
        scene.mFar = sDomeRadius * 2.f;
        scene.mFog = false;
        scene.mParticles = false;
        scene.mClearColor = clearColor;
        renderer.beginScene(scene);

        const osg::Vec3f cloudColor = osg::Vec3f(fogColor.r(), fogColor.g(), fogColor.b()) + osg::Vec3f(0.13f, 0.13f, 0.13f);
        const float blend = sky.getCloudBlendFactor();
        if (mClouds.mGeometry.mIndexCount != 0)
        {
            mClouds.mMaterial.mEmissive = cloudColor;
            const CtrGpu::TexturePtr current = getCloudTexture(sky.getClouds(), 0);
            const CtrGpu::TexturePtr next = getCloudTexture(sky.getNextClouds(), 1);
            CtrScene::DrawParams params;
            params.mUvOffsetV = sky.getCloudAnimationTimer();
            mClouds.mMaterial.mTexture = current ? current : next;
            if (current && blend < 1.f)
            {
                params.mAlpha = 1.f - blend;
                renderer.drawMesh(mClouds, osg::Matrixf::identity(), params);
            }
            if (next && blend > 0.f)
            {
                params.mTextureOverride = next.get();
                params.mAlpha = blend;
                renderer.drawMesh(mClouds, osg::Matrixf::identity(), params);
            }
        }

        const osg::Vec4f& sunColor = sky.getSunDiscColor();
        const osg::Vec3f& sunDirection = sky.getSunDirection();
        if (sky.isSunEnabled() && mSun.mGeometry.mIndexCount != 0 && mSun.mMaterial.mTexture && sunColor.a() > 0.f
            && sunDirection.z() > -0.2f)
        {
            mSun.mMaterial.mEmissive = osg::Vec3f(sunColor.r(), sunColor.g(), sunColor.b());
            const osg::Vec3f right(view(0, 0), view(1, 0), view(2, 0));
            const osg::Vec3f up(view(0, 1), view(1, 1), view(2, 1));
            const osg::Vec3f toCamera(view(0, 2), view(1, 2), view(2, 2));
            const osg::Vec3f center = sunDirection * sSunDistance;
            const osg::Matrixf world(right.x() * sSunHalfSize, right.y() * sSunHalfSize, right.z() * sSunHalfSize, 0.f,
                up.x() * sSunHalfSize, up.y() * sSunHalfSize, up.z() * sSunHalfSize, 0.f, toCamera.x(), toCamera.y(),
                toCamera.z(), 0.f, center.x(), center.y(), center.z(), 1.f);
            CtrScene::DrawParams params;
            params.mAlpha = std::min(sunColor.a(), 1.f);
            renderer.drawMesh(mSun, world, params);
        }

        renderer.endScene();
    }
}
