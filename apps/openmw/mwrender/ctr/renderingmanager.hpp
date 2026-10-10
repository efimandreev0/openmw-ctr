#ifndef OPENMW_MWRENDER_CTR_RENDERINGMANAGER_H
#define OPENMW_MWRENDER_CTR_RENDERINGMANAGER_H


#include "objects.hpp"

#include "../renderinginterface.hpp"
#include "../rendermode.hpp"

#include <components/ctr/scene/renderer.hpp>
#include <components/settings/settings.hpp>
#include <components/vfs/pathutil.hpp>

#include <osg/BoundingBox>
#include <osg/BoundingSphere>
#include <osg/Quat>
#include <osg/Vec2f>
#include <osg/Vec3f>
#include <osg/Vec4f>
#include <osg/Vec4i>
#include <osg/ref_ptr>

#include <deque>
#include <filesystem>
#include <memory>
#include <span>
#include <vector>

namespace Resource
{
    class ResourceSystem;
}

namespace ESM
{
    struct FormId;
    using RefNum = FormId;
}

namespace SceneUtil
{
    class WorkQueue;
    class UnrefQueue;
}

namespace DetourNavigator
{
    struct AgentBounds;
}

namespace MWWorld
{
    class GroundcoverStore;
    class Cell;
    class CellStore;
}

namespace Terrain
{
    class World;
}

namespace MWRender
{
    class CtrContext;
    class SkyManager;
    class NpcAnimation;
    class Camera;
    class LandManager;
    class PostProcessor;
    class CtrTerrain;
    class CtrWater;
    class CtrSkyRenderer;

    class ProjectileVisual
    {
    public:
        const osg::Vec3f& getPosition() const { return mPosition; }
        void setPosition(const osg::Vec3f& position) { mPosition = position; }
        const osg::Quat& getAttitude() const { return mAttitude; }
        void setAttitude(const osg::Quat& attitude) { mAttitude = attitude; }
        void advance(float duration) { mTime += duration; }

    private:
        friend class RenderingManager;

        CtrScene::ModelPtr mModel;
        std::vector<osg::Matrixf> mBindPose;
        CtrGpu::TexturePtr mTexture;
        osg::Vec3f mPosition;
        osg::Quat mAttitude;
        float mTime = 0.f;
        bool mRotate = false;
        bool mHasLight = false;
        CtrScene::PointLight mLight;
    };

    class RenderingManager : public MWRender::RenderingInterface
    {
    public:
        RenderingManager(Resource::ResourceSystem* resourceSystem, const std::shared_ptr<SceneUtil::WorkQueue>& workQueue);
        ~RenderingManager();

        static RenderingManager* getInstance() { return sInstance; }

        bool renderMapView(CtrGpu::Texture& texture, const osg::Vec2f& center, const osg::Vec2f& up, float halfSize,
            float zMin, float zMax);
        std::uint32_t renderCharacterPreview(
            CtrGpu::Texture& texture, const Animation* animation, const osg::Matrixf& view, float fovY);
        osg::BoundingBox getStaticSceneBounds() const;

        MWRender::Objects& getObjects() override;

        Resource::ResourceSystem* getResourceSystem();

        void flushUnrefQueueImmediate() {}

        const std::shared_ptr<SceneUtil::WorkQueue>& getWorkQueue() const;

        Terrain::World* getTerrain() { return nullptr; }

        void preloadCommonAssets() {}

        double getReferenceTime() const;

        void setNightEyeFactor(float factor);

        void setAmbientColour(const osg::Vec4f& colour);

        int skyGetMasserPhase() const;
        int skyGetSecundaPhase() const;
        void skySetMoonColour(bool red);

        const osg::Vec4f& getSunLightPosition() const { return mSunPosition; }
        void setSunDirection(const osg::Vec3f& direction);
        void setSunColour(const osg::Vec4f& diffuse, const osg::Vec4f& specular, float sunVis);
        void setNight(bool isNight) { mNight = isNight; }

        void configureAmbient(const MWWorld::Cell& cell);
        void configureFog(const MWWorld::Cell& cell);
        void configureFog(
            float fogDepth, float underwaterFog, float dlFactor, float dlOffset, const osg::Vec4f& colour);

        void addCell(const MWWorld::CellStore* store);
        void removeCell(const MWWorld::CellStore* store);

        void enableTerrain(bool enable, ESM::RefId worldspace);

        void updatePtr(const MWWorld::Ptr& old, const MWWorld::Ptr& updated);

        void rotateObject(const MWWorld::Ptr& ptr, const osg::Quat& rot);
        void moveObject(const MWWorld::Ptr& ptr, const osg::Vec3f& pos);
        void scaleObject(const MWWorld::Ptr& ptr, const osg::Vec3f& scale);

        void removeObject(const MWWorld::Ptr& ptr);

        void setWaterEnabled(bool enabled);
        void setWaterHeight(float level);
        void setWaterCell(const MWWorld::CellStore* store) {}

        std::vector<char> screenshot(int w, int h) { return {}; }

        struct RayResult
        {
            bool mHit;
            osg::Vec3f mHitNormalWorld;
            osg::Vec3f mHitPointWorld;
            MWWorld::Ptr mHitObject;
            ESM::RefNum mHitRefnum;
            float mRatio;
        };

        RayResult castRay(const osg::Vec3f& origin, const osg::Vec3f& dest, bool ignorePlayer,
            bool ignoreActors = false, bool ignoreTerrain = false, std::span<const MWWorld::Ptr> ignoreList = {});

        RayResult castCameraToViewportRay(const float nX, const float nY, float maxDistance, bool ignorePlayer,
            bool ignoreActors = false, bool ignoreTerrain = false);

        osg::Vec2f getScreenCoords(const osg::BoundingBox& bb);

        void setSkyEnabled(bool enabled);

        bool toggleRenderMode(RenderMode mode);

        SkyManager* getSkyManager();

        void spawnEffect(VFS::Path::NormalizedView model, std::string_view texture, const osg::Vec3f& worldPosition,
            float scale = 1.f, bool isMagicVFX = true, bool useAmbientLight = true, std::string_view effectId = {},
            bool loop = false);
        void removeEffect(std::string_view effectId);

        std::shared_ptr<ProjectileVisual> createProjectileVisual(VFS::Path::NormalizedView model,
            VFS::Path::NormalizedView texture, const osg::Vec3f& pos, const osg::Quat& orient, bool rotate,
            const osg::Vec4f* lightDiffuse);

        void clear();

        void notifyWorldSpaceChanged();

        void update(float dt, bool paused);

        void draw();
        void handleDebugInput(CtrScene::Renderer& renderer, const CtrScene::SceneParams& scene);

        Animation* getAnimation(const MWWorld::Ptr& ptr);
        const Animation* getAnimation(const MWWorld::ConstPtr& ptr) const;

        PostProcessor* getPostProcessor() { return nullptr; }

        void addWaterRippleEmitter(const MWWorld::Ptr& ptr) {}
        void removeWaterRippleEmitter(const MWWorld::Ptr& ptr) {}
        void emitWaterRipple(const osg::Vec3f& pos) {}

        void updatePlayerPtr(const MWWorld::Ptr& ptr);

        void removePlayer(const MWWorld::Ptr& player);
        void setupPlayer(const MWWorld::Ptr& player);
        void renderPlayer(const MWWorld::Ptr& player);

        void rebuildPtr(const MWWorld::Ptr& ptr);

        void processChangedSettings(const Settings::CategorySettingVector& settings);

        float getNearClipDistance() const { return mNearClip; }
        float getViewDistance() const { return mViewDistance; }
        void setViewDistance(float distance, bool delay = false);

        float getTerrainHeightAt(const osg::Vec3f& pos, ESM::RefId worldspace);

        Camera* getCamera() { return mCamera.get(); }

        void overrideFieldOfView(float val);
        void setFieldOfView(float val);
        float getFieldOfView() const;
        void resetFieldOfView();

        osg::Vec3f getHalfExtents(const MWWorld::ConstPtr& object) const;
        osg::BoundingBox getCullSafeBoundingBox(const MWWorld::Ptr& ptr) const;

        void exportSceneGraph(
            const MWWorld::Ptr& ptr, const std::filesystem::path& filename, const std::string& format)
        {
        }

        LandManager* getLandManager() const;

        bool toggleBorders() { return false; }

        void updateActorPath(const MWWorld::ConstPtr& actor, const std::deque<osg::Vec3f>& path,
            const DetourNavigator::AgentBounds& agentBounds, const osg::Vec3f& start, const osg::Vec3f& end) const
        {
        }

        void removeActorPath(const MWWorld::ConstPtr& actor) const {}

        void setNavMeshNumber(const std::size_t value) {}

        void setActiveGrid(const osg::Vec4i& grid) {}

        bool pagingEnableObject(int type, const MWWorld::ConstPtr& ptr, bool enabled) { return false; }
        void pagingBlacklistObject(int type, const MWWorld::ConstPtr& ptr) {}
        bool pagingUnlockCache() { return false; }
        void getPagedRefnums(const osg::Vec4i& activeGrid, std::vector<ESM::RefNum>& out) {}

        void updateProjectionMatrix() {}

        void setScreenRes(int width, int height) {}

        void setProjectionOffset(const osg::Vec2f& offset) { mProjectionOffset = offset; }
        osg::Vec2f getProjectionOffset() const { return mProjectionOffset; }

    private:
        struct Effect;

        static RenderingManager* sInstance;

        void updateAmbient();
        void pickLights(const osg::BoundingSpheref& bound, CtrScene::DrawParams& params) const;

        Resource::ResourceSystem* mResourceSystem;
        std::shared_ptr<SceneUtil::WorkQueue> mWorkQueue;
        std::unique_ptr<CtrContext> mContext;
        bool mDebugHideWater = false;
        bool mDebugHideCameraInside = false;

        std::unique_ptr<Objects> mObjects;
        std::unique_ptr<LandManager> mLandManager;
        std::unique_ptr<CtrTerrain> mTerrain;
        std::unique_ptr<CtrWater> mWater;
        std::unique_ptr<CtrSkyRenderer> mSkyRenderer;
        std::unique_ptr<SkyManager> mSky;
        std::unique_ptr<Camera> mCamera;
        osg::ref_ptr<NpcAnimation> mPlayerAnimation;
        std::vector<std::unique_ptr<Effect>> mEffects;
        std::vector<std::weak_ptr<ProjectileVisual>> mProjectiles;

        std::vector<const CtrScene::PointLight*> mFrameLights;

        osg::Vec4f mAmbientColor;
        osg::Vec4f mSunColor{ 1.f, 1.f, 1.f, 1.f };
        osg::Vec4f mSunPosition{ 0.f, 0.f, 1.f, 0.f };
        osg::Vec4f mFogColor{ 0.5f, 0.5f, 0.5f, 1.f };
        osg::Vec4f mClearColor{ 0.5f, 0.5f, 0.5f, 1.f };
        float mFogStart = 2048.f;
        float mFogEnd = 7168.f;
        float mNightEyeFactor;
        float mNearClip;
        float mViewDistance;
        bool mFieldOfViewOverridden;
        float mFieldOfViewOverride;
        float mFieldOfView;
        float mFirstPersonFieldOfView;
        bool mNight = false;
        bool mSkyEnabled = false;
        bool mInterior = false;
        bool mWaterEnabled = false;
        float mWaterHeight = 0.f;
        bool mRenderScene = true;
        double mReferenceTime = 0.0;
        osg::Vec2f mProjectionOffset;

        void operator=(const RenderingManager&);
        RenderingManager(const RenderingManager&);
    };
}

#endif
