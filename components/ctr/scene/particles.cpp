#include "particles.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include <osg/Quat>

#include <components/ctr/gpu/ctrgpu.hpp>
#include <components/misc/rng.hpp>

#include "renderer.hpp"

namespace CtrScene
{
    namespace
    {
        constexpr std::uint8_t sVertexFormat = VF_Color;

        float random01()
        {
            return Misc::Rng::rollClosedProbability();
        }

        std::int16_t quantize(float value, float offset, float scale)
        {
            return static_cast<std::int16_t>(std::clamp(std::round((value - offset) / scale), -32767.f, 32767.f));
        }

        std::uint8_t toByte(float value)
        {
            return static_cast<std::uint8_t>(std::clamp(std::round(value * 255.f), 0.f, 255.f));
        }

        osg::Vec4f interpolateColor(const std::vector<ParticleColorKey>& keys, float time)
        {
            if (time <= keys.front().mTime)
                return keys.front().mColor;
            if (time >= keys.back().mTime)
                return keys.back().mColor;
            for (std::size_t i = 1; i < keys.size(); ++i)
            {
                if (time < keys[i].mTime)
                {
                    const ParticleColorKey& a = keys[i - 1];
                    const ParticleColorKey& b = keys[i];
                    const float span = b.mTime - a.mTime;
                    const float t = span > 0.f ? (time - a.mTime) / span : 0.f;
                    return a.mColor * (1.f - t) + b.mColor * t;
                }
            }
            return keys.back().mColor;
        }

        osg::Matrixf orthonormalized(const osg::Matrixf& m)
        {
            osg::Matrixf result = m;
            for (int row = 0; row < 3; ++row)
            {
                osg::Vec3f axis(m(row, 0), m(row, 1), m(row, 2));
                const float length = axis.length();
                if (length > 1e-6f)
                    axis /= length;
                result(row, 0) = axis.x();
                result(row, 1) = axis.y();
                result(row, 2) = axis.z();
            }
            return result;
        }
    }

    ParticleInstance::ParticleInstance(const ParticleSystem& system)
        : mSystem(&system)
    {
        mParticles.reserve(system.mMaxParticles);
        mMesh.mName = "particles";
        mMesh.mVertexFormat = sVertexFormat;
        mMesh.mMaterial = system.mMaterial;
        mMesh.mUvScale = 1.f / 32767.f;
    }

    void ParticleInstance::emit(int count, const osg::Matrixf& emitterToParticles)
    {
        const ParticleSystem& s = *mSystem;
        const osg::Vec3f half = s.mEmitterDimensions * 0.5f;
        for (int i = 0; i < count && mParticles.size() < s.mMaxParticles; ++i)
        {
            Particle particle;
            const osg::Vec3f local(-half.x() + 2.f * half.x() * random01(), -half.y() + 2.f * half.y() * random01(),
                -half.z() + 2.f * half.z() * random01());
            const float hdir = s.mPlanarAngle + s.mPlanarAngleVariation * (2.f * random01() - 1.f);
            const float vdir = s.mDeclination + s.mDeclinationVariation * (2.f * random01() - 1.f);
            const osg::Vec3f dir
                = (osg::Quat(vdir, osg::Vec3f(0, 1, 0)) * osg::Quat(hdir, osg::Vec3f(0, 0, 1))) * osg::Vec3f(0, 0, 1);
            const float speed = s.mSpeedMin + (s.mSpeedMax - s.mSpeedMin) * random01();

            particle.mPosition = local * emitterToParticles;
            particle.mVelocity = osg::Matrixf::transform3x3(dir * speed, emitterToParticles);
            particle.mAge = 0.f;
            particle.mLifetime
                = std::max(std::numeric_limits<float>::epsilon(), s.mLifetime + s.mLifetimeVariation * random01());
            mParticles.push_back(particle);
        }
    }

    void ParticleInstance::update(float dt, float controllerInput, const osg::Matrixf& systemWorld,
        const osg::Matrixf& emitterWorld, bool emitterVisible)
    {
        if (controllerInput < 0.f || dt <= 0.f)
            return; // frozen
        const ParticleSystem& s = *mSystem;

        osg::Matrixf worldToParticles;
        if (!s.mWorldSpace)
            worldToParticles.invert(systemWorld);

        osg::Vec3f gravityDirections[4];
        osg::Vec3f gravityPositions[4];
        const std::size_t gravityCount = std::min<std::size_t>(s.mGravity.size(), 4);
        for (std::size_t g = 0; g < gravityCount; ++g)
        {
            const ParticleGravity& gravity = s.mGravity[g];
            gravityPositions[g] = s.mWorldSpace ? gravity.mPosition * systemWorld : gravity.mPosition;
            gravityDirections[g]
                = s.mWorldSpace ? osg::Matrixf::transform3x3(gravity.mDirection, systemWorld) : gravity.mDirection;
            gravityDirections[g].normalize();
        }

        constexpr float magic = 1.6f;
        for (std::size_t i = 0; i < mParticles.size();)
        {
            Particle& p = mParticles[i];
            p.mAge += dt;
            if (p.mAge >= p.mLifetime)
            {
                p = mParticles.back();
                mParticles.pop_back();
                continue;
            }
            for (std::size_t g = 0; g < gravityCount; ++g)
            {
                const ParticleGravity& gravity = s.mGravity[g];
                if (gravity.mPoint)
                {
                    osg::Vec3f diff = gravityPositions[g] - p.mPosition;
                    float decay = 1.f;
                    if (gravity.mDecay != 0.f)
                        decay = std::exp(-gravity.mDecay * diff.length());
                    diff.normalize();
                    p.mVelocity += diff * gravity.mForce * dt * decay * magic;
                }
                else
                {
                    float decay = 1.f;
                    if (gravity.mDecay != 0.f)
                    {
                        const float distance = std::abs((p.mPosition - gravityPositions[g]) * gravityDirections[g]);
                        decay = std::exp(-gravity.mDecay * distance);
                    }
                    p.mVelocity += gravityDirections[g] * gravity.mForce * dt * decay * magic;
                }
            }
            p.mPosition += p.mVelocity * dt;
            ++i;
        }

        const float time = s.controllerTime(controllerInput);
        const bool emitting = emitterVisible && time >= s.mEmitStart && time < s.mEmitStop && s.mBirthRate > 0.f;
        if (!emitting)
        {
            mEmitAccumulator = 0.f;
            return;
        }
        mEmitAccumulator += s.mBirthRate * dt;
        const int count = static_cast<int>(mEmitAccumulator);
        if (count <= 0)
            return;
        mEmitAccumulator -= static_cast<float>(count);
        osg::Matrixf emitterToParticles = orthonormalized(emitterWorld);
        if (!s.mWorldSpace)
            emitterToParticles = emitterToParticles * worldToParticles;
        emit(count, emitterToParticles);
    }

    void ParticleInstance::buildGeometry(const Renderer& renderer, const osg::Matrixf& systemWorld)
    {
        const ParticleSystem& s = *mSystem;
        GpuGeometry& geometry = mMesh.mGeometry;
        const std::uint16_t stride = vertexStride(sVertexFormat);
        if (geometry.mVertices == nullptr)
        {
            const std::size_t quads = std::max<std::size_t>(s.mMaxParticles, 1);
            geometry.mVertices = CtrGpu::linearAllocChecked(quads * 4 * stride);
            geometry.mIndices
                = static_cast<std::uint16_t*>(CtrGpu::linearAllocChecked(quads * 6 * sizeof(std::uint16_t)));
            if (geometry.mVertices == nullptr || geometry.mIndices == nullptr)
            {
                geometry = GpuGeometry();
                return;
            }
            for (std::size_t q = 0; q < quads; ++q)
            {
                const auto v = static_cast<std::uint16_t>(q * 4);
                std::uint16_t* index = geometry.mIndices + q * 6;
                index[0] = v;
                index[1] = static_cast<std::uint16_t>(v + 1);
                index[2] = static_cast<std::uint16_t>(v + 2);
                index[3] = v;
                index[4] = static_cast<std::uint16_t>(v + 2);
                index[5] = static_cast<std::uint16_t>(v + 3);
            }
            CtrGpu::flushLinear(geometry.mIndices, quads * 6 * sizeof(std::uint16_t));
        }

        const osg::Matrixf& view = renderer.getSceneParams().mView;
        const osg::Vec3f right(view(0, 0), view(1, 0), view(2, 0));
        const osg::Vec3f up(view(0, 1), view(1, 1), view(2, 1));
        const osg::Vec3f toCamera(view(0, 2), view(1, 2), view(2, 2));

        struct Quad
        {
            osg::Vec3f mCenter;
            float mHalfSize;
            osg::Vec4f mColor;
        };
        Quad quads[256];
        const std::size_t count = std::min<std::size_t>(mParticles.size(), 256);
        osg::BoundingBoxf box;
        const float systemScale = s.mWorldSpace ? 1.f : systemWorld.getScale().x();
        for (std::size_t i = 0; i < count; ++i)
        {
            const Particle& p = mParticles[i];
            Quad& quad = quads[i];
            quad.mCenter = s.mWorldSpace ? p.mPosition : p.mPosition * systemWorld;

            float size = s.mInitialSize;
            if (p.mAge < s.mGrowTime && s.mGrowTime != 0.f)
                size *= p.mAge / s.mGrowTime;
            if (p.mLifetime - p.mAge < s.mFadeTime && s.mFadeTime != 0.f)
                size *= (p.mLifetime - p.mAge) / s.mFadeTime;
            quad.mHalfSize = size * 0.5f * systemScale;

            quad.mColor = s.mColorKeys.empty() ? s.mInitialColor
                                               : interpolateColor(s.mColorKeys, p.mAge / p.mLifetime);

            box.expandBy(quad.mCenter - osg::Vec3f(quad.mHalfSize, quad.mHalfSize, quad.mHalfSize) * 1.5f);
            box.expandBy(quad.mCenter + osg::Vec3f(quad.mHalfSize, quad.mHalfSize, quad.mHalfSize) * 1.5f);
        }
        geometry.mIndexCount = 0;
        if (count == 0 || !box.valid())
            return;

        const osg::Vec3f extent = (box._max - box._min) * 0.5f;
        mMesh.mPosOffset = box.center();
        mMesh.mPosScale = osg::Vec3f(std::max(extent.x(), 1e-3f) / 32767.f, std::max(extent.y(), 1e-3f) / 32767.f,
            std::max(extent.z(), 1e-3f) / 32767.f);
        mMesh.mBound = osg::BoundingSpheref(box.center(), extent.length());

        static const float corners[4][2] = { { -1.f, -1.f }, { 1.f, -1.f }, { 1.f, 1.f }, { -1.f, 1.f } };
        static const float uvs[4][2] = { { 0.f, 1.f }, { 1.f, 1.f }, { 1.f, 0.f }, { 0.f, 0.f } };
        auto* out = static_cast<std::uint8_t*>(geometry.mVertices);
        for (std::size_t i = 0; i < count; ++i)
        {
            const Quad& quad = quads[i];
            for (int c = 0; c < 4; ++c)
            {
                const osg::Vec3f position
                    = quad.mCenter + (right * corners[c][0] + up * corners[c][1]) * quad.mHalfSize;
                auto* pos = reinterpret_cast<std::int16_t*>(out);
                pos[0] = quantize(position.x(), mMesh.mPosOffset.x(), mMesh.mPosScale.x());
                pos[1] = quantize(position.y(), mMesh.mPosOffset.y(), mMesh.mPosScale.y());
                pos[2] = quantize(position.z(), mMesh.mPosOffset.z(), mMesh.mPosScale.z());
                pos[3] = 0;
                auto* uv = reinterpret_cast<std::int16_t*>(out + 8);
                uv[0] = static_cast<std::int16_t>(uvs[c][0] * 32767.f);
                uv[1] = static_cast<std::int16_t>(uvs[c][1] * 32767.f);
                auto* normal = reinterpret_cast<std::int8_t*>(out + 12);
                normal[0] = static_cast<std::int8_t>(toCamera.x() * 127.f);
                normal[1] = static_cast<std::int8_t>(toCamera.y() * 127.f);
                normal[2] = static_cast<std::int8_t>(toCamera.z() * 127.f);
                normal[3] = 0;
                std::uint8_t* color = out + 16;
                color[0] = toByte(quad.mColor.r());
                color[1] = toByte(quad.mColor.g());
                color[2] = toByte(quad.mColor.b());
                color[3] = toByte(quad.mColor.a());
                out += stride;
            }
        }
        geometry.mVertexCount = static_cast<std::uint32_t>(count * 4);
        geometry.mIndexCount = static_cast<std::uint32_t>(count * 6);
    }

    void ParticleInstance::draw(Renderer& renderer, const osg::Matrixf& systemWorld, const DrawParams& params)
    {
        if (mParticles.empty())
            return;
        const std::uint32_t frame = CtrGpu::getFrameNumber();
        if (frame != mBuiltFrame)
        {
            mBuiltFrame = frame;
            buildGeometry(renderer, systemWorld);
        }
        if (mMesh.mGeometry.mIndexCount == 0)
            return;
        renderer.drawMesh(mMesh, osg::Matrixf::identity(), params);
    }

    void renderParticles(Renderer& renderer, const Model& model,
        std::vector<ParticleInstance>& instances, float& lastTime, float input,
        const std::vector<osg::Matrixf>& nodeWorld, const DrawParams& params)
    {
        const SceneParams& scene = renderer.getSceneParams();
        if (instances.empty() || !scene.mParticles)
            return;
        const float dt = lastTime < 0.f ? 0.f : std::clamp(scene.mTime - lastTime, 0.f, 0.1f);
        const bool advance = scene.mTime != lastTime;
        lastTime = scene.mTime;
        for (std::size_t i = 0; i < instances.size() && i < model.mParticleSystems.size(); ++i)
        {
            const ParticleSystem& system = model.mParticleSystems[i];
            if (system.mNode < 0 || static_cast<std::size_t>(system.mNode) >= nodeWorld.size())
                continue;
            const osg::Matrixf& systemWorld = nodeWorld[system.mNode];
            if (advance)
            {
                const bool hasEmitter
                    = system.mEmitterNode >= 0 && static_cast<std::size_t>(system.mEmitterNode) < nodeWorld.size();
                const osg::Matrixf& emitterWorld = hasEmitter ? nodeWorld[system.mEmitterNode] : systemWorld;
                const bool emitterVisible
                    = hasEmitter && !(model.mNodes[system.mEmitterNode].mFlags & Node_Hidden);
                instances[i].update(dt, input, systemWorld, emitterWorld, emitterVisible);
            }
            instances[i].draw(renderer, systemWorld, params);
        }
    }

}
