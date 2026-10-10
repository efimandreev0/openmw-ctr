#include "renderingmanager.hpp"

#include <3ds.h>

#include <algorithm>
#include <vector>
#include <cmath>
#include <limits>

#include <components/debug/debuglog.hpp>
#include <components/esm3/loadcell.hpp>
#include <components/esm/util.hpp>
#include <components/esm3/loadland.hpp>
#include <components/esmterrain/landobject.hpp>
#include <components/misc/constants.hpp>
#include <components/misc/resourcehelpers.hpp>
#include <components/resource/resourcesystem.hpp>
#include <components/sceneutil/positionattitudetransform.hpp>
#include <components/sceneutil/colourutil.hpp>
#include <components/settings/values.hpp>

#include "../../mwbase/environment.hpp"
#include "../../mwbase/world.hpp"
#include "../../mwbase/windowmanager.hpp"

#include "../../mwworld/cell.hpp"
#include "../../mwworld/cellstore.hpp"
#include "../../mwworld/class.hpp"
#include "../../mwworld/ptr.hpp"

#include "../../mwmechanics/actorutil.hpp"

#include "../../mwphysics/collisiontype.hpp"
#include "../../mwphysics/raycasting.hpp"

#include "../fogmanager.hpp"
#include "../landmanager.hpp"
#include "../vismask.hpp"

#include "../../mwworld/datetimemanager.hpp"

#include "animation.hpp"
#include "characterpreview.hpp"
#include "localmap.hpp"
#include "camera.hpp"
#include "context.hpp"
#include "npcanimation.hpp"
#include "sky.hpp"
#include "terrain.hpp"
#include "skyrender.hpp"
#include "water.hpp"

namespace MWRender
{
    struct RenderingManager::Effect
    {
        CtrScene::ModelPtr mModel;
        std::vector<osg::Matrixf> mBindPose;
        osg::Matrixf mWorld;
        std::string mEffectId;
        float mTime = 0.f;
        float mMaxTime = 0.f;
        bool mLoop = false;
        bool mFullAmbient = true;
        std::vector<CtrScene::ParticleInstance> mParticles;
        float mParticleTime = -1.f;
        std::vector<osg::Matrixf> mNodeWorld;
    };

    namespace
    {
        FogManager& fog()
        {
            static FogManager sFog;
            return sFog;
        }

        bool isPickedByBounds(const MWWorld::ConstPtr& ptr)
        {
            switch (ptr.getType())
            {
                case ESM::REC_WEAP:
                case ESM::REC_ARMO:
                case ESM::REC_CLOT:
                case ESM::REC_BOOK:
                case ESM::REC_ALCH:
                case ESM::REC_INGR:
                case ESM::REC_MISC:
                case ESM::REC_APPA:
                case ESM::REC_LOCK:
                case ESM::REC_PROB:
                case ESM::REC_REPA:
                case ESM::REC_LIGH:
                    return true;
                default:
                    return false;
            }
        }

        bool intersectBox(const osg::Vec3f& from, const osg::Vec3f& to, const osg::BoundingBoxf& box, float& ratio)
        {
            const osg::Vec3f dir = to - from;
            float tmin = 0.f, tmax = 1.f;
            for (int i = 0; i < 3; ++i)
            {
                if (std::abs(dir[i]) < 1e-6f)
                {
                    if (from[i] < box._min[i] || from[i] > box._max[i])
                        return false;
                    continue;
                }
                float t1 = (box._min[i] - from[i]) / dir[i];
                float t2 = (box._max[i] - from[i]) / dir[i];
                if (t1 > t2)
                    std::swap(t1, t2);
                tmin = std::max(tmin, t1);
                tmax = std::min(tmax, t2);
                if (tmin > tmax)
                    return false;
            }
            ratio = tmin;
            return true;
        }

        osg::Matrixf objectMatrix(const SceneUtil::PositionAttitudeTransform& node)
        {
            osg::Matrixf m;
            m.makeScale(node.getScale());
            m.postMultRotate(node.getAttitude());
            m.postMultTranslate(node.getPosition());
            return m;
        }
    }

    RenderingManager* RenderingManager::sInstance = nullptr;

    RenderingManager::RenderingManager(
        Resource::ResourceSystem* resourceSystem, const std::shared_ptr<SceneUtil::WorkQueue>& workQueue)
        : mResourceSystem(resourceSystem)
        , mWorkQueue(workQueue)
        , mNightEyeFactor(0.f)
        , mNearClip(Settings::camera().mNearClip)
        , mViewDistance(Settings::camera().mViewingDistance)
        , mFieldOfViewOverridden(false)
        , mFieldOfViewOverride(0.f)
        , mFieldOfView(Settings::camera().mFieldOfView)
        , mFirstPersonFieldOfView(Settings::camera().mFirstPersonFieldOfView)
    {
        mContext = std::make_unique<CtrContext>(resourceSystem);
        mObjects = std::make_unique<Objects>(resourceSystem);
        mLandManager = std::make_unique<LandManager>(
            ESM::Land::DATA_VHGT | ESM::Land::DATA_VNML | ESM::Land::DATA_VCLR | ESM::Land::DATA_VTEX);
        mTerrain = std::make_unique<CtrTerrain>(mLandManager.get());
        mWater = std::make_unique<CtrWater>(mContext->getTextures());
        mSkyRenderer = std::make_unique<CtrSkyRenderer>(mContext->getTextures());
        mSky = std::make_unique<SkyManager>();
        mCamera = std::make_unique<Camera>();
        sInstance = this;
    }

    RenderingManager::~RenderingManager()
    {
        if (sInstance == this)
            sInstance = nullptr;
        mEffects.clear();
        mPlayerAnimation = nullptr;
        mObjects.reset();
        mTerrain.reset();
        mWater.reset();
        mSkyRenderer.reset();
        mContext.reset();
    }

    MWRender::Objects& RenderingManager::getObjects()
    {
        return *mObjects;
    }

    Resource::ResourceSystem* RenderingManager::getResourceSystem()
    {
        return mResourceSystem;
    }

    const std::shared_ptr<SceneUtil::WorkQueue>& RenderingManager::getWorkQueue() const
    {
        return mWorkQueue;
    }

    double RenderingManager::getReferenceTime() const
    {
        return mReferenceTime;
    }

    void RenderingManager::setNightEyeFactor(float factor)
    {
        mNightEyeFactor = factor;
    }

    void RenderingManager::setAmbientColour(const osg::Vec4f& colour)
    {
        mAmbientColor = colour;
    }

    int RenderingManager::skyGetMasserPhase() const
    {
        return mSky->getMasserPhase();
    }

    int RenderingManager::skyGetSecundaPhase() const
    {
        return mSky->getSecundaPhase();
    }

    void RenderingManager::skySetMoonColour(bool red)
    {
        mSky->setMoonColour(red);
    }

    void RenderingManager::configureAmbient(const MWWorld::Cell& cell)
    {
        mInterior = !cell.isExterior() && !cell.isQuasiExterior();
        osg::Vec4f ambient = SceneUtil::colourFromRGB(cell.getMood().mAmbiantColor);
        if (mInterior)
        {
            constexpr float minimumLuminance = 0.08f;
            const float luminance = 0.2126f * ambient.r() + 0.7152f * ambient.g() + 0.0722f * ambient.b();
            if (luminance < minimumLuminance)
            {
                if (luminance <= 0.f)
                    ambient = osg::Vec4f(minimumLuminance, minimumLuminance, minimumLuminance, ambient.a());
                else
                    ambient *= minimumLuminance / luminance;
            }
        }
        setAmbientColour(ambient);

        osg::Vec4f diffuse = SceneUtil::colourFromRGB(cell.getMood().mDirectionalColor);
        setSunColour(diffuse, diffuse, 0.f);
        mSunPosition = osg::Vec4f(-1.f, osg::DegreesToRadians(45.f), osg::DegreesToRadians(45.f), 0.f);
    }

    void RenderingManager::setSunColour(const osg::Vec4f& diffuse, const osg::Vec4f& specular, float sunVis)
    {
        mSunColor = diffuse;
    }

    void RenderingManager::setSunDirection(const osg::Vec3f& direction)
    {
        osg::Vec3f position = -direction;
        position.z() = 400.f - std::abs(position.x());
        const osg::Vec3f sunlightPos = Settings::shaders().mMatchSunlightToSun ? position : -direction;
        mSunPosition = osg::Vec4f(sunlightPos, 0.f);
        mSky->setSunDirection(position);
    }

    void RenderingManager::configureFog(const MWWorld::Cell& cell)
    {
        fog().configure(mViewDistance, cell);
    }

    void RenderingManager::configureFog(
        float fogDepth, float underwaterFog, float dlFactor, float dlOffset, const osg::Vec4f& color)
    {
        fog().configure(mViewDistance, fogDepth, underwaterFog, dlFactor, dlOffset, color);
    }

    SkyManager* RenderingManager::getSkyManager()
    {
        return mSky.get();
    }

    void RenderingManager::addCell(const MWWorld::CellStore* store)
    {
        const MWWorld::Cell& cell = *store->getCell();
        if (cell.isExterior() && cell.getWorldSpace() == ESM::Cell::sDefaultWorldspaceId)
            mTerrain->addCell(cell.getGridX(), cell.getGridY());
    }

    void RenderingManager::removeCell(const MWWorld::CellStore* store)
    {
        mObjects->removeCell(store);
        const MWWorld::Cell& cell = *store->getCell();
        if (cell.isExterior())
            mTerrain->removeCell(cell.getGridX(), cell.getGridY());
    }

    void RenderingManager::enableTerrain(bool enable, ESM::RefId worldspace)
    {
        mTerrain->setEnabled(enable && worldspace == ESM::Cell::sDefaultWorldspaceId);
        if (!enable)
            mTerrain->clear();
    }

    void RenderingManager::setSkyEnabled(bool enabled)
    {
        mSkyEnabled = enabled;
        mSky->setEnabled(enabled);
    }

    bool RenderingManager::toggleRenderMode(RenderMode mode)
    {
        if (mode == Render_Scene)
        {
            mRenderScene = !mRenderScene;
            return mRenderScene;
        }
        if (mode == Render_Water)
        {
            mWaterEnabled = !mWaterEnabled;
            return mWaterEnabled;
        }
        return false;
    }

    void RenderingManager::setWaterEnabled(bool enabled)
    {
        mWaterEnabled = enabled;
        mSky->setWaterEnabled(enabled);
    }

    void RenderingManager::setWaterHeight(float height)
    {
        mWaterHeight = height;
        mSky->setWaterHeight(height);
    }

    void RenderingManager::updatePtr(const MWWorld::Ptr& old, const MWWorld::Ptr& updated)
    {
        mObjects->updatePtr(old, updated);
    }

    void RenderingManager::rotateObject(const MWWorld::Ptr& ptr, const osg::Quat& rot)
    {
        if (ptr == mCamera->getTrackingPtr() && !mCamera->isVanityOrPreviewModeEnabled())
            mCamera->rotateCameraToTrackingPtr();

        ptr.getRefData().getBaseNode()->setAttitude(rot);
    }

    void RenderingManager::moveObject(const MWWorld::Ptr& ptr, const osg::Vec3f& pos)
    {
        ptr.getRefData().getBaseNode()->setPosition(pos);
    }

    void RenderingManager::scaleObject(const MWWorld::Ptr& ptr, const osg::Vec3f& scale)
    {
        ptr.getRefData().getBaseNode()->setScale(scale);

        if (ptr == mCamera->getTrackingPtr()) // update height of camera
            mCamera->processViewChange();
    }

    void RenderingManager::removeObject(const MWWorld::Ptr& ptr)
    {
        mObjects->removeObject(ptr);
    }

    void RenderingManager::spawnEffect(VFS::Path::NormalizedView model, std::string_view /*texture*/,
        const osg::Vec3f& worldPosition, float scale, bool /*isMagicVFX*/, bool useAmbientLight,
        std::string_view effectId, bool loop)
    {
        auto effect = std::make_unique<Effect>();
        effect->mModel = mContext->getModels().get(model);
        effect->mModel->computeBindPose(effect->mBindPose);
        effect->mWorld = osg::Matrixf::scale(scale, scale, scale) * osg::Matrixf::translate(worldPosition);
        for (const CtrScene::ParticleSystem& system : effect->mModel->mParticleSystems)
            effect->mParticles.emplace_back(system);
        effect->mNodeWorld.resize(effect->mBindPose.size());
        for (std::size_t i = 0; i < effect->mBindPose.size(); ++i)
            effect->mNodeWorld[i] = effect->mBindPose[i] * effect->mWorld;
        effect->mEffectId = effectId;
        effect->mMaxTime = effect->mModel->mMaxControllerTime;
        effect->mLoop = loop;
        effect->mFullAmbient = useAmbientLight;
        mEffects.push_back(std::move(effect));
    }

    std::shared_ptr<ProjectileVisual> RenderingManager::createProjectileVisual(VFS::Path::NormalizedView model,
        VFS::Path::NormalizedView texture, const osg::Vec3f& pos, const osg::Quat& orient, bool rotate,
        const osg::Vec4f* lightDiffuse)
    {
        auto visual = std::make_shared<ProjectileVisual>();
        visual->mModel = mContext->getModels().get(model);
        visual->mModel->computeBindPose(visual->mBindPose);
        if (!texture.value().empty())
            visual->mTexture = mContext->getModels().getTextureCache()->get(texture.value());
        visual->mPosition = pos;
        visual->mAttitude = orient;
        visual->mRotate = rotate;
        if (lightDiffuse != nullptr)
        {
            visual->mHasLight = true;
            visual->mLight.mColor = osg::Vec3f(lightDiffuse->r(), lightDiffuse->g(), lightDiffuse->b());
            visual->mLight.mRadius = 66.f * 3.f;
            visual->mLight.mPosition = pos;
        }
        std::erase_if(mProjectiles, [](const std::weak_ptr<ProjectileVisual>& p) { return p.expired(); });
        mProjectiles.push_back(visual);
        return visual;
    }

    void RenderingManager::removeEffect(std::string_view effectId)
    {
        std::erase_if(mEffects, [&](const std::unique_ptr<Effect>& effect) { return effect->mEffectId == effectId; });
    }

    void RenderingManager::clear()
    {
        mEffects.clear();
        mSky->setMoonColour(false);
        notifyWorldSpaceChanged();
    }

    void RenderingManager::notifyWorldSpaceChanged()
    {
        mEffects.clear();
        mTerrain->clear();
        mContext->purgeUnused();
    }

    void RenderingManager::update(float dt, bool paused)
    {
        if (!paused)
        {
            mReferenceTime += dt;
            for (auto it = mEffects.begin(); it != mEffects.end();)
            {
                Effect& effect = **it;
                effect.mTime += dt;
                if (effect.mTime >= effect.mMaxTime)
                {
                    if (!effect.mLoop)
                    {
                        it = mEffects.erase(it);
                        continue;
                    }
                    effect.mTime = 0.f;
                }
                ++it;
            }
            mObjects->forEach([dt](Animation& animation) { animation.advanceEffects(dt); });
            if (mPlayerAnimation)
                mPlayerAnimation->advanceEffects(dt);
            mSky->update(dt, MWBase::Environment::get().getWorld()->getTimeManager()->getGameTimeScale());
        }

        mCamera->update(dt, paused);
    }

    void RenderingManager::pickLights(const osg::BoundingSpheref& bound, CtrScene::DrawParams& params) const
    {
        params.mNumLights = 0;
        float distances[CtrScene::sMaxLightsPerDraw];
        for (const CtrScene::PointLight* light : mFrameLights)
        {
            const float reach = light->mRadius + bound.radius();
            const float distance2 = (light->mPosition - bound.center()).length2();
            if (distance2 > reach * reach)
                continue;
            int slot = params.mNumLights;
            if (slot == CtrScene::sMaxLightsPerDraw)
            {
                slot = 0;
                for (int i = 1; i < CtrScene::sMaxLightsPerDraw; ++i)
                    if (distances[i] > distances[slot])
                        slot = i;
                if (distances[slot] <= distance2)
                    continue;
            }
            else
                ++params.mNumLights;
            params.mLights[slot] = light;
            distances[slot] = distance2;
        }
    }

    bool RenderingManager::renderMapView(CtrGpu::Texture& texture, const osg::Vec2f& center, const osg::Vec2f& up,
        float halfSize, float zMin, float zMax)
    {
        C3D_RenderTarget* target = texture.getRenderTarget();
        if (target == nullptr)
            return true;
        const std::uint32_t skippedBefore = CtrGpu::getSkippedDraws();
        CtrScene::Renderer& renderer = mContext->getRenderer();

        CtrScene::SceneParams scene;
        scene.mView.makeLookAt(osg::Vec3f(center, zMax + 5.f), osg::Vec3f(center, zMin), osg::Vec3f(up, 0.f));
        scene.mNear = 5.f;
        scene.mFar = (zMax - zMin) + 10.f;
        scene.mOrthoHalfSize = halfSize;
        scene.mFog = false;
        scene.mParticles = false;
        scene.mTarget = target;
        scene.mClearColor = osg::Vec4f(0.f, 0.f, 0.f, 1.f);
        scene.mAmbient = osg::Vec3f(0.5f, 0.5f, 0.5f);
        scene.mSunDirection = osg::Vec3f(-0.3f, -0.3f, 0.7f);
        scene.mSunColor = osg::Vec3f(0.7f, 0.7f, 0.7f);
        renderer.beginScene(scene);

        CtrScene::DrawParams terrainParams;
        mTerrain->draw(renderer, terrainParams);

        mObjects->forEach([&](const Animation& animation) {
            if (!animation.isRenderable() || animation.getPtr().getClass().isActor())
                return;
            if (!renderer.isVisible(animation.getWorldBound()))
                return;
            CtrScene::DrawParams params;
            animation.render(renderer, params);
        });

        renderer.endScene();
        return CtrGpu::getSkippedDraws() == skippedBefore;
    }

    std::uint32_t RenderingManager::renderCharacterPreview(
        CtrGpu::Texture& texture, const Animation* animation, const osg::Matrixf& view, float fovY)
    {
        C3D_RenderTarget* target = texture.getRenderTarget();
        if (target == nullptr)
            return 0;
        CtrScene::Renderer& renderer = mContext->getRenderer();

        CtrScene::SceneParams scene;
        scene.mView = view;
        scene.mFovY = fovY;
        scene.mNear = 10.f;
        scene.mFar = 1000.f;
        scene.mTargetAspect = static_cast<float>(texture.getWidth()) / static_cast<float>(texture.getHeight());
        scene.mFog = false;
        scene.mParticles = false;
        scene.mTarget = target;
        scene.mClearColor = osg::Vec4f(0.12f, 0.11f, 0.1f, 1.f);
        scene.mAmbient = osg::Vec3f(0.45f, 0.45f, 0.45f);
        scene.mSunDirection = osg::Vec3f(0.3f, 1.f, 0.6f);
        scene.mSunColor = osg::Vec3f(0.75f, 0.75f, 0.75f);
        renderer.beginScene(scene);
        if (animation != nullptr)
        {
            CtrScene::DrawParams params;
            animation->render(renderer, params);
        }
        renderer.endScene();
        return renderer.getStats().mDraws;
    }

    void RenderingManager::handleDebugInput(CtrScene::Renderer& renderer, const CtrScene::SceneParams& scene)
    {
        const u32 held = hidKeysHeld();
        const u32 down = hidKeysDown();
        constexpr u32 debugKeys = KEY_X | KEY_Y | KEY_A | KEY_B | KEY_DUP | KEY_DLEFT | KEY_DRIGHT | KEY_DDOWN;
        if ((held & (KEY_L | KEY_R)) != (KEY_L | KEY_R) || (down & debugKeys) == 0)
            return;
        MWBase::WindowManager* windowManager = MWBase::Environment::get().getWindowManager();

        if (down & KEY_A)
        {
            mDebugHideWater = !mDebugHideWater;
            Log(Debug::Info) << "[Visibility] water " << (mDebugHideWater ? "hidden" : "shown");
            windowManager->messageBox(mDebugHideWater ? "Water hidden" : "Water shown");
        }
        if (down & KEY_B)
        {
            mDebugHideCameraInside = !mDebugHideCameraInside;
            Log(Debug::Info) << "[Visibility] objects around the camera " << (mDebugHideCameraInside ? "hidden" : "shown");
            windowManager->messageBox(
                mDebugHideCameraInside ? "Objects around the camera hidden" : "Objects around the camera shown");
        }

        if (down & KEY_DDOWN)
        {
            static Debug::Level sSavedLevel = Debug::All;
            const bool quiet = Log::sMinDebugLevel > Debug::Error;
            if (quiet)
            {
                sSavedLevel = Log::sMinDebugLevel;
                Log(Debug::Info) << "[Log] logging errors only";
                Log::sMinDebugLevel = Debug::Error;
            }
            else
            {
                Log::sMinDebugLevel = sSavedLevel;
                Log(Debug::Info) << "[Log] logging everything";
            }
            windowManager->messageBox(quiet ? "Logging off (errors only)" : "Logging on");
        }
        if (down & KEY_DUP)
        {
            renderer.setDebugNoFog(!renderer.getDebugNoFog());
            Log(Debug::Info) << "[Visibility] fog " << (renderer.getDebugNoFog() ? "off" : "on");
            windowManager->messageBox(renderer.getDebugNoFog() ? "Fog off" : "Fog on");
        }
        if (down & KEY_DLEFT)
        {
            renderer.setDebugNoTextures(!renderer.getDebugNoTextures());
            Log(Debug::Info) << "[Visibility] textures " << (renderer.getDebugNoTextures() ? "off" : "on");
            windowManager->messageBox(renderer.getDebugNoTextures() ? "Textures off" : "Textures on");
        }
        if (down & KEY_DRIGHT)
        {
            renderer.setDebugTwoSided(!renderer.getDebugTwoSided());
            Log(Debug::Info) << "[Visibility] back faces " << (renderer.getDebugTwoSided() ? "drawn" : "culled");
            windowManager->messageBox(renderer.getDebugTwoSided() ? "Back faces drawn" : "Back faces culled");
        }
        if (down & KEY_X)
        {
            renderer.setCullingEnabled(!renderer.isCullingEnabled());
            Log(Debug::Info) << "[Visibility] culling " << (renderer.isCullingEnabled() ? "on" : "off");
            windowManager->messageBox(renderer.isCullingEnabled() ? "Culling on" : "Culling off");
        }
        if ((down & KEY_Y) == 0)
            return;

        const osg::Matrixf cameraWorld = osg::Matrixf::inverse(scene.mView);
        const osg::Vec3f cameraPos = cameraWorld.getTrans();
        const osg::Vec3f cameraDir = osg::Matrixf::transform3x3(osg::Vec3f(0.f, 0.f, -1.f), scene.mView);
        const MWWorld::Ptr player = MWMechanics::getPlayer();
        const MWWorld::CellStore* cell = player.isEmpty() ? nullptr : player.getCell();
        Log(Debug::Info) << "[Visibility] report: camera " << cameraPos.x() << "," << cameraPos.y() << ","
                         << cameraPos.z() << " looking " << cameraDir.x() << "," << cameraDir.y() << ","
                         << cameraDir.z() << " mode " << static_cast<int>(mCamera->getMode()) << " fov "
                         << scene.mFovY << " near " << scene.mNear << " far " << scene.mFar << " culling "
                         << (renderer.isCullingEnabled() ? "on" : "off") << " cell "
                         << (cell != nullptr ? cell->getCell()->getDescription() : std::string_view("?"))
                         << " water " << (mWaterEnabled ? "on" : "off") << " at " << mWaterHeight
                         << (mDebugHideWater ? " (hidden)" : "") << " hide-around-camera " << mDebugHideCameraInside
                         << " fog " << scene.mFogStart << ".." << scene.mFogEnd
                         << (renderer.getDebugNoFog() ? " (off)" : "") << " textures "
                         << !renderer.getDebugNoTextures() << " back faces culled " << !renderer.getDebugTwoSided()
                         << " last frame draws " << renderer.getScreenStats().mDraws << " skipped draws so far "
                         << CtrGpu::getSkippedDraws();
        std::vector<std::pair<float, const Animation*>> nearby;
        int culled = 0;
        mObjects->forEach([&](const Animation& animation) {
            const osg::BoundingSpheref bound = animation.getWorldBound();
            const float distance = (bound.center() - cameraPos).length() - bound.radius();
            if (!renderer.isVisible(bound))
                ++culled;
            if (distance <= 800.f)
                nearby.emplace_back(distance, &animation);
        });
        std::sort(nearby.begin(), nearby.end(),
            [](const auto& a, const auto& b) { return a.first < b.first; });
        int listed = 0;
        for (const auto& [distance, animationPtr] : nearby)
        {
            if (listed >= 80)
                break;
            const Animation& animation = *animationPtr;
            const osg::BoundingSpheref bound = animation.getWorldBound();
            const bool visible = renderer.isVisible(bound);
            ++listed;
            const osg::Vec3f view = bound.center() * scene.mView;
            const CtrScene::Model* model = animation.getObjectModel();
            Log(Debug::Info) << "[Visibility] " << animation.getPtr().getCellRef().getRefId() << " model "
                             << (model != nullptr ? model->mPath.value() : std::string_view("-")) << " meshes "
                             << (model != nullptr ? model->mMeshes.size() : 0) << " renderable "
                             << animation.isRenderable() << " visible " << visible << " distance " << distance
                             << " bound " << bound.center().x() << "," << bound.center().y() << ","
                             << bound.center().z() << " r " << bound.radius() << " view " << view.x() << ","
                             << view.y() << "," << view.z();
            if (listed <= 12)
                animation.logMeshes(scene.mView);
        }
        Log(Debug::Info) << "[Visibility] " << listed << " objects within 800 units listed, " << culled
                         << " culled in total";
        windowManager->messageBox("Visibility report written to openmw.log");
    }

    osg::BoundingBox RenderingManager::getStaticSceneBounds() const
    {
        osg::BoundingBox bounds;
        mObjects->forEach([&](Animation& animation) {
            const MWWorld::Ptr ptr = animation.getPtr();
            if (!animation.isRenderable() || ptr.getClass().isActor())
                return;
            const osg::BoundingBox box = getCullSafeBoundingBox(ptr);
            if (box.valid())
                bounds.expandBy(box);
        });
        return bounds;
    }

    void RenderingManager::draw()
    {
        CtrScene::Renderer& renderer = mContext->getRenderer();

        RaceSelectionPreview::renderPending(*this);

        mCamera->updateCamera();

        osg::Vec4f ambient = mAmbientColor;
        if (mNightEyeFactor > 0.f)
            ambient += osg::Vec4f(0.7f, 0.7f, 0.7f, 0.0f) * mNightEyeFactor;

        const bool underwater
            = mWaterEnabled && MWBase::Environment::get().getWorld()->isUnderwater(MWMechanics::getPlayer().getCell(),
                osg::Vec3f(mCamera->getPosition()));

        CtrScene::SceneParams scene;
        scene.mView = mCamera->getViewMatrix();
        scene.mTime = static_cast<float>(mReferenceTime);
        scene.mFovY = mFieldOfViewOverridden ? mFieldOfViewOverride : mFieldOfView;
        scene.mNear = mNearClip;
        scene.mFogEnd = std::min(fog().getFogEnd(underwater), mViewDistance);
        scene.mFogStart = std::min(fog().getFogStart(underwater), scene.mFogEnd - 1.f);
        scene.mFar = std::max(scene.mFogEnd, mNearClip + 1.f);
        scene.mAmbient = osg::Vec3f(ambient.r(), ambient.g(), ambient.b());
        osg::Vec3f sunDirection(mSunPosition.x(), mSunPosition.y(), mSunPosition.z());
        sunDirection.normalize();
        scene.mSunDirection = sunDirection;
        scene.mSunColor = osg::Vec3f(mSunColor.r(), mSunColor.g(), mSunColor.b());
        scene.mFogColor = fog().getFogColor(underwater);
        const bool drawSky = !(mInterior || underwater || !mSkyEnabled);
        scene.mClearColor = drawSky ? mSky->getSkyColor() : scene.mFogColor;

        mFrameLights.clear();
        const float time = static_cast<float>(mReferenceTime);
        mObjects->forEach([this, time](Animation& animation) { animation.collectLights(mFrameLights, time); });
        if (mPlayerAnimation)
            mPlayerAnimation->collectLights(mFrameLights, time);
        for (const auto& weak : mProjectiles)
        {
            if (const auto visual = weak.lock(); visual && visual->mHasLight)
            {
                visual->mLight.mPosition = visual->mPosition;
                mFrameLights.push_back(&visual->mLight);
            }
        }

        if (drawSky && mRenderScene)
        {
            mSkyRenderer->draw(renderer, *mSky, scene.mView, scene.mFovY, scene.mClearColor, scene.mFogColor);
            scene.mClearColorBuffer = false;
        }

        renderer.beginScene(scene);
        handleDebugInput(renderer, scene);

        if (mRenderScene)
        {
            CtrScene::DrawParams terrainParams;
            mTerrain->draw(renderer, terrainParams);

            const osg::Vec3f cameraPos = osg::Matrixf::inverse(scene.mView).getTrans();
            float minRadiusPerDistance = 0.f;
            if (Settings::camera().mSmallFeatureCulling && renderer.isCullingEnabled())
            {
                const float pixelsPerRadian = (CtrGpu::sTopHeight * 0.5f)
                    / std::tan(osg::DegreesToRadians(scene.mFovY) * 0.5f);
                minRadiusPerDistance = Settings::camera().mSmallFeatureCullingPixelSize * 0.5f / pixelsPerRadian;
            }

            mObjects->forEach([&](const Animation& animation) {
                if (!animation.isRenderable())
                    return;
                const osg::BoundingSpheref bound = animation.getWorldBound();
                const float centerDistance = (bound.center() - cameraPos).length();
                if (bound.valid() && bound.radius() < centerDistance * minRadiusPerDistance)
                    return;
                if (mDebugHideCameraInside && bound.valid() && bound.radius() < 1000.f
                    && centerDistance < bound.radius())
                    return;
                if (!renderer.isVisible(bound))
                    return;
                CtrScene::DrawParams params;
                pickLights(bound, params);
                animation.render(renderer, params);
            });

            if (mPlayerAnimation && mPlayerAnimation->isRenderable())
            {
                CtrScene::DrawParams params;
                pickLights(mPlayerAnimation->getWorldBound(), params);
                const bool firstPerson = mCamera->getMode() == Camera::Mode::FirstPerson;
                if (firstPerson)
                    renderer.setFirstPersonDepth(true);
                mPlayerAnimation->render(renderer, params);
                if (firstPerson)
                    renderer.setFirstPersonDepth(false);
            }

            if (mWaterEnabled && !mDebugHideWater)
                mWater->draw(renderer, osg::Vec3f(mCamera->getPosition()), mWaterHeight, time);

            for (const auto& weak : mProjectiles)
            {
                const auto visual = weak.lock();
                if (!visual)
                    continue;
                osg::Matrixf world = osg::Matrixf::rotate(visual->mAttitude)
                    * osg::Matrixf::translate(visual->mPosition);
                if (visual->mRotate)
                    world = osg::Matrixf::rotate(visual->mTime * osg::PIf * 2.f, osg::Vec3f(0.f, -1.f, 0.f)) * world;
                const osg::BoundingSpheref& modelBound = visual->mModel->mMeshes.empty()
                    ? osg::BoundingSpheref()
                    : visual->mModel->mMeshes.front().mBound;
                CtrScene::DrawParams params;
                params.mFullAmbient = visual->mHasLight;
                params.mTextureOverride = visual->mTexture.get();
                if (!visual->mHasLight)
                    pickLights(osg::BoundingSpheref(visual->mPosition, std::max(modelBound.radius(), 16.f)), params);
                for (const CtrScene::Mesh& mesh : visual->mModel->mMeshes)
                    if (!mesh.isSkinned() && mesh.mNode >= 0)
                        renderer.drawMesh(mesh, visual->mBindPose[mesh.mNode] * world, params);
            }

            for (const auto& effect : mEffects)
            {
                CtrScene::DrawParams params;
                params.mFullAmbient = effect->mFullAmbient;
                for (const CtrScene::Mesh& mesh : effect->mModel->mMeshes)
                    if (!mesh.isSkinned() && mesh.mNode >= 0)
                        renderer.drawMesh(mesh, effect->mBindPose[mesh.mNode] * effect->mWorld, params);
                CtrScene::renderParticles(renderer, *effect->mModel, effect->mParticles, effect->mParticleTime,
                    effect->mTime, effect->mNodeWorld, params);
            }
        }

        renderer.endScene();

        if (LocalMap* localMap = LocalMap::getInstance())
            localMap->renderPending(*this);
    }

    Animation* RenderingManager::getAnimation(const MWWorld::Ptr& ptr)
    {
        if (mPlayerAnimation.get() && ptr == mPlayerAnimation->getPtr())
            return mPlayerAnimation.get();

        return mObjects->getAnimation(ptr);
    }

    const Animation* RenderingManager::getAnimation(const MWWorld::ConstPtr& ptr) const
    {
        if (mPlayerAnimation.get() && ptr == mPlayerAnimation->getPtr())
            return mPlayerAnimation.get();

        return mObjects->getAnimation(ptr);
    }

    void RenderingManager::updatePlayerPtr(const MWWorld::Ptr& ptr)
    {
        if (mPlayerAnimation.get())
        {
            setupPlayer(ptr);
            mPlayerAnimation->updatePtr(ptr);
        }
        mCamera->attachTo(ptr);
    }

    void RenderingManager::removePlayer(const MWWorld::Ptr& player) {}

    void RenderingManager::setupPlayer(const MWWorld::Ptr& player)
    {
        if (!player.getRefData().getBaseNode())
        {
            osg::ref_ptr<SceneUtil::PositionAttitudeTransform> node = new SceneUtil::PositionAttitudeTransform;
            node->setNodeMask(Mask_Player);
            player.getRefData().setBaseNode(std::move(node));
        }
    }

    void RenderingManager::renderPlayer(const MWWorld::Ptr& player)
    {
        mPlayerAnimation = new NpcAnimation(player, player.getRefData().getBaseNode(), mResourceSystem, false,
            NpcAnimation::VM_Normal, mFirstPersonFieldOfView);

        mCamera->setAnimation(mPlayerAnimation.get());
        mCamera->attachTo(player);
    }

    void RenderingManager::rebuildPtr(const MWWorld::Ptr& ptr)
    {
        NpcAnimation* anim = nullptr;
        if (mPlayerAnimation && ptr == mPlayerAnimation->getPtr())
            anim = mPlayerAnimation.get();
        else
            anim = dynamic_cast<NpcAnimation*>(mObjects->getAnimation(ptr));
        if (anim)
        {
            anim->rebuild();
            if (mCamera->getTrackingPtr() == ptr)
            {
                mCamera->attachTo(ptr);
                mCamera->setAnimation(anim);
            }
        }
    }

    void RenderingManager::processChangedSettings(const Settings::CategorySettingVector& changed)
    {
        for (const auto& [category, setting] : changed)
        {
            if (category == "Camera" && setting == "field of view")
                mFieldOfView = Settings::camera().mFieldOfView;
            else if (category == "Camera" && setting == "viewing distance")
                setViewDistance(Settings::camera().mViewingDistance);
        }
    }

    void RenderingManager::setViewDistance(float distance, bool /*delay*/)
    {
        mViewDistance = distance;
        fog().rescaleToViewDistance(distance);
    }

    float RenderingManager::getTerrainHeightAt(const osg::Vec3f& pos, ESM::RefId worldspace)
    {
        const float cellSize = static_cast<float>(ESM::getCellSize(worldspace));
        const int cellX = static_cast<int>(std::floor(pos.x() / cellSize));
        const int cellY = static_cast<int>(std::floor(pos.y() / cellSize));
        std::shared_ptr<const ESMTerrain::LandObject> land
            = mLandManager->getLand(ESM::ExteriorCellLocation(cellX, cellY, worldspace));
        constexpr float defaultHeight = -2048.f;
        if (!land)
            return defaultHeight;
        const ESM::LandData* data = land->getData(ESM::Land::DATA_VHGT);
        if (data == nullptr)
            return defaultHeight;
        const int landSize = data->getLandSize();
        const auto heights = data->getHeights();

        const float nX = (pos.x() - cellX * cellSize) / cellSize * (landSize - 1);
        const float nY = (pos.y() - cellY * cellSize) / cellSize * (landSize - 1);
        const int x0 = std::clamp(static_cast<int>(nX), 0, landSize - 1);
        const int y0 = std::clamp(static_cast<int>(nY), 0, landSize - 1);
        const int x1 = std::min(x0 + 1, landSize - 1);
        const int y1 = std::min(y0 + 1, landSize - 1);
        const float fx = nX - x0;
        const float fy = nY - y0;
        const float h00 = heights[y0 * landSize + x0];
        const float h10 = heights[y0 * landSize + x1];
        const float h01 = heights[y1 * landSize + x0];
        const float h11 = heights[y1 * landSize + x1];
        if ((1.f - fy) > fx)
            return h00 + (h10 - h00) * fx + (h01 - h00) * fy;
        return h11 + (h01 - h11) * (1.f - fx) + (h10 - h11) * (1.f - fy);
    }

    void RenderingManager::overrideFieldOfView(float val)
    {
        mFieldOfViewOverridden = true;
        mFieldOfViewOverride = val;
    }

    void RenderingManager::setFieldOfView(float val)
    {
        mFieldOfView = val;
    }

    float RenderingManager::getFieldOfView() const
    {
        return mFieldOfViewOverridden ? mFieldOfViewOverride : mFieldOfView;
    }

    void RenderingManager::resetFieldOfView()
    {
        mFieldOfViewOverridden = false;
    }

    osg::Vec3f RenderingManager::getHalfExtents(const MWWorld::ConstPtr& object) const
    {
        osg::Vec3f halfExtents(0, 0, 0);
        VFS::Path::Normalized modelName(object.getClass().getCorrectedModel(object));
        if (modelName.empty())
            return halfExtents;

        const CtrScene::ModelPtr model = mContext->getModels().get(modelName);
        const osg::BoundingBoxf& bounds = model->mBounds;
        if (bounds.valid())
        {
            halfExtents[0] = std::abs(bounds.xMax() - bounds.xMin()) / 2.f;
            halfExtents[1] = std::abs(bounds.yMax() - bounds.yMin()) / 2.f;
            halfExtents[2] = std::abs(bounds.zMax() - bounds.zMin()) / 2.f;
        }
        return halfExtents;
    }

    osg::BoundingBox RenderingManager::getCullSafeBoundingBox(const MWWorld::Ptr& ptr) const
    {
        if (ptr.isEmpty())
            return {};
        const VFS::Path::Normalized modelName(ptr.getClass().getCorrectedModel(ptr));
        if (modelName.empty())
            return {};
        const CtrScene::ModelPtr model = mContext->getModels().get(modelName);
        if (!model->mBounds.valid())
            return {};

        osg::Matrixf world;
        if (const SceneUtil::PositionAttitudeTransform* node = ptr.getRefData().getBaseNode())
            world = objectMatrix(*node);
        else
            world.makeTranslate(ptr.getCellRef().getPosition().asVec3());

        osg::BoundingBox result;
        for (unsigned int i = 0; i < 8; ++i)
            result.expandBy(model->mBounds.corner(i) * world);
        return result;
    }

    osg::Vec2f RenderingManager::getScreenCoords(const osg::BoundingBox& bb)
    {
        if (bb.valid())
        {
            const osg::Vec3f worldPoint((bb.xMin() + bb.xMax()) * 0.5f, (bb.yMin() + bb.yMax()) * 0.5f, bb.zMax());
            float x, y;
            if (mContext->getRenderer().project(worldPoint, x, y) && x >= 0.f && x <= 1.f && y >= 0.f && y <= 1.f)
                return osg::Vec2f(x, y);
        }
        return osg::Vec2f(0.5f, 0.f);
    }

    RenderingManager::RayResult RenderingManager::castRay(const osg::Vec3f& origin, const osg::Vec3f& dest,
        bool ignorePlayer, bool ignoreActors, bool ignoreTerrain, std::span<const MWWorld::Ptr> ignoreList)
    {
        RayResult result;
        result.mHit = false;
        result.mRatio = 0.f;

        const float length = (dest - origin).length();
        if (length <= 0.f)
            return result;

        int mask = MWPhysics::CollisionType_World | MWPhysics::CollisionType_Door;
        if (!ignoreActors)
            mask |= MWPhysics::CollisionType_Actor;
        if (!ignoreTerrain)
            mask |= MWPhysics::CollisionType_HeightMap;

        std::vector<MWWorld::ConstPtr> ignore(ignoreList.begin(), ignoreList.end());
        const MWWorld::Ptr player = MWMechanics::getPlayer();
        if (ignorePlayer && !player.isEmpty())
            ignore.push_back(player);

        const MWPhysics::RayCastingResult physics
            = MWBase::Environment::get().getWorld()->getRayCasting()->castRay(origin, dest, ignore, {}, mask);
        if (physics.mHit)
        {
            result.mHit = true;
            result.mHitPointWorld = physics.mHitPos;
            result.mHitNormalWorld = physics.mHitNormal;
            result.mHitObject = physics.mHitObject;
            result.mRatio = (physics.mHitPos - origin).length() / length;
        }

        float bestRatio = result.mHit ? result.mRatio : 1.f;
        MWWorld::Ptr bestItem;
        mObjects->forEach([&](Animation& animation) {
            const MWWorld::Ptr ptr = animation.getPtr();
            if (!isPickedByBounds(ptr) || !animation.isRenderable())
                return;
            for (const MWWorld::Ptr& ignored : ignoreList)
                if (ignored == ptr)
                    return;
            const SceneUtil::PositionAttitudeTransform* node = ptr.getRefData().getBaseNode();
            if (node == nullptr)
                return;
            const osg::BoundingSpheref sphere = animation.getWorldBound();
            if (!sphere.valid())
                return;
            const osg::Vec3f toCenter = sphere.center() - origin;
            const osg::Vec3f dir = (dest - origin) / length;
            const float along = toCenter * dir;
            if (along < -sphere.radius() || along > length * bestRatio + sphere.radius())
                return;
            if ((toCenter - dir * along).length2() > sphere.radius() * sphere.radius())
                return;
            const VFS::Path::Normalized modelName(ptr.getClass().getCorrectedModel(ptr));
            if (modelName.empty())
                return;
            const CtrScene::ModelPtr model = mContext->getModels().get(modelName);
            if (!model->mBounds.valid())
                return;
            osg::Matrixf inverse;
            inverse.invert(objectMatrix(*node));
            float ratio;
            if (intersectBox(origin * inverse, dest * inverse, model->mBounds, ratio) && ratio < bestRatio)
            {
                bestRatio = ratio;
                bestItem = ptr;
            }
        });

        if (!bestItem.isEmpty())
        {
            result.mHit = true;
            result.mRatio = bestRatio;
            result.mHitPointWorld = origin + (dest - origin) * bestRatio;
            result.mHitNormalWorld = origin - dest;
            result.mHitNormalWorld.normalize();
            result.mHitObject = bestItem;
        }
        return result;
    }

    RenderingManager::RayResult RenderingManager::castCameraToViewportRay(
        const float nX, const float nY, float maxDistance, bool ignorePlayer, bool ignoreActors, bool ignoreTerrain)
    {
        const float fovy = osg::DegreesToRadians(getFieldOfView());
        const float ty = std::tan(fovy * 0.5f);
        const float tx = ty * (400.f / 240.f);
        const osg::Vec3f viewDir((nX * 2.f - 1.f) * tx, (1.f - nY * 2.f) * ty, -1.f);
        osg::Matrixf inverseView;
        inverseView.invert(mCamera->getViewMatrix());
        const osg::Vec3f origin = osg::Vec3f(0.f, 0.f, 0.f) * inverseView;
        const osg::Vec3f end = (viewDir * maxDistance) * inverseView;
        return castRay(origin, end, ignorePlayer, ignoreActors, ignoreTerrain);
    }

    LandManager* RenderingManager::getLandManager() const
    {
        return mLandManager.get();
    }
}
