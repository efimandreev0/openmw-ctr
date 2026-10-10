#ifndef OPENMW_COMPONENTS_CTR_SCENE_PARTICLES_H
#define OPENMW_COMPONENTS_CTR_SCENE_PARTICLES_H

#include <cstdint>
#include <vector>

#include <osg/Matrixf>
#include <osg/Vec3f>

#include "model.hpp"

namespace CtrScene
{
    class Renderer;
    struct DrawParams;

    class ParticleInstance
    {
    public:
        explicit ParticleInstance(const ParticleSystem& system);

        void update(float dt, float controllerInput, const osg::Matrixf& systemWorld,
            const osg::Matrixf& emitterWorld, bool emitterVisible);

        void draw(Renderer& renderer, const osg::Matrixf& systemWorld, const DrawParams& params);

        bool empty() const { return mParticles.empty(); }

    private:
        struct Particle
        {
            osg::Vec3f mPosition; ///< particle space
            osg::Vec3f mVelocity;
            float mAge;
            float mLifetime;
        };

        void emit(int count, const osg::Matrixf& emitterToParticles);
        void buildGeometry(const Renderer& renderer, const osg::Matrixf& systemWorld);

        const ParticleSystem* mSystem;
        std::vector<Particle> mParticles;
        float mEmitAccumulator = 0.f;
        Mesh mMesh;
        std::uint32_t mBuiltFrame = ~0u;
    };

    void renderParticles(Renderer& renderer, const Model& model, std::vector<ParticleInstance>& instances,
        float& lastTime, float input, const std::vector<osg::Matrixf>& nodeWorld, const DrawParams& params);
}

#endif
