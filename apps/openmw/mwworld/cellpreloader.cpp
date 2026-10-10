#include "cellpreloader.hpp"

#include <algorithm>
#include <atomic>
#include <limits>
#include <set>
#include <span>

#include <osg/Stats>

#include <components/debug/debuglog.hpp>
#include <components/esm/util.hpp>
#include <components/esm3/loadcell.hpp>
#include <components/loadinglistener/reporter.hpp>
#include <components/misc/constants.hpp>
#include <components/misc/pathhelpers.hpp>
#include <components/misc/resourcehelpers.hpp>
#include <components/misc/strings/algorithm.hpp>
#include <components/misc/strings/lower.hpp>
#include <components/resource/bulletshapemanager.hpp>
#include <components/resource/resourcesystem.hpp>
#ifdef OPENMW_CTR_NATIVE
#include <components/resource/niffilemanager.hpp>
#else
#include <components/resource/keyframemanager.hpp>
#include <components/resource/scenemanager.hpp>
#include <components/terrain/view.hpp>
#include <components/terrain/world.hpp>
#endif
#include <components/vfs/manager.hpp>

#include "../mwrender/landmanager.hpp"

#include "cellstore.hpp"
#include "class.hpp"

namespace MWWorld
{
    namespace
    {
        bool contains(std::span<const PositionCellGrid> positions, const PositionCellGrid& contained, float tolerance)
        {
            const float squaredTolerance = tolerance * tolerance;
            const auto predicate = [&](const PositionCellGrid& v) {
                return (contained.mPosition - v.mPosition).length2() < squaredTolerance
                    && contained.mCellBounds == v.mCellBounds;
            };
            return std::ranges::any_of(positions, predicate);
        }

        bool contains(
            std::span<const PositionCellGrid> container, std::span<const PositionCellGrid> contained, float tolerance)
        {
            const auto predicate = [&](const PositionCellGrid& v) { return contains(container, v, tolerance); };
            return std::ranges::all_of(contained, predicate);
        }
    }

    struct ListModelsVisitor
    {
        bool operator()(const MWWorld::ConstPtr& ptr)
        {
            ptr.getClass().getModelsToPreload(ptr, mOut);

            return true;
        }

        std::vector<VFS::Path::NormalizedView>& mOut;
    };

    /// Worker thread item: preload models in a cell.
    class PreloadItem : public SceneUtil::WorkItem
    {
    public:
        /// Constructor to be called from the main thread.
        explicit PreloadItem(MWWorld::CellStore* cell, Resource::SceneManager* sceneManager,
            Resource::BulletShapeManager* bulletShapeManager, Resource::KeyframeManager* keyframeManager,
            Terrain::World* terrain, MWRender::LandManager* landManager, bool preloadInstances)
            : mIsExterior(cell->getCell()->isExterior())
            , mCellLocation(cell->getCell()->getExteriorCellLocation())
            , mCellId(cell->getCell()->getId())
            , mSceneManager(sceneManager)
            , mBulletShapeManager(bulletShapeManager)
            , mKeyframeManager(keyframeManager)
            , mTerrain(terrain)
            , mLandManager(landManager)
            , mPreloadInstances(preloadInstances)
            , mAbort(false)
        {
#ifndef OPENMW_CTR_NATIVE
            mTerrainView = mTerrain->createView();
#endif

            ListModelsVisitor visitor{ mMeshes };
            cell->forEachConst(visitor);
        }

        void abort() override { mAbort = true; }

        /// Preload work to be called from the worker thread.
        void doWork() override
        {
            if (mIsExterior)
            {
                try
                {
#ifndef OPENMW_CTR_NATIVE
                    mTerrain->cacheCell(mTerrainView.get(), mCellLocation.mX, mCellLocation.mY);
#endif
                    mPreloadedOwnedObjects.insert(mLandManager->getLand(mCellLocation));
                }
                catch (const std::exception& e)
                {
                    Log(Debug::Warning) << "Failed to cache terrain for exterior cell " << mCellLocation << ": "
                                        << e.what();
                }
            }

            VFS::Path::Normalized mesh;
            VFS::Path::Normalized kfname;
            for (VFS::Path::NormalizedView path : mMeshes)
            {
                if (mAbort)
                    break;

                try
                {
#ifdef OPENMW_CTR_NATIVE
                    const VFS::Manager& vfs = *mVFS;
#else
                    const VFS::Manager& vfs = *mSceneManager->getVFS();
#endif
                    mesh = Misc::ResourceHelpers::correctMeshPath(path);
                    mesh = Misc::ResourceHelpers::correctActorModelPath(mesh, &vfs);

                    if (!vfs.exists(mesh))
                        continue;

                    constexpr VFS::Path::ExtensionView nif("nif");
                    if (Misc::getFileName(mesh).starts_with('x') && mesh.extension() == nif)
                    {
                        kfname = mesh;
                        constexpr VFS::Path::ExtensionView kf("kf");
                        kfname.changeExtension(kf);
#ifdef OPENMW_CTR_NATIVE
                        if (vfs.exists(kfname))
                            mPreloadedOwnedObjects.insert(mNifFileManager->get(kfname));
                    }

                    mPreloadedOwnedObjects.insert(mNifFileManager->get(mesh));
#else
                        if (vfs.exists(kfname))
                            mPreloadedOwnedObjects.insert(mKeyframeManager->get(kfname));
                    }

                    mPreloadedObjects.insert(mSceneManager->getTemplate(mesh));
#endif
                    if (mPreloadInstances)
                        mPreloadedOwnedObjects.insert(mBulletShapeManager->cacheInstance(mesh));
                    else
                        mPreloadedOwnedObjects.insert(mBulletShapeManager->getShape(mesh));
                }
                catch (const std::exception& e)
                {
                    Log(Debug::Warning) << "Failed to preload mesh \"" << path << "\" from cell " << mCellId << ": "
                                        << e.what();
                }
            }
        }

    private:
        bool mIsExterior;
        ESM::ExteriorCellLocation mCellLocation;
        ESM::RefId mCellId;
        std::vector<VFS::Path::NormalizedView> mMeshes;
        Resource::SceneManager* mSceneManager;
        Resource::BulletShapeManager* mBulletShapeManager;
        Resource::KeyframeManager* mKeyframeManager;
        Terrain::World* mTerrain;
        MWRender::LandManager* mLandManager;
        bool mPreloadInstances;

        std::atomic<bool> mAbort;

#ifdef OPENMW_CTR_NATIVE
    public:
        const VFS::Manager* mVFS = nullptr;
        Resource::NifFileManager* mNifFileManager = nullptr;

    private:
#else
        osg::ref_ptr<Terrain::View> mTerrainView;
#endif

        std::set<osg::ref_ptr<const osg::Object>> mPreloadedObjects;
        std::set<std::shared_ptr<const void>> mPreloadedOwnedObjects;
    };

#ifdef OPENMW_CTR_NATIVE
    class TerrainPreloadItem : public SceneUtil::WorkItem
    {
    public:
        void wait(Loading::Listener&) const {}
    };
#else
    class TerrainPreloadItem : public SceneUtil::WorkItem
    {
    public:
        explicit TerrainPreloadItem(const std::vector<osg::ref_ptr<Terrain::View>>& views, Terrain::World* world,
            std::span<const PositionCellGrid> preloadPositions)
            : mAbort(false)
            , mTerrainViews(views)
            , mWorld(world)
            , mPreloadPositions(preloadPositions.begin(), preloadPositions.end())
        {
        }

        void doWork() override
        {
            for (unsigned int i = 0; i < mTerrainViews.size() && i < mPreloadPositions.size() && !mAbort; ++i)
            {
                mTerrainViews[i]->reset();
                mWorld->preload(mTerrainViews[i], mPreloadPositions[i].mPosition, mPreloadPositions[i].mCellBounds,
                    mAbort, mLoadingReporter);
            }
            mLoadingReporter.complete();
        }

        void abort() override { mAbort = true; }

        void wait(Loading::Listener& listener) const { mLoadingReporter.wait(listener); }

    private:
        std::atomic<bool> mAbort;
        std::vector<osg::ref_ptr<Terrain::View>> mTerrainViews;
        Terrain::World* mWorld;
        std::vector<PositionCellGrid> mPreloadPositions;
        Loading::Reporter mLoadingReporter;
    };
#endif

    /// Worker thread item: update the resource system's cache, effectively deleting unused entries.
    class UpdateCacheItem : public SceneUtil::WorkItem
    {
    public:
        UpdateCacheItem(Resource::ResourceSystem* resourceSystem, double referenceTime)
            : mReferenceTime(referenceTime)
            , mResourceSystem(resourceSystem)
        {
        }

        void doWork() override { mResourceSystem->updateCache(mReferenceTime); }

    private:
        double mReferenceTime;
        Resource::ResourceSystem* mResourceSystem;
    };

    CellPreloader::CellPreloader(Resource::ResourceSystem* resourceSystem,
        Resource::BulletShapeManager* bulletShapeManager, Terrain::World* terrain, MWRender::LandManager* landManager)
        : mResourceSystem(resourceSystem)
        , mBulletShapeManager(bulletShapeManager)
        , mTerrain(terrain)
        , mLandManager(landManager)
        , mExpiryDelay(0.0)
        , mPreloadInstances(true)
        , mLastResourceCacheUpdate(0.0)
        , mLoadedTerrainTimestamp(0.0)
    {
    }

    CellPreloader::~CellPreloader()
    {
        clearAllTasks();
    }

    void CellPreloader::preload(CellStore& cell, double timestamp, bool urgent)
    {
        if (!mWorkQueue)
        {
            Log(Debug::Error) << "Error: can't preload, no work queue set";
            return;
        }
        if (cell.getState() == CellStore::State_Unloaded)
        {
            Log(Debug::Error) << "Error: can't preload objects for unloaded cell";
            return;
        }

        PreloadMap::iterator found = mPreloadCells.find(&cell);
        if (found != mPreloadCells.end())
        {
            // already preloaded, nothing to do other than updating the timestamp
            found->second.mTimeStamp = timestamp;
            return;
        }

        while (mPreloadCells.size() >= mMaxCacheSize)
        {
#if defined(__vita__) || defined(__3DS__)
            if (urgent && mHasPlayerContext)
            {
                PreloadMap::iterator furthest = mPreloadCells.end();
                float furthestDistSq = -1.0f;
                for (PreloadMap::iterator it = mPreloadCells.begin(); it != mPreloadCells.end(); ++it)
                {
                    float d = getCellDistanceSq(it->first);
                    if (d > furthestDistSq)
                    {
                        furthestDistSq = d;
                        furthest = it;
                    }
                }
                if (furthest != mPreloadCells.end())
                {
                    furthest->second.mWorkItem->abort();
                    mPreloadCells.erase(furthest);
                    ++mEvicted;
                    continue;
                }
            }
#endif
            // throw out oldest cell to make room
            PreloadMap::iterator oldestCell = mPreloadCells.begin();
            double oldestTimestamp = std::numeric_limits<double>::max();
            double threshold = 1.0; // seconds
            for (PreloadMap::iterator it = mPreloadCells.begin(); it != mPreloadCells.end(); ++it)
            {
                if (it->second.mTimeStamp < oldestTimestamp)
                {
                    oldestTimestamp = it->second.mTimeStamp;
                    oldestCell = it;
                }
            }

            if (oldestTimestamp + threshold < timestamp)
            {
                oldestCell->second.mWorkItem->abort();
                mPreloadCells.erase(oldestCell);
                ++mEvicted;
            }
            else
                return;
        }

#ifdef OPENMW_CTR_NATIVE
        auto item = std::make_shared<PreloadItem>(
            &cell, nullptr, mBulletShapeManager, nullptr, mTerrain, mLandManager, mPreloadInstances);
        item->mVFS = mResourceSystem->getVFS();
        item->mNifFileManager = mResourceSystem->getNifFileManager();
#else
        auto item = std::make_shared<PreloadItem>(&cell, mResourceSystem->getSceneManager(), mBulletShapeManager,
            mResourceSystem->getKeyframeManager(), mTerrain, mLandManager, mPreloadInstances);
#endif
        mWorkQueue->addWorkItem(item);

        mPreloadCells.emplace(&cell, PreloadEntry(timestamp, item));
        ++mAdded;
    }

    void CellPreloader::notifyLoaded(CellStore* cell)
    {
        PreloadMap::iterator found = mPreloadCells.find(cell);
        if (found != mPreloadCells.end())
        {
            if (found->second.mWorkItem)
            {
                found->second.mWorkItem->abort();
                found->second.mWorkItem = nullptr;
            }

            mPreloadCells.erase(found);
            ++mLoaded;
        }
    }

    void CellPreloader::clear()
    {
        for (PreloadMap::iterator it = mPreloadCells.begin(); it != mPreloadCells.end();)
        {
            if (it->second.mWorkItem)
            {
                it->second.mWorkItem->abort();
                it->second.mWorkItem = nullptr;
            }

            mPreloadCells.erase(it++);
        }
    }

    void CellPreloader::updateCache(double timestamp)
    {
        for (PreloadMap::iterator it = mPreloadCells.begin(); it != mPreloadCells.end();)
        {
#if defined(__vita__) || defined(__3DS__)
            double expiryDelay = mExpiryDelay;
            if (isRearHemisphere(it->first))
                expiryDelay *= 0.4;
#else
            const double expiryDelay = mExpiryDelay;
#endif
            if (mPreloadCells.size() >= mMinCacheSize && it->second.mTimeStamp < timestamp - expiryDelay)
            {
                if (it->second.mWorkItem)
                {
                    it->second.mWorkItem->abort();
                    it->second.mWorkItem = nullptr;
                }
                mPreloadCells.erase(it++);
                ++mExpired;
            }
            else
                ++it;
        }

        if (timestamp - mLastResourceCacheUpdate > 1.0 && (!mUpdateCacheItem || mUpdateCacheItem->isDone()))
        {
            // the resource cache is cleared from the worker thread so that we're not holding up the main thread with
            // delete operations
            mUpdateCacheItem = std::make_shared<UpdateCacheItem>(mResourceSystem, timestamp);
            mWorkQueue->addWorkItem(mUpdateCacheItem, true);
            mLastResourceCacheUpdate = timestamp;
        }

        if (mTerrainPreloadItem && mTerrainPreloadItem->isDone())
        {
            mLoadedTerrainPositions = mTerrainPreloadPositions;
            mLoadedTerrainTimestamp = timestamp;
        }
    }

    void CellPreloader::setExpiryDelay(double expiryDelay)
    {
        mExpiryDelay = expiryDelay;
    }

    void CellPreloader::setPreloadInstances(bool preload)
    {
        mPreloadInstances = preload;
    }

    void CellPreloader::setWorkQueue(std::shared_ptr<SceneUtil::WorkQueue> workQueue)
    {
        mWorkQueue = workQueue;
    }

    void CellPreloader::syncTerrainLoad(Loading::Listener& listener)
    {
        if (mTerrainPreloadItem != nullptr && !mTerrainPreloadItem->isDone())
            mTerrainPreloadItem->wait(listener);
    }

    void CellPreloader::abortTerrainPreloadExcept(const PositionCellGrid* exceptPos)
    {
        if (exceptPos != nullptr && contains(mTerrainPreloadPositions, *exceptPos, Constants::CellSizeInUnits))
            return;
        if (mTerrainPreloadItem && !mTerrainPreloadItem->isDone())
        {
            mTerrainPreloadItem->abort();
            mTerrainPreloadItem->waitTillDone();
        }
        setTerrainPreloadPositions({});
    }

    void CellPreloader::setTerrainPreloadPositions(std::span<const PositionCellGrid> positions)
    {
#ifdef OPENMW_CTR_NATIVE
#else
        if (positions.empty())
        {
            mTerrainPreloadPositions.clear();
            mLoadedTerrainPositions.clear();
        }
        else if (contains(mTerrainPreloadPositions, positions, terrainPreloadMergeDistance))
            return;
        if (mTerrainPreloadItem && !mTerrainPreloadItem->isDone())
        {
            const bool sameGrids = std::ranges::equal(mTerrainPreloadPositions, positions,
                [](const PositionCellGrid& l, const PositionCellGrid& r) { return l.mCellBounds == r.mCellBounds; });
            if (!sameGrids)
            {
                mTerrainPreloadItem->abort();
                mTerrainPreloadPositions.clear();
            }
            return;
        }
        else
        {
            if (mTerrainViews.size() > positions.size())
                mTerrainViews.resize(positions.size());
            else if (mTerrainViews.size() < positions.size())
            {
                for (size_t i = mTerrainViews.size(); i < positions.size(); ++i)
                    mTerrainViews.emplace_back(mTerrain->createView());
            }

            mTerrainPreloadPositions.assign(positions.begin(), positions.end());
            if (!positions.empty())
            {
                mTerrainPreloadItem = std::make_shared<TerrainPreloadItem>(mTerrainViews, mTerrain, positions);
                mWorkQueue->addWorkItem(mTerrainPreloadItem);
            }
        }
#endif
    }

    bool CellPreloader::isTerrainLoaded(const PositionCellGrid& position, double referenceTime) const
    {
#ifdef OPENMW_CTR_NATIVE
        return true;
#else
        return mLoadedTerrainTimestamp + mResourceSystem->getSceneManager()->getExpiryDelay() > referenceTime
            && contains(mLoadedTerrainPositions, position, Constants::CellSizeInUnits);
#endif
    }

    void CellPreloader::setTerrain(Terrain::World* terrain)
    {
        if (terrain != mTerrain)
        {
            clearAllTasks();
            mTerrain = terrain;
        }
    }

    void CellPreloader::clearAllTasks()
    {
        if (mTerrainPreloadItem)
        {
            mTerrainPreloadItem->abort();
            mTerrainPreloadItem->waitTillDone();
            mTerrainPreloadItem = nullptr;
        }

        if (mUpdateCacheItem)
        {
            mUpdateCacheItem->waitTillDone();
            mUpdateCacheItem = nullptr;
        }

        for (PreloadMap::iterator it = mPreloadCells.begin(); it != mPreloadCells.end(); ++it)
            it->second.mWorkItem->abort();

        for (PreloadMap::iterator it = mPreloadCells.begin(); it != mPreloadCells.end(); ++it)
            it->second.mWorkItem->waitTillDone();

        mPreloadCells.clear();
    }

    void CellPreloader::reportStats(unsigned int frameNumber, osg::Stats& stats) const
    {
        stats.setAttribute(frameNumber, "CellPreloader Count", static_cast<double>(mPreloadCells.size()));
        stats.setAttribute(frameNumber, "CellPreloader Added", static_cast<double>(mAdded));
        stats.setAttribute(frameNumber, "CellPreloader Evicted", static_cast<double>(mEvicted));
        stats.setAttribute(frameNumber, "CellPreloader Loaded", static_cast<double>(mLoaded));
        stats.setAttribute(frameNumber, "CellPreloader Expired", static_cast<double>(mExpired));
    }

#if defined(__vita__) || defined(__3DS__)
    void CellPreloader::setPlayerContext(const osg::Vec3f& pos, const osg::Vec3f& forwardDir)
    {
        mPlayerPos = pos;
        mForwardDir = forwardDir;
        mHasPlayerContext = true;
    }

    float CellPreloader::getCellDistanceSq(const MWWorld::CellStore* cell) const
    {
        if (!cell || !cell->isExterior())
            return 0.0f;
        const osg::Vec2f c = ESM::indexToPosition(cell->getCell()->getExteriorCellLocation(), true);
        const float dx = c.x() - mPlayerPos.x();
        const float dy = c.y() - mPlayerPos.y();
        return dx * dx + dy * dy;
    }

    bool CellPreloader::isRearHemisphere(const MWWorld::CellStore* cell) const
    {
        if (!mHasPlayerContext || !cell || !cell->isExterior())
            return false;
        // Smoothed-dir magnitude doubles as a stability gate: low means the player is wandering.
        const float dirMag = mForwardDir.length();
        if (dirMag < 0.5f)
            return false;
        const osg::Vec2f c = ESM::indexToPosition(cell->getCell()->getExteriorCellLocation(), true);
        const float vx = c.x() - mPlayerPos.x();
        const float vy = c.y() - mPlayerPos.y();
        const float toCellLen = std::sqrt(vx * vx + vy * vy);
        if (toCellLen < 1e-3f)
            return false;
        const float cosAngle = (vx * mForwardDir.x() + vy * mForwardDir.y()) / (toCellLen * dirMag);
        return cosAngle < -0.3f;
    }
#endif
}
