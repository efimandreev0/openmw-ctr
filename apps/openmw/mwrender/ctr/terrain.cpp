#include "terrain.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>

#include <components/debug/debuglog.hpp>
#include <components/esm/esmterrain.hpp>
#include <components/esm/util.hpp>
#include <components/esm3/loadland.hpp>
#include <components/esm3/loadltex.hpp>
#include <components/esmterrain/landobject.hpp>
#include <components/misc/resourcehelpers.hpp>
#include <components/resource/resourcesystem.hpp>

#include "../../mwbase/environment.hpp"
#include "../../mwworld/esmstore.hpp"

#include "../landmanager.hpp"

#include "context.hpp"

namespace MWRender
{
    namespace
    {
        constexpr int sStep = 2; // use every second height sample
        constexpr int sVertsPerSide = ESM::Land::LAND_SIZE / sStep + 1; // 33
        constexpr int sTiles = ESM::Land::LAND_TEXTURE_SIZE; // 16 texture tiles per side
        constexpr int sQuadsPerTile = (ESM::Land::LAND_SIZE - 1) / sTiles / sStep; // 2
    }

    struct CtrTerrain::Chunk
    {
        std::vector<CtrScene::Mesh> mMeshes;
        void* mSharedVertices = nullptr;
        osg::Matrixf mWorld;
        osg::BoundingSpheref mBound;

        ~Chunk()
        {
            for (std::size_t i = 1; i < mMeshes.size(); ++i)
                mMeshes[i].mGeometry.mVertices = nullptr;
        }
    };

    CtrTerrain::CtrTerrain(LandManager* landManager)
        : mLandManager(landManager)
    {
    }

    CtrTerrain::~CtrTerrain() = default;

    void CtrTerrain::addCell(int x, int y)
    {
        const auto key = std::make_pair(x, y);
        if (mChunks.count(key))
            return;
        std::unique_ptr<Chunk> chunk = build(x, y);
        if (chunk)
            mChunks.emplace(key, std::move(chunk));
    }

    void CtrTerrain::removeCell(int x, int y)
    {
        mChunks.erase(std::make_pair(x, y));
    }

    void CtrTerrain::clear()
    {
        mChunks.clear();
    }

    std::unique_ptr<CtrTerrain::Chunk> CtrTerrain::build(int cellX, int cellY)
    {
        const ESM::RefId worldspace = ESM::Cell::sDefaultWorldspaceId;
        std::shared_ptr<const ESMTerrain::LandObject> land
            = mLandManager->getLand(ESM::ExteriorCellLocation(cellX, cellY, worldspace));
        if (!land)
            return nullptr;
        const ESM::LandData* data = land->getData(ESM::Land::DATA_VHGT);
        if (data == nullptr || data->getLandSize() != ESM::Land::LAND_SIZE)
            return nullptr;

        const auto heights = data->getHeights();
        const ESM::LandData* normalData = land->getData(ESM::Land::DATA_VNML);
        const ESM::LandData* colorData = land->getData(ESM::Land::DATA_VCLR);
        const ESM::LandData* textureData = land->getData(ESM::Land::DATA_VTEX);
        const float cellSize = static_cast<float>(ESM::Land::REAL_SIZE);
        const float spacing = cellSize / (ESM::Land::LAND_SIZE - 1);

        auto chunk = std::make_unique<Chunk>();
        chunk->mWorld.makeTranslate(cellX * cellSize, cellY * cellSize, 0.f);

        CtrScene::Mesh prototype;
        prototype.mVertexFormat = CtrScene::VF_Color;
        prototype.mMaterial.mFlags = CtrScene::Mat_VertexAmbDiff;
        const float minHeight = data->getMinHeight();
        const float maxHeight = std::max(data->getMaxHeight(), minHeight + 1.f);
        prototype.mPosOffset = osg::Vec3f(cellSize * 0.5f, cellSize * 0.5f, (minHeight + maxHeight) * 0.5f);
        prototype.mPosScale = osg::Vec3f(
            cellSize * 0.5f / 32767.f, cellSize * 0.5f / 32767.f, (maxHeight - minHeight) * 0.5f / 32767.f);
        const float maxUv = static_cast<float>(sTiles);
        prototype.mUvScale = maxUv / 32767.f;
        prototype.mNode = 0;
        prototype.mBound = osg::BoundingSpheref(osg::BoundingBoxf(
            osg::Vec3f(0.f, 0.f, minHeight), osg::Vec3f(cellSize, cellSize, maxHeight)));

        const std::uint16_t stride = CtrScene::vertexStride(prototype.mVertexFormat);
        const std::size_t vertexCount = sVertsPerSide * sVertsPerSide;
        auto* vertices = static_cast<std::uint8_t*>(CtrGpu::linearAllocChecked(vertexCount * stride));
        if (vertices == nullptr)
            return nullptr;
        chunk->mSharedVertices = vertices;

        auto quantize = [](float value, float offset, float scale) {
            return static_cast<std::int16_t>(std::clamp(std::round((value - offset) / scale), -32767.f, 32767.f));
        };

        for (int y = 0; y < sVertsPerSide; ++y)
        {
            for (int x = 0; x < sVertsPerSide; ++x)
            {
                const int sx = x * sStep;
                const int sy = y * sStep;
                const std::size_t src = static_cast<std::size_t>(sy) * ESM::Land::LAND_SIZE + sx;
                std::uint8_t* out = vertices + (static_cast<std::size_t>(y) * sVertsPerSide + x) * stride;
                auto* pos = reinterpret_cast<std::int16_t*>(out);
                pos[0] = quantize(sx * spacing, prototype.mPosOffset.x(), prototype.mPosScale.x());
                pos[1] = quantize(sy * spacing, prototype.mPosOffset.y(), prototype.mPosScale.y());
                pos[2] = quantize(heights[src], prototype.mPosOffset.z(), prototype.mPosScale.z());
                pos[3] = 0;
                auto* uv = reinterpret_cast<std::int16_t*>(out + 8);
                uv[0] = quantize(static_cast<float>(x) / (sQuadsPerTile), 0.f, prototype.mUvScale);
                uv[1] = quantize(static_cast<float>(y) / (sQuadsPerTile), 0.f, prototype.mUvScale);
                auto* normal = reinterpret_cast<std::int8_t*>(out + 12);
                if (normalData != nullptr)
                {
                    const auto normals = normalData->getNormals();
                    osg::Vec3f n(normals[src * 3], normals[src * 3 + 1], normals[src * 3 + 2]);
                    n.normalize();
                    normal[0] = static_cast<std::int8_t>(n.x() * 127.f);
                    normal[1] = static_cast<std::int8_t>(n.y() * 127.f);
                    normal[2] = static_cast<std::int8_t>(n.z() * 127.f);
                }
                else
                {
                    normal[0] = 0;
                    normal[1] = 0;
                    normal[2] = 127;
                }
                normal[3] = 0;
                std::uint8_t* color = out + 16;
                if (colorData != nullptr)
                {
                    const auto colors = colorData->getColors();
                    color[0] = colors[src * 3];
                    color[1] = colors[src * 3 + 1];
                    color[2] = colors[src * 3 + 2];
                }
                else
                    color[0] = color[1] = color[2] = 255;
                color[3] = 255;
            }
        }
        CtrGpu::flushLinear(vertices, vertexCount * stride);

        std::map<std::uint16_t, std::vector<std::uint16_t>> indicesByTexture;
        for (int ty = 0; ty < sTiles; ++ty)
        {
            for (int tx = 0; tx < sTiles; ++tx)
            {
                std::uint16_t texture = 0;
                if (textureData != nullptr)
                    texture = textureData->getTextures()[ty * sTiles + tx];
                std::vector<std::uint16_t>& indices = indicesByTexture[texture];
                for (int qy = 0; qy < sQuadsPerTile; ++qy)
                {
                    for (int qx = 0; qx < sQuadsPerTile; ++qx)
                    {
                        const int x = tx * sQuadsPerTile + qx;
                        const int y = ty * sQuadsPerTile + qy;
                        const std::uint16_t v0 = static_cast<std::uint16_t>(y * sVertsPerSide + x);
                        const std::uint16_t v1 = static_cast<std::uint16_t>(v0 + 1);
                        const std::uint16_t v2 = static_cast<std::uint16_t>(v0 + sVertsPerSide);
                        const std::uint16_t v3 = static_cast<std::uint16_t>(v2 + 1);
                        indices.insert(indices.end(), { v0, v1, v3, v0, v3, v2 });
                    }
                }
            }
        }

        const MWWorld::ESMStore& store = *MWBase::Environment::get().getESMStore();
        CtrScene::TextureCache& textures = CtrContext::get()->getTextures();
        const int plugin = land->getPlugin();
        for (auto& [textureId, indices] : indicesByTexture)
        {
            CtrScene::Mesh mesh;
            mesh.mVertexFormat = prototype.mVertexFormat;
            mesh.mMaterial = prototype.mMaterial;
            mesh.mPosOffset = prototype.mPosOffset;
            mesh.mPosScale = prototype.mPosScale;
            mesh.mUvScale = prototype.mUvScale;
            mesh.mNode = 0;
            mesh.mBound = prototype.mBound;

            std::string texturePath = "_land_default.dds";
            if (textureId != 0)
                if (const ESM::Path* path = store.get<ESM::LandTexture>().search(textureId - 1, plugin))
                    texturePath = std::string(path->getNormalized().value());
            mesh.mMaterial.mTexture = textures.get(texturePath);

            auto* indexBuffer
                = static_cast<std::uint16_t*>(CtrGpu::linearAllocChecked(indices.size() * sizeof(std::uint16_t)));
            if (indexBuffer == nullptr)
                break;
            std::memcpy(indexBuffer, indices.data(), indices.size() * sizeof(std::uint16_t));
            CtrGpu::flushLinear(indexBuffer, indices.size() * sizeof(std::uint16_t));
            mesh.mGeometry.mVertices = vertices;
            mesh.mGeometry.mVertexCount = static_cast<std::uint32_t>(vertexCount);
            mesh.mGeometry.mIndices = indexBuffer;
            mesh.mGeometry.mIndexCount = static_cast<std::uint32_t>(indices.size());
            chunk->mMeshes.push_back(std::move(mesh));
        }

        if (chunk->mMeshes.empty())
        {
            CtrGpu::deferLinearFree(vertices);
            return nullptr;
        }

        chunk->mBound = osg::BoundingSpheref(
            prototype.mBound.center() + chunk->mWorld.getTrans(), prototype.mBound.radius());
        return chunk;
    }

    void CtrTerrain::draw(CtrScene::Renderer& renderer, const CtrScene::DrawParams& params) const
    {
        if (!mEnabled)
            return;
        for (const auto& [key, chunk] : mChunks)
        {
            if (!renderer.isVisible(chunk->mBound))
                continue;
            for (const CtrScene::Mesh& mesh : chunk->mMeshes)
                renderer.drawMesh(mesh, chunk->mWorld, params);
        }
    }
}
