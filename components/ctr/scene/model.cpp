#include "model.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <set>

#include <components/debug/debuglog.hpp>
#include <components/misc/strings/algorithm.hpp>
#include <components/misc/strings/lower.hpp>
#include <components/nif/controller.hpp>
#include <components/nif/data.hpp>
#include <components/nif/extra.hpp>
#include <components/nif/niffile.hpp>
#include <components/nif/node.hpp>
#include <components/nif/particle.hpp>
#include <components/nif/property.hpp>
#include <components/nif/texture.hpp>
#include <components/resource/niffilemanager.hpp>

#include "texturecache.hpp"

#include <cstdio>

extern "C" const char* volatile g_ctrBeacon;

namespace CtrScene
{

    GpuGeometry::GpuGeometry(GpuGeometry&& other) noexcept
        : mVertices(other.mVertices)
        , mIndices(other.mIndices)
        , mVertexCount(other.mVertexCount)
        , mIndexCount(other.mIndexCount)
    {
        other.mVertices = nullptr;
        other.mIndices = nullptr;
        other.mVertexCount = 0;
        other.mIndexCount = 0;
    }

    GpuGeometry& GpuGeometry::operator=(GpuGeometry&& other) noexcept
    {
        if (this != &other)
        {
            this->~GpuGeometry();
            new (this) GpuGeometry(std::move(other));
        }
        return *this;
    }

    GpuGeometry::~GpuGeometry()
    {
        if (mVertices != nullptr)
            CtrGpu::deferLinearFree(mVertices);
        if (mIndices != nullptr)
            CtrGpu::deferLinearFree(mIndices);
    }


    int Model::findNode(std::string_view name) const
    {
        for (std::size_t i = 0; i < mNodes.size(); ++i)
            if (Misc::StringUtils::ciEqual(mNodes[i].mName, name))
                return static_cast<int>(i);
        return -1;
    }

    int Model::findNodeByRecordIndex(int recordIndex) const
    {
        for (std::size_t i = 0; i < mNodes.size(); ++i)
            if (mNodes[i].mRecordIndex == recordIndex)
                return static_cast<int>(i);
        return -1;
    }

    int Model::findSwitch(std::string_view name) const
    {
        for (std::size_t i = 0; i < mSwitches.size(); ++i)
            if (Misc::StringUtils::ciEqual(mSwitches[i].mName, name))
                return static_cast<int>(i);
        return -1;
    }

    float ParticleSystem::controllerTime(float input) const
    {
        const float time = mFrequency * input + mPhase;
        if (time >= mStartTime && time <= mStopTime)
            return time;
        const float delta = mStopTime - mStartTime;
        switch (mExtrapolation)
        {
            case Nif::NiTimeController::ExtrapolationMode::Cycle:
            {
                if (delta <= 0)
                    return mStartTime;
                const float cycles = (time - mStartTime) / delta;
                return mStartTime + (cycles - std::floor(cycles)) * delta;
            }
            case Nif::NiTimeController::ExtrapolationMode::Reverse:
            {
                if (delta <= 0)
                    return mStartTime;
                const float cycles = (time - mStartTime) / delta;
                const float remainder = (cycles - std::floor(cycles)) * delta;
                if ((static_cast<int>(std::fabs(std::floor(cycles))) % 2) == 0)
                    return mStartTime + remainder;
                return mStopTime - remainder;
            }
            default:
                return std::clamp(time, mStartTime, mStopTime);
        }
    }

    void Model::computeBindPose(std::vector<osg::Matrixf>& out) const
    {
        out.resize(mNodes.size());
        for (std::size_t i = 0; i < mNodes.size(); ++i)
        {
            const Node& node = mNodes[i];
            out[i] = node.mParent >= 0 ? node.mLocal * out[node.mParent] : node.mLocal;
        }
    }

    namespace
    {
        struct PropertyState
        {
            const Nif::NiTexturingProperty* mTexturing = nullptr;
            const Nif::NiMaterialProperty* mMaterial = nullptr;
            const Nif::NiAlphaProperty* mAlpha = nullptr;
            const Nif::NiVertexColorProperty* mVertexColor = nullptr;
            const Nif::NiStencilProperty* mStencil = nullptr;
            const Nif::NiZBufferProperty* mZBuffer = nullptr;

            void apply(const Nif::NiAVObject& object)
            {
                for (const auto& property : object.mProperties)
                {
                    if (property.empty())
                        continue;
                    const Nif::NiProperty* p = property.getPtr();
                    switch (p->mRecordType)
                    {
                        case Nif::RC_NiTexturingProperty:
                            mTexturing = static_cast<const Nif::NiTexturingProperty*>(p);
                            break;
                        case Nif::RC_NiMaterialProperty:
                            mMaterial = static_cast<const Nif::NiMaterialProperty*>(p);
                            break;
                        case Nif::RC_NiAlphaProperty:
                            mAlpha = static_cast<const Nif::NiAlphaProperty*>(p);
                            break;
                        case Nif::RC_NiVertexColorProperty:
                            mVertexColor = static_cast<const Nif::NiVertexColorProperty*>(p);
                            break;
                        case Nif::RC_NiStencilProperty:
                            mStencil = static_cast<const Nif::NiStencilProperty*>(p);
                            break;
                        case Nif::RC_NiZBufferProperty:
                            mZBuffer = static_cast<const Nif::NiZBufferProperty*>(p);
                            break;
                        default:
                            break;
                    }
                }
            }
        };

        bool isNiGeometry(int type)
        {
            return type == Nif::RC_NiTriShape || type == Nif::RC_NiTriStrips || type == Nif::RC_BSLODTriShape
                || type == Nif::RC_BSSegmentedTriShape;
        }

        std::int16_t quantize(float value, float offset, float scale)
        {
            const float q = std::round((value - offset) / scale);
            return static_cast<std::int16_t>(std::clamp(q, -32767.f, 32767.f));
        }

        std::int8_t quantizeNormal(float value)
        {
            return static_cast<std::int8_t>(std::clamp(std::round(value * 127.f), -127.f, 127.f));
        }

        std::uint8_t toByte(float value)
        {
            return static_cast<std::uint8_t>(std::clamp(std::round(value * 255.f), 0.f, 255.f));
        }

        struct SourceVertex
        {
            osg::Vec3f mPosition;
            osg::Vec2f mUv;
            osg::Vec3f mNormal;
            osg::Vec4f mColor{ 1.f, 1.f, 1.f, 1.f };
            std::uint8_t mBones[3] = { 0, 0, 0 };
            float mWeights[3] = { 1.f, 0.f, 0.f };
        };

        void* writeVertices(const std::vector<SourceVertex>& vertices, const std::vector<std::uint16_t>& order,
            const Mesh& mesh, const std::uint8_t* boneRemap)
        {
            const std::uint16_t stride = vertexStride(mesh.mVertexFormat);
            auto* buffer = static_cast<std::uint8_t*>(CtrGpu::linearAllocChecked(order.size() * stride));
            if (buffer == nullptr)
                return nullptr;
            std::uint8_t* out = buffer;
            for (std::uint16_t index : order)
            {
                const SourceVertex& v = vertices[index];
                auto* pos = reinterpret_cast<std::int16_t*>(out);
                pos[0] = quantize(v.mPosition.x(), mesh.mPosOffset.x(), mesh.mPosScale.x());
                pos[1] = quantize(v.mPosition.y(), mesh.mPosOffset.y(), mesh.mPosScale.y());
                pos[2] = quantize(v.mPosition.z(), mesh.mPosOffset.z(), mesh.mPosScale.z());
                pos[3] = 0;
                auto* uv = reinterpret_cast<std::int16_t*>(out + 8);
                uv[0] = quantize(v.mUv.x(), 0.f, mesh.mUvScale);
                uv[1] = quantize(v.mUv.y(), 0.f, mesh.mUvScale);
                auto* normal = reinterpret_cast<std::int8_t*>(out + 12);
                normal[0] = quantizeNormal(v.mNormal.x());
                normal[1] = quantizeNormal(v.mNormal.y());
                normal[2] = quantizeNormal(v.mNormal.z());
                normal[3] = 0;
                std::uint8_t* next = out + 16;
                if (mesh.mVertexFormat & VF_Color)
                {
                    next[0] = toByte(v.mColor.r());
                    next[1] = toByte(v.mColor.g());
                    next[2] = toByte(v.mColor.b());
                    next[3] = toByte(v.mColor.a());
                    next += 4;
                }
                if (mesh.mVertexFormat & VF_Skin)
                {
                    int sum = 0;
                    for (int i = 0; i < 3; ++i)
                    {
                        next[i] = boneRemap != nullptr ? boneRemap[v.mBones[i]] : v.mBones[i];
                        next[4 + i] = toByte(v.mWeights[i]);
                        sum += next[4 + i];
                    }
                    next[4] = static_cast<std::uint8_t>(std::clamp(next[4] + 255 - sum, 0, 255));
                    next[3] = 0;
                    next[7] = 0;
                }
                out += stride;
            }
            CtrGpu::flushLinear(buffer, order.size() * stride);
            return buffer;
        }

        std::uint16_t* writeIndices(const std::vector<std::uint16_t>& indices)
        {
            auto* buffer = static_cast<std::uint16_t*>(
                CtrGpu::linearAllocChecked(std::max<std::size_t>(indices.size(), 1) * sizeof(std::uint16_t)));
            if (buffer == nullptr)
                return nullptr;
            std::memcpy(buffer, indices.data(), indices.size() * sizeof(std::uint16_t));
            CtrGpu::flushLinear(buffer, indices.size() * sizeof(std::uint16_t));
            return buffer;
        }

        class Builder
        {
        public:
            Builder(Model& model, TextureCache* textures, Nif::FileView nif)
                : mModel(model)
                , mTextures(textures)
                , mNif(nif)
            {
            }

            void build()
            {
                for (std::size_t i = 0; i < mNif.numRoots(); ++i)
                {
                    const Nif::Record* root = mNif.getRoot(i);
                    if (root == nullptr)
                        continue;
                    if (const auto* object = dynamic_cast<const Nif::NiAVObject*>(root))
                    {
                        mRoot = nullptr;
                        mHasMarkers = false;
                        mCollisionNode = nullptr;
                        handle(object, -1, PropertyState(), false, -1, -1, false, 0);
                    }
                }
                for (ParticleSystem& system : mModel.mParticleSystems)
                    if (system.mEmitterRecord >= 0)
                        system.mEmitterNode
                            = static_cast<std::int16_t>(mModel.findNodeByRecordIndex(system.mEmitterRecord));
                computeBounds();
            }

        private:
            Model& mModel;
            TextureCache* mTextures;
            Nif::FileView mNif;
            const Nif::NiAVObject* mRoot = nullptr;
            const Nif::NiAVObject* mCollisionNode = nullptr;
            bool mHasMarkers = false;

            void handle(const Nif::NiAVObject* nifNode, int parent, PropertyState props, bool skipMeshes,
                int switchIndex, int switchChild, bool billboard, int animFlags)
            {
                if (mRoot != nullptr && Misc::StringUtils::ciEqual(nifNode->mName, "Bounding Box"))
                    return;
                if (mModel.mNodes.size() >= 0x7fff)
                    return;

                const bool isRoot = mRoot == nullptr;
                if (isRoot)
                    mRoot = nifNode;

                const int nodeIndex = static_cast<int>(mModel.mNodes.size());
                {
                    Node node;
                    node.mName = nifNode->mName;
                    node.mLocal = nifNode->mTransform.toMatrix();
                    node.mParent = static_cast<std::int16_t>(parent);
                    node.mRecordIndex = static_cast<std::int32_t>(nifNode->mRecordIndex);
                    mModel.mNodes.push_back(std::move(node));
                }

                for (Nif::NiTimeControllerPtr ctrl = nifNode->mController; !ctrl.empty(); ctrl = ctrl->mNext)
                    mModel.mMaxControllerTime = std::max(mModel.mMaxControllerTime, ctrl->mTimeStop);

                bool recursiveCollision = false;
                for (const auto& e : nifNode->getExtraList())
                {
                    if (e.empty())
                        continue;
                    if (e->mRecordType == Nif::RC_NiTextKeyExtraData)
                    {
                        const auto* tk = static_cast<const Nif::NiTextKeyExtraData*>(e.getPtr());
                        for (const Nif::NiTextKeyExtraData::TextKey& key : tk->mList)
                        {
                            std::vector<std::string> lines;
                            Misc::StringUtils::split(key.mText, lines, "\r\n");
                            for (std::string& line : lines)
                            {
                                Misc::StringUtils::trim(line);
                                Misc::StringUtils::lowerCaseInPlace(line);
                                if (!line.empty())
                                    mModel.mTextKeys.emplace(key.mTime, std::move(line));
                            }
                        }
                    }
                    else if (e->mRecordType == Nif::RC_NiStringExtraData)
                    {
                        const auto* sd = static_cast<const Nif::NiStringExtraData*>(e.getPtr());
                        if (sd->mData == "MRK" && isRoot)
                            mHasMarkers = true;
                        else if (sd->mData == "RCN" && isRoot)
                            recursiveCollision = true;
                    }
                    else if (e->mRecordType == Nif::RC_BSXFlags && isRoot)
                    {
                        const auto* flags = static_cast<const Nif::NiIntegerExtraData*>(e.getPtr());
                        if (flags->mData & 32)
                            mHasMarkers = true;
                    }
                }

                const auto* niNode = dynamic_cast<const Nif::NiNode*>(nifNode);
                if (niNode != nullptr && isRoot)
                    mCollisionNode = niNode->findRootCollisionNode(recursiveCollision);

                if (nifNode == mCollisionNode || nifNode->isHidden())
                {
                    skipMeshes = true;
                    mModel.mNodes[nodeIndex].mFlags |= Node_Hidden;
                }

                if (nifNode->mRecordType == Nif::RC_NiBillboardNode)
                {
                    billboard = true;
                    mModel.mNodes[nodeIndex].mFlags |= Node_Billboard;
                }

                props.apply(*nifNode);

                if (nifNode->mRecordType == Nif::RC_NiBSAnimationNode
                    || nifNode->mRecordType == Nif::RC_NiBSParticleNode)
                    animFlags = nifNode->mFlags;

                if (nifNode->mRecordType == Nif::RC_NiParticles && !skipMeshes)
                    buildParticles(*nifNode, nodeIndex, props, animFlags);

                if (isNiGeometry(nifNode->mRecordType) && !skipMeshes)
                {
                    const std::string_view name = nifNode->mName;
                    const bool skip = (mHasMarkers && Misc::StringUtils::ciStartsWith(name, "tri editormarker"))
                        || Misc::StringUtils::ciStartsWith(name, "shadow")
                        || Misc::StringUtils::ciStartsWith(name, "tri shadow");
                    if (!skip)
                        buildMesh(static_cast<const Nif::NiGeometry*>(nifNode), nodeIndex, props, switchIndex,
                            switchChild, billboard);
                }

                if (niNode == nullptr)
                    return;

                if (nifNode->mRecordType == Nif::RC_NiSwitchNode || nifNode->mRecordType == Nif::RC_NiLODNode)
                {
                    Switch sw;
                    sw.mName = nifNode->mName;
                    sw.mNode = static_cast<std::int16_t>(nodeIndex);
                    sw.mNumChildren = static_cast<std::uint8_t>(std::min<std::size_t>(niNode->mChildren.size(), 255));
                    if (nifNode->mRecordType == Nif::RC_NiSwitchNode)
                        sw.mInitialChild = static_cast<std::uint8_t>(
                            static_cast<const Nif::NiSwitchNode*>(nifNode)->mInitialIndex);
                    mModel.mNodes[nodeIndex].mFlags |= Node_Switch;
                    const int newSwitch = static_cast<int>(mModel.mSwitches.size());
                    mModel.mSwitches.push_back(std::move(sw));
                    for (std::size_t i = 0; i < niNode->mChildren.size() && i < 127; ++i)
                        if (!niNode->mChildren[i].empty())
                            handle(niNode->mChildren[i].getPtr(), nodeIndex, props, skipMeshes, newSwitch,
                                static_cast<int>(i), billboard, animFlags);
                    return;
                }

                for (const auto& child : niNode->mChildren)
                    if (!child.empty())
                        handle(child.getPtr(), nodeIndex, props, skipMeshes, switchIndex, switchChild, billboard,
                            animFlags);
            }

            void buildParticles(const Nif::NiAVObject& nifNode, int nodeIndex, const PropertyState& props, int animFlags)
            {
                const Nif::NiParticleSystemController* partctrl = nullptr;
                for (Nif::NiTimeControllerPtr ctrl = nifNode.mController; !ctrl.empty(); ctrl = ctrl->mNext)
                {
                    if (!ctrl->isActive())
                        continue;
                    if (ctrl->mRecordType == Nif::RC_NiParticleSystemController
                        || ctrl->mRecordType == Nif::RC_NiBSPArrayController)
                        partctrl = static_cast<const Nif::NiParticleSystemController*>(ctrl.getPtr());
                }
                if (partctrl == nullptr)
                    return;

                ParticleSystem system;
                system.mNode = static_cast<std::int16_t>(nodeIndex);
                system.mWorldSpace = !(animFlags & Nif::NiNode::ParticleFlag_LocalSpace);
                system.mAutoPlay = (animFlags & Nif::NiNode::ParticleFlag_AutoPlay) != 0;
                if (!partctrl->mEmitter.empty())
                    system.mEmitterRecord = static_cast<std::int32_t>(partctrl->mEmitter->mRecordIndex);

                const auto* particles = static_cast<const Nif::NiParticles*>(&nifNode);
                std::size_t quota = partctrl->mParticles.size();
                if (!particles->mData.empty())
                {
                    const auto* data = static_cast<const Nif::NiParticlesData*>(particles->mData.getPtr());
                    quota = data->mNumParticles;
                    system.mBoundRadius = data->mBoundingSphere.radius();
                }
                system.mMaxParticles = static_cast<std::uint16_t>(std::min<std::size_t>(quota, 48));

                if (partctrl->noAutoAdjust())
                    system.mBirthRate = partctrl->mBirthRate;
                else if (partctrl->mLifetime != 0 || partctrl->mLifetimeVariation != 0)
                    system.mBirthRate = static_cast<float>(partctrl->mParticles.size())
                        / (partctrl->mLifetime + partctrl->mLifetimeVariation / 2);
                if (quota > system.mMaxParticles && quota > 0)
                    system.mBirthRate *= static_cast<float>(system.mMaxParticles) / static_cast<float>(quota);

                system.mSpeedMin = partctrl->mSpeed - partctrl->mSpeedVariation * 0.5f;
                system.mSpeedMax = partctrl->mSpeed + partctrl->mSpeedVariation * 0.5f;
                system.mPlanarAngle = partctrl->mPlanarAngle;
                system.mPlanarAngleVariation = partctrl->mPlanarAngleVariation;
                system.mDeclination = partctrl->mDeclination;
                system.mDeclinationVariation = partctrl->mDeclinationVariation;
                system.mLifetime = partctrl->mLifetime;
                system.mLifetimeVariation = partctrl->mLifetimeVariation;
                system.mEmitterDimensions = partctrl->mEmitterDimensions;
                system.mInitialSize = partctrl->mInitialSize;
                system.mInitialColor = partctrl->mInitialColor;
                system.mEmitStart = partctrl->mEmitStartTime;
                system.mEmitStop = partctrl->mEmitStopTime;
                system.mFrequency = partctrl->mFrequency;
                system.mPhase = partctrl->mPhase;
                system.mStartTime = partctrl->mTimeStart;
                system.mStopTime = partctrl->mTimeStop;
                system.mExtrapolation = static_cast<std::uint8_t>(partctrl->extrapolationMode());

                for (Nif::NiParticleModifierPtr modifier = partctrl->mModifier; !modifier.empty();
                     modifier = modifier->mNext)
                {
                    if (modifier->mRecordType == Nif::RC_NiParticleGrowFade)
                    {
                        const auto* gf = static_cast<const Nif::NiParticleGrowFade*>(modifier.getPtr());
                        system.mGrowTime = gf->mGrowTime;
                        system.mFadeTime = gf->mFadeTime;
                    }
                    else if (modifier->mRecordType == Nif::RC_NiGravity)
                    {
                        const auto* gr = static_cast<const Nif::NiGravity*>(modifier.getPtr());
                        ParticleGravity gravity;
                        gravity.mForce = gr->mForce;
                        gravity.mDecay = gr->mDecay;
                        gravity.mPoint = gr->mType == Nif::ForceType::Point;
                        gravity.mPosition = gr->mPosition;
                        gravity.mDirection = gr->mDirection;
                        gravity.mDirection.normalize();
                        system.mGravity.push_back(gravity);
                    }
                    else if (modifier->mRecordType == Nif::RC_NiParticleColorModifier)
                    {
                        const auto* cl = static_cast<const Nif::NiParticleColorModifier*>(modifier.getPtr());
                        if (!cl->mData.empty() && cl->mData->mKeyMap != nullptr)
                            for (const auto& [time, key] : cl->mData->mKeyMap->mKeys)
                                system.mColorKeys.push_back({ time, key.mValue });
                    }
                }

                std::uint32_t uvSet = 0;
                system.mMaterial = makeMaterial(props, true, uvSet);
                system.mMaterial.mFlags |= Mat_TwoSided;
                mModel.mParticleSystems.push_back(std::move(system));
            }

            Material makeMaterial(const PropertyState& props, bool hasVertexColors, std::uint32_t& uvSet)
            {
                Material material;
                uvSet = 0;
                if (props.mTexturing != nullptr && !props.mTexturing->mTextures.empty())
                {
                    const Nif::NiTexturingProperty::Texture& base
                        = props.mTexturing->mTextures[Nif::NiTexturingProperty::BaseTexture];
                    if (base.mEnabled && !base.mSourceTexture.empty() && base.mSourceTexture->mExternal
                        && mTextures != nullptr)
                    {
                        material.mTexture = mTextures->get(base.mSourceTexture->mFile);
                        uvSet = base.mUVSet;
                    }
                }

                if (props.mMaterial != nullptr)
                {
                    const Nif::NiMaterialProperty& m = *props.mMaterial;
                    material.mDiffuse = osg::Vec4f(m.mDiffuse, m.mAlpha);
                    material.mAmbient = m.mAmbient;
                    material.mEmissive = m.mEmissive * m.mEmissiveMult;
                }

                using VertexMode = Nif::NiVertexColorProperty::VertexMode;
                using LightMode = Nif::NiVertexColorProperty::LightMode;
                VertexMode vertexMode = VertexMode::VertMode_SrcAmbDif;
                LightMode lightMode = LightMode::LightMode_EmiAmbDif;
                if (props.mVertexColor != nullptr)
                {
                    vertexMode = props.mVertexColor->mVertexMode;
                    lightMode = props.mVertexColor->mLightingMode;
                }
                if (lightMode == LightMode::LightMode_Emissive)
                    material.mFlags |= Mat_Unlit;
                if (hasVertexColors)
                {
                    if (vertexMode == VertexMode::VertMode_SrcEmissive)
                        material.mFlags |= Mat_VertexEmissive;
                    else if (vertexMode == VertexMode::VertMode_SrcAmbDif
                        && lightMode != LightMode::LightMode_Emissive)
                        material.mFlags |= Mat_VertexAmbDiff;
                }

                if (props.mAlpha != nullptr)
                {
                    const Nif::NiAlphaProperty& alpha = *props.mAlpha;
                    if (alpha.useAlphaBlending())
                    {
                        material.mFlags |= Mat_AlphaBlend;
                        material.mSrcBlend = static_cast<std::uint8_t>(alpha.sourceBlendMode());
                        material.mDstBlend = static_cast<std::uint8_t>(alpha.destinationBlendMode());
                    }
                    if (alpha.useAlphaTesting())
                    {
                        material.mFlags |= Mat_AlphaTest;
                        material.mAlphaFunc = static_cast<std::uint8_t>(alpha.alphaTestMode());
                        material.mAlphaRef = alpha.mThreshold;
                    }
                }

                if (props.mStencil != nullptr)
                {
                    using DrawMode = Nif::NiStencilProperty::DrawMode;
                    if (props.mStencil->mDrawMode == DrawMode::Both)
                        material.mFlags |= Mat_TwoSided;
                    else if (props.mStencil->mDrawMode == DrawMode::Clockwise)
                        material.mFlags |= Mat_CullFront;
                }

                if (props.mZBuffer != nullptr)
                {
                    if (!props.mZBuffer->depthTest())
                        material.mFlags |= Mat_NoDepthTest;
                    if (!props.mZBuffer->depthWrite())
                        material.mFlags |= Mat_NoDepthWrite;
                }
                return material;
            }

            static void collectTriangles(const Nif::NiGeometryData& data, std::vector<std::uint16_t>& out)
            {
                if (data.mRecordType == Nif::RC_NiTriShapeData)
                {
                    const auto& triangles = static_cast<const Nif::NiTriShapeData&>(data).mTriangles;
                    out.assign(triangles.begin(), triangles.end());
                }
                else if (data.mRecordType == Nif::RC_NiTriStripsData)
                {
                    for (const auto& strip : static_cast<const Nif::NiTriStripsData&>(data).mStrips)
                    {
                        for (std::size_t i = 2; i < strip.size(); ++i)
                        {
                            std::uint16_t a = strip[i - 2], b = strip[i - 1], c = strip[i];
                            if (a == b || b == c || a == c)
                                continue;
                            if (i % 2 == 1)
                                std::swap(a, b);
                            out.push_back(a);
                            out.push_back(b);
                            out.push_back(c);
                        }
                    }
                }
            }

            void buildMesh(const Nif::NiGeometry* geom, int nodeIndex, const PropertyState& props, int switchIndex,
                int switchChild, bool billboard)
            {
                if (geom->mData.empty())
                    return;
                const Nif::NiGeometryData& data = *geom->mData.getPtr();
                std::vector<std::uint16_t> indices;
                collectTriangles(data, indices);
                const std::size_t numVertices = data.mVertices.size();
                if (indices.size() < 3 || numVertices == 0 || numVertices > 0xffff)
                    return;
                for (std::uint16_t index : indices)
                    if (index >= numVertices)
                        return;

                const bool hasColors = data.mColors.size() == numVertices;
                std::uint32_t uvSet = 0;
                Mesh mesh;
                mesh.mName = geom->mName;
                mesh.mNode = static_cast<std::int16_t>(nodeIndex);
                mesh.mSwitch = static_cast<std::int16_t>(switchIndex);
                mesh.mSwitchChild = static_cast<std::int8_t>(switchChild);
                mesh.mMaterial = makeMaterial(props, hasColors, uvSet);
                if (billboard)
                    mesh.mFlags |= Mesh_Billboard;
                if (Misc::StringUtils::ciStartsWith(geom->mName, "tri bip"))
                    mesh.mFlags |= Mesh_TriBip;
                if (hasColors && (mesh.mMaterial.mFlags & (Mat_VertexAmbDiff | Mat_VertexEmissive)))
                    mesh.mVertexFormat |= VF_Color;

                const std::vector<osg::Vec2f>* uvs = nullptr;
                if (uvSet < data.mUVList.size() && data.mUVList[uvSet].size() == numVertices)
                    uvs = &data.mUVList[uvSet];
                else if (!data.mUVList.empty() && data.mUVList[0].size() == numVertices)
                    uvs = &data.mUVList[0];
                const bool hasNormals = data.mNormals.size() == numVertices;

                std::vector<SourceVertex> vertices(numVertices);
                osg::BoundingBoxf box;
                float maxUv = 1.f;
                for (std::size_t i = 0; i < numVertices; ++i)
                {
                    SourceVertex& v = vertices[i];
                    v.mPosition = data.mVertices[i];
                    box.expandBy(v.mPosition);
                    if (uvs != nullptr)
                    {
                        v.mUv = (*uvs)[i];
                        maxUv = std::max({ maxUv, std::abs(v.mUv.x()), std::abs(v.mUv.y()) });
                    }
                    v.mNormal = hasNormals ? data.mNormals[i] : osg::Vec3f(0.f, 0.f, 1.f);
                    if (hasColors)
                        v.mColor = data.mColors[i];
                }

                const osg::Vec3f extent = (box._max - box._min) * 0.5f;
                mesh.mPosOffset = box.center();
                mesh.mPosScale = osg::Vec3f(std::max(extent.x(), 1e-3f) / 32767.f,
                    std::max(extent.y(), 1e-3f) / 32767.f, std::max(extent.z(), 1e-3f) / 32767.f);
                mesh.mUvScale = maxUv / 32767.f;

                if (!geom->mSkin.empty() && !geom->mSkin->mData.empty())
                {
                    if (buildSkin(*geom->mSkin.getPtr(), vertices, indices, mesh, data))
                    {
                        mModel.mHasSkin = true;
                        mModel.mMeshes.push_back(std::move(mesh));
                    }
                    return;
                }

                std::vector<std::uint16_t> order(numVertices);
                for (std::size_t i = 0; i < numVertices; ++i)
                    order[i] = static_cast<std::uint16_t>(i);
                mesh.mGeometry.mVertices = writeVertices(vertices, order, mesh, nullptr);
                mesh.mGeometry.mIndices = writeIndices(indices);
                if (mesh.mGeometry.mVertices == nullptr || mesh.mGeometry.mIndices == nullptr)
                {
                    Log(Debug::Error) << "Out of linear memory loading " << mModel.mPath;
                    return;
                }
                mesh.mGeometry.mVertexCount = static_cast<std::uint32_t>(numVertices);
                mesh.mGeometry.mIndexCount = static_cast<std::uint32_t>(indices.size());
                mesh.mBound = osg::BoundingSpheref(box);
                mModel.mGpuBytes += mesh.mGeometry.byteSize(vertexStride(mesh.mVertexFormat));
                mModel.mMeshes.push_back(std::move(mesh));
            }

            bool buildSkin(const Nif::NiSkinInstance& skin, std::vector<SourceVertex>& vertices,
                const std::vector<std::uint16_t>& indices, Mesh& mesh, const Nif::NiGeometryData& data)
            {
                const Nif::NiSkinData& skinData = *skin.mData.getPtr();
                const std::size_t numBones = std::min(skin.mBones.size(), skinData.mBones.size());
                if (numBones == 0 || numBones > 255)
                    return false;

                mesh.mVertexFormat |= VF_Skin;
                const osg::Matrixf dataTransform = skinData.mTransform.toMatrix();
                for (std::size_t i = 0; i < numBones; ++i)
                {
                    const Nif::NiAVObject* bone = skin.mBones[i].getPtr();
                    mesh.mBoneNames.push_back(bone != nullptr ? Misc::StringUtils::lowerCase(bone->mName) : "");
                    mesh.mBoneBind.push_back(dataTransform * skinData.mBones[i].mTransform.toMatrix());
                }

                std::vector<std::vector<std::pair<float, std::uint8_t>>> influences(vertices.size());
                for (std::size_t bone = 0; bone < numBones; ++bone)
                    for (const auto& [vertex, weight] : skinData.mBones[bone].mWeights)
                        if (vertex < influences.size() && weight > 0.f)
                            influences[vertex].emplace_back(weight, static_cast<std::uint8_t>(bone));
                for (std::size_t i = 0; i < vertices.size(); ++i)
                {
                    auto& list = influences[i];
                    std::sort(list.begin(), list.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
                    SourceVertex& v = vertices[i];
                    float sum = 0.f;
                    for (std::size_t k = 0; k < 3 && k < list.size(); ++k)
                        sum += list[k].first;
                    for (std::size_t k = 0; k < 3; ++k)
                    {
                        if (k < list.size() && sum > 0.f)
                        {
                            v.mBones[k] = list[k].second;
                            v.mWeights[k] = list[k].first / sum;
                        }
                        else
                        {
                            v.mBones[k] = k < list.size() ? list[k].second : (list.empty() ? 0 : list[0].second);
                            v.mWeights[k] = (k == 0 && list.empty()) ? 1.f : 0.f;
                        }
                    }
                }

                {
                    osg::BoundingBoxf box;
                    for (const SourceVertex& v : vertices)
                        box.expandBy(v.mPosition * dataTransform);
                    mesh.mBound = osg::BoundingSpheref(box);
                }

                std::vector<bool> used(indices.size() / 3, false);
                std::size_t remaining = used.size();
                while (remaining > 0)
                {
                    SkinPart part;
                    std::set<std::uint8_t> bones;
                    std::vector<std::uint16_t> partIndices;
                    for (std::size_t t = 0; t < used.size(); ++t)
                    {
                        if (used[t])
                            continue;
                        std::set<std::uint8_t> needed = bones;
                        for (int c = 0; c < 3; ++c)
                        {
                            const SourceVertex& v = vertices[indices[t * 3 + c]];
                            for (int k = 0; k < 3; ++k)
                                if (v.mWeights[k] > 0.f || k == 0)
                                    needed.insert(v.mBones[k]);
                        }
                        if (needed.size() > static_cast<std::size_t>(sMaxBonesPerDraw))
                        {
                            if (bones.empty())
                            {
                                used[t] = true;
                            }
                            continue;
                        }
                        bones.swap(needed);
                        used[t] = true;
                        for (int c = 0; c < 3; ++c)
                            partIndices.push_back(indices[t * 3 + c]);
                    }
                    if (partIndices.empty())
                        continue;

                    std::uint8_t remap[256] = {};
                    for (std::uint8_t bone : bones)
                    {
                        remap[bone] = static_cast<std::uint8_t>(part.mBones.size());
                        part.mBones.push_back(bone);
                    }

                    std::map<std::uint16_t, std::uint16_t> local;
                    std::vector<std::uint16_t> order;
                    for (std::uint16_t& index : partIndices)
                    {
                        auto found = local.find(index);
                        if (found == local.end())
                        {
                            found = local.emplace(index, static_cast<std::uint16_t>(order.size())).first;
                            order.push_back(index);
                        }
                        index = found->second;
                    }

                    part.mGeometry.mVertices = writeVertices(vertices, order, mesh, remap);
                    part.mGeometry.mIndices = writeIndices(partIndices);
                    if (part.mGeometry.mVertices == nullptr || part.mGeometry.mIndices == nullptr)
                    {
                        Log(Debug::Error) << "Out of linear memory loading " << mModel.mPath;
                        return false;
                    }
                    part.mGeometry.mVertexCount = static_cast<std::uint32_t>(order.size());
                    part.mGeometry.mIndexCount = static_cast<std::uint32_t>(partIndices.size());
                    mModel.mGpuBytes += part.mGeometry.byteSize(vertexStride(mesh.mVertexFormat));
                    mesh.mSkinParts.push_back(std::move(part));
                }
                (void)data;
                return !mesh.mSkinParts.empty();
            }

            void computeBounds()
            {
                std::vector<osg::Matrixf> world;
                mModel.computeBindPose(world);
                for (const Mesh& mesh : mModel.mMeshes)
                {
                    if (!mesh.mBound.valid())
                        continue;
                    if (mesh.isSkinned() || mesh.mNode < 0)
                    {
                        mModel.mBounds.expandBy(mesh.mBound);
                        continue;
                    }
                    const osg::Matrixf& m = world[mesh.mNode];
                    const osg::Vec3f center = mesh.mBound.center() * m;
                    const float scale = std::sqrt(std::max(
                        { m(0, 0) * m(0, 0) + m(0, 1) * m(0, 1) + m(0, 2) * m(0, 2),
                            m(1, 0) * m(1, 0) + m(1, 1) * m(1, 1) + m(1, 2) * m(1, 2),
                            m(2, 0) * m(2, 0) + m(2, 1) * m(2, 1) + m(2, 2) * m(2, 2) }));
                    mModel.mBounds.expandBy(osg::BoundingSpheref(center, mesh.mBound.radius() * scale));
                }
                for (const ParticleSystem& system : mModel.mParticleSystems)
                {
                    const int node = system.mEmitterNode >= 0 ? system.mEmitterNode : system.mNode;
                    const float radius = std::max(system.mBoundRadius,
                        system.mSpeedMax * (system.mLifetime + system.mLifetimeVariation) + system.mInitialSize);
                    mModel.mBounds.expandBy(osg::BoundingSpheref(world[node].getTrans(), std::max(radius, 16.f)));
                }
            }
        };
    }


    ModelCache::ModelCache(Resource::NifFileManager* nifFileManager, TextureCache* textures)
        : mNifFileManager(nifFileManager)
        , mTextures(textures)
    {
    }

    ModelCache::~ModelCache() = default;

    ModelPtr ModelCache::find(VFS::Path::NormalizedView path)
    {
        ModelPtr model = get(path);
        if (model->mNodes.empty())
            return nullptr;
        return model;
    }

    ModelPtr ModelCache::get(VFS::Path::NormalizedView path)
    {
        const std::string key(path.value());
        if (auto found = mModels.find(key); found != mModels.end())
            return found->second;

        static char beacon[128];
        std::snprintf(beacon, sizeof(beacon), "model %s", key.c_str());
        const char* const previousBeacon = g_ctrBeacon;
        g_ctrBeacon = beacon;

        auto model = std::make_shared<Model>();
        model->mPath = VFS::Path::Normalized(path);
        try
        {
            Nif::NIFFilePtr file = mNifFileManager->get(path);
            Builder builder(*model, mTextures, Nif::FileView(*file));
            builder.build();
        }
        catch (const std::exception& e)
        {
            Log(Debug::Error) << "Failed to load model " << path << ": " << e.what();
            model = std::make_shared<Model>();
            model->mPath = VFS::Path::Normalized(path);
        }
        g_ctrBeacon = previousBeacon;
        mModels.emplace(key, model);
        return model;
    }

    void ModelCache::purgeUnused()
    {
        for (auto it = mModels.begin(); it != mModels.end();)
        {
            if (it->second.use_count() == 1)
                it = mModels.erase(it);
            else
                ++it;
        }
    }

    void ModelCache::clear()
    {
        mModels.clear();
    }

    std::size_t ModelCache::getGpuBytes() const
    {
        std::size_t total = 0;
        for (const auto& [path, model] : mModels)
            total += model->mGpuBytes;
        return total;
    }
}
