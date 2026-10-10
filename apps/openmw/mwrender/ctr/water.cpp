#include "water.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>

#include <components/ctr/scene/texturecache.hpp>

namespace MWRender
{
    namespace
    {
        constexpr float sHalfSize = 8192.f;
        constexpr int sGridCells = 8;
        constexpr float sTextureRepeat = 1024.f;
        constexpr int sFrameStep = 4;
        constexpr float sFramesPerSecond = 8.f;

        std::int16_t quantize(float value, float scale)
        {
            return static_cast<std::int16_t>(std::clamp(std::round(value / scale), -32767.f, 32767.f));
        }
    }

    CtrWater::CtrWater(CtrScene::TextureCache& textures)
        : mTextures(textures)
    {
    }

    void CtrWater::build()
    {
        mBuilt = true;

        for (int i = 0; i < 32; i += sFrameStep)
        {
            char path[40];
            std::snprintf(path, sizeof(path), "textures/water/water%02d.dds", i);
            if (CtrGpu::TexturePtr texture = mTextures.getExact(path))
                mFrames.push_back(std::move(texture));
        }

        mMesh.mName = "water";
        mMesh.mVertexFormat = 0;
        mMesh.mPosOffset = osg::Vec3f();
        mMesh.mPosScale = osg::Vec3f(sHalfSize / 32767.f, sHalfSize / 32767.f, 1.f / 32767.f);
        const float maxUv = sHalfSize / sTextureRepeat;
        mMesh.mUvScale = maxUv / 32767.f;
        mMesh.mBound = osg::BoundingSpheref(osg::Vec3f(), sHalfSize * 1.5f);

        CtrScene::Material& material = mMesh.mMaterial;
        material.mFlags = CtrScene::Mat_AlphaBlend | CtrScene::Mat_TwoSided | CtrScene::Mat_NoDepthWrite;
        material.mDiffuse = osg::Vec4f(1.f, 1.f, 1.f, 0.75f);
        material.mAmbient = osg::Vec3f(1.f, 1.f, 1.f);
        if (!mFrames.empty())
            material.mTexture = mFrames.front();

        const int side = sGridCells + 1;
        const std::uint16_t stride = CtrScene::vertexStride(0);
        auto* vertices = static_cast<std::uint8_t*>(CtrGpu::linearAllocChecked(side * side * stride));
        auto* indices = static_cast<std::uint16_t*>(
            CtrGpu::linearAllocChecked(sGridCells * sGridCells * 6 * sizeof(std::uint16_t)));
        if (vertices == nullptr || indices == nullptr)
        {
            CtrGpu::deferLinearFree(vertices);
            CtrGpu::deferLinearFree(indices);
            return;
        }

        std::uint8_t* out = vertices;
        for (int y = 0; y < side; ++y)
        {
            for (int x = 0; x < side; ++x)
            {
                const float px = -sHalfSize + 2.f * sHalfSize * x / sGridCells;
                const float py = -sHalfSize + 2.f * sHalfSize * y / sGridCells;
                auto* pos = reinterpret_cast<std::int16_t*>(out);
                pos[0] = quantize(px, mMesh.mPosScale.x());
                pos[1] = quantize(py, mMesh.mPosScale.y());
                pos[2] = 0;
                pos[3] = 0;
                auto* uv = reinterpret_cast<std::int16_t*>(out + 8);
                uv[0] = quantize(px / sTextureRepeat, mMesh.mUvScale);
                uv[1] = quantize(-py / sTextureRepeat, mMesh.mUvScale);
                auto* normal = reinterpret_cast<std::int8_t*>(out + 12);
                normal[0] = 0;
                normal[1] = 0;
                normal[2] = 127;
                normal[3] = 0;
                out += stride;
            }
        }

        std::uint16_t* index = indices;
        for (int y = 0; y < sGridCells; ++y)
        {
            for (int x = 0; x < sGridCells; ++x)
            {
                const auto v = static_cast<std::uint16_t>(y * side + x);
                *index++ = v;
                *index++ = static_cast<std::uint16_t>(v + 1);
                *index++ = static_cast<std::uint16_t>(v + side + 1);
                *index++ = v;
                *index++ = static_cast<std::uint16_t>(v + side + 1);
                *index++ = static_cast<std::uint16_t>(v + side);
            }
        }

        CtrGpu::flushLinear(vertices, side * side * stride);
        CtrGpu::flushLinear(indices, sGridCells * sGridCells * 6 * sizeof(std::uint16_t));
        mMesh.mGeometry.mVertices = vertices;
        mMesh.mGeometry.mIndices = indices;
        mMesh.mGeometry.mVertexCount = side * side;
        mMesh.mGeometry.mIndexCount = sGridCells * sGridCells * 6;
    }

    void CtrWater::draw(CtrScene::Renderer& renderer, const osg::Vec3f& cameraPosition, float height, float time)
    {
        if (!mBuilt)
            build();
        if (mMesh.mGeometry.mIndexCount == 0)
            return;

        const float x = std::floor(cameraPosition.x() / sTextureRepeat) * sTextureRepeat;
        const float y = std::floor(cameraPosition.y() / sTextureRepeat) * sTextureRepeat;
        const osg::Matrixf world = osg::Matrixf::translate(x, y, height);

        CtrScene::DrawParams params;
        if (!mFrames.empty())
        {
            const auto frame = static_cast<std::size_t>(time * sFramesPerSecond) % mFrames.size();
            params.mTextureOverride = mFrames[frame].get();
        }
        renderer.drawMesh(mMesh, world, params);
    }
}
