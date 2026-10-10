// Resource ObjectCache for OpenMW, forked from osgDB ObjectCache by Robert Osfield, see copyright notice below.
// Changes:
// - removeExpiredObjectsInCache no longer keeps a lock while the unref happens.
// - template allows customized KeyType.
// - objects with uninitialized time stamp are not removed.

/* -*-c++-*- OpenSceneGraph - Copyright (C) 1998-2006 Robert Osfield
 *
 * This library is open source and may be redistributed and/or modified under
 * the terms of the OpenSceneGraph Public License (OSGPL) version 0.0 or
 * (at your option) any later version.  The full license is in LICENSE file
 * included with this distribution, and on the openscenegraph.org website.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * OpenSceneGraph Public License for more details.
 */

#ifndef OPENMW_COMPONENTS_RESOURCE_OBJECTCACHE
#define OPENMW_COMPONENTS_RESOURCE_OBJECTCACHE

#include "cachestats.hpp"

#ifdef __3DS__
#include <osg/Object>
#else
#include <osg/Node>
#endif
#include <osg/ref_ptr>

#include <algorithm>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

namespace osg
{
    class Object;
    class State;
    class NodeVisitor;
    class Stats;
}

namespace Resource
{
    template <class T>
    long useCount(const std::shared_ptr<T>& value)
    {
        return value.use_count();
    }

    template <class T>
    long useCount(const osg::ref_ptr<T>& value)
    {
        return value == nullptr ? 0 : value->referenceCount();
    }

    template <class T>
    concept ReleasesGLObjects = requires(T value, osg::State* state)
    {
        value->releaseGLObjects(state);
    };

    template <class T>
    concept HoldsOsgObject = std::is_convertible_v<decltype(std::declval<T>().get()), osg::Object*>;

    template <class ValueType>
    struct GenericObjectCacheItem
    {
        ValueType mValue;
        double mLastUsage;
    };

    template <typename KeyType, typename ValueType>
    class GenericObjectCache
    {
    public:
        /*
         * @brief Updates usage timestamps and removes expired items
         *
         * Updates the lastUsage timestamp of cached non-nullptr items that have external references.
         * Initializes lastUsage timestamp for new items.
         * Removes items that haven't been referenced for longer than expiryDelay.
         *
         * \note
         * Last usage might be updated from other places so nullptr items
         * that are not referenced elsewhere are not always removed.
         *
         * @param referenceTime the timestamp indicating when the item was most recently used
         * @param expiryDelay the delay after which the cache entry for an item expires
         */
        void update(double referenceTime, double expiryDelay)
        {
            std::vector<ValueType> objectsToRemove;
            {
                const double expiryTime = referenceTime - expiryDelay;
                std::lock_guard<std::mutex> lock(mMutex);

                std::erase_if(mItems, [&](auto& v) {
                    Item& item = v.second;

                    // update last usage timestamp if item is being referenced externally
                    // or initialize if not set
                    if (useCount(item.mValue) > 1 || item.mLastUsage == 0)
                        item.mLastUsage = referenceTime;

                    // skip items that have been accessed since expiryTime
                    if (item.mLastUsage > expiryTime)
                        return false;

                    ++mExpired;

                    // just mark for removal here so objects can be removed in bulk outside the lock
                    if (item.mValue != nullptr)
                        objectsToRemove.push_back(std::move(item.mValue));

                    return true;
                });

#ifdef __3DS__
                // Hard entry cap: the time-based pass above keeps the union of
                // everything touched in the last `expiryDelay` seconds, which
                // spikes during a cell load. Bound it: if we still hold more
                // than CAP entries, evict the least-recently-used ones that are
                // NOT externally referenced (refcount==1 — only the cache holds
                // them). Actively-used templates (refcount>1) are never touched,
                // so this only accelerates what expiry would do anyway.
                constexpr std::size_t kCtrCacheCap = 384;
                if (mItems.size() > kCtrCacheCap)
                {
                    std::vector<typename std::remove_reference_t<decltype(mItems)>::iterator> evictable;
                    evictable.reserve(mItems.size());
                    for (auto it = mItems.begin(); it != mItems.end(); ++it)
                        if (it->second.mValue != nullptr && useCount(it->second.mValue) <= 1)
                            evictable.push_back(it);
                    const std::size_t target = mItems.size() - kCtrCacheCap;
                    if (evictable.size() > 0)
                    {
                        const std::size_t toEvict = std::min(target, evictable.size());
                        std::partial_sort(evictable.begin(), evictable.begin() + toEvict, evictable.end(),
                            [](const auto& a, const auto& b) { return a->second.mLastUsage < b->second.mLastUsage; });
                        for (std::size_t i = 0; i < toEvict; ++i)
                        {
                            objectsToRemove.push_back(std::move(evictable[i]->second.mValue));
                            mItems.erase(evictable[i]);
                            ++mExpired;
                        }
                    }
                }
#endif
            }
            // remove expired items from cache
            objectsToRemove.clear();
        }

        /** Remove all objects in the cache regardless of having external references or expiry times.*/
        void clear()
        {
            std::lock_guard<std::mutex> lock(mMutex);
            mItems.clear();
        }

        /** Add a key,object,timestamp triple to the Registry::ObjectCache.*/
        template <class K>
        void addEntryToObjectCache(K&& key, ValueType object, double timestamp = 0.0)
        {
            std::lock_guard<std::mutex> lock(mMutex);
            const auto it = mItems.find(key);
            if (it == mItems.end())
                mItems.emplace_hint(it, std::forward<K>(key), Item{ std::move(object), timestamp });
            else
                it->second = Item{ std::move(object), timestamp };
        }

        /** Remove Object from cache.*/
        void removeFromObjectCache(const auto& key)
        {
            std::lock_guard<std::mutex> lock(mMutex);
            const auto itr = mItems.find(key);
            if (itr != mItems.end())
                mItems.erase(itr);
        }

        /** Get a value from the object cache; a default-constructed one if absent. */
        ValueType getRefFromObjectCache(const auto& key)
        {
            std::lock_guard<std::mutex> lock(mMutex);
            if (Item* const item = find(key))
                return item->mValue;
            return ValueType();
        }

        std::optional<ValueType> getRefFromObjectCacheOrNone(const auto& key)
        {
            const std::lock_guard<std::mutex> lock(mMutex);
            if (Item* const item = find(key))
                return item->mValue;
            return std::nullopt;
        }

        /** Check if an object is in the cache, and if it is, update its usage time stamp. */
        bool checkInObjectCache(const auto& key, double timeStamp)
        {
            std::lock_guard<std::mutex> lock(mMutex);
            if (Item* const item = find(key))
            {
                item->mLastUsage = timeStamp;
                return true;
            }
            return false;
        }

        /** call releaseGLObjects on all objects attached to the object cache.
            Constrained, so instantiating the cache with a value type that has no such
            method is not an error — it simply does not offer this. */
        void releaseGLObjects(osg::State* state) requires ReleasesGLObjects<ValueType>
        {
            std::lock_guard<std::mutex> lock(mMutex);
            for (const auto& [k, v] : mItems)
                v.mValue->releaseGLObjects(state);
        }

        /** call node->accept(nv); for all nodes in the objectCache. Constrained for
            the same reason as releaseGLObjects. */
#ifndef __3DS__
        void accept(osg::NodeVisitor& nv) requires HoldsOsgObject<ValueType>
        {
            std::lock_guard<std::mutex> lock(mMutex);
            for (const auto& [k, v] : mItems)
                if (osg::Object* const object = v.mValue.get())
                    if (osg::Node* const node = dynamic_cast<osg::Node*>(object))
                        node->accept(nv);
        }
#endif

        /** call operator()(KeyType, osg::Object*) for each object in the cache. */
        template <class Functor>
        void call(Functor&& f)
        {
            std::lock_guard<std::mutex> lock(mMutex);
            for (const auto& [k, v] : mItems)
                f(k, v.mValue.get());
        }

        template <class K>
        std::optional<std::pair<KeyType, ValueType>> lowerBound(K&& key)
        {
            const std::lock_guard<std::mutex> lock(mMutex);
            const auto it = mItems.lower_bound(std::forward<K>(key));
            if (it == mItems.end())
                return std::nullopt;
            return std::pair(it->first, it->second.mValue);
        }

        CacheStats getStats() const
        {
            const std::lock_guard<std::mutex> lock(mMutex);
            return CacheStats{
                .mSize = mItems.size(),
                .mGet = mGet,
                .mHit = mHit,
                .mExpired = mExpired,
            };
        }

    protected:
        using Item = GenericObjectCacheItem<ValueType>;

        std::map<KeyType, Item, std::less<>> mItems;
        mutable std::mutex mMutex;
        std::size_t mGet = 0;
        std::size_t mHit = 0;
        std::size_t mExpired = 0;

        Item* find(const auto& key)
        {
            ++mGet;
            const auto it = mItems.find(key);
            if (it == mItems.end())
                return nullptr;
            ++mHit;
            return &it->second;
        }
    };
}

#endif
