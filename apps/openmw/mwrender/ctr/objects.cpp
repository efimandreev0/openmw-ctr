#include "objects.hpp"

#include <components/misc/resourcehelpers.hpp>
#include <components/misc/strings/algorithm.hpp>
#include <components/resource/resourcesystem.hpp>
#include <components/sceneutil/positionattitudetransform.hpp>

#include "../../mwworld/class.hpp"
#include "../../mwworld/inventorystore.hpp"
#include "../../mwworld/ptr.hpp"

#include "../vismask.hpp"

#include "animation.hpp"
#include "creatureanimation.hpp"
#include "npcanimation.hpp"

namespace MWRender
{
    Objects::Objects(Resource::ResourceSystem* resourceSystem)
        : mResourceSystem(resourceSystem)
    {
    }

    Objects::~Objects()
    {
        mObjects.clear();
    }

    void Objects::insertBegin(const MWWorld::Ptr& ptr)
    {
        osg::ref_ptr<SceneUtil::PositionAttitudeTransform> insert(new SceneUtil::PositionAttitudeTransform);

        const float* f = ptr.getRefData().getPosition().pos;
        insert->setPosition(osg::Vec3f(f[0], f[1], f[2]));

        const float scale = ptr.getCellRef().getScale();
        osg::Vec3f scaleVec(scale, scale, scale);
        ptr.getClass().adjustScale(ptr, scaleVec, true);
        insert->setScale(scaleVec);

        ptr.getRefData().setBaseNode(std::move(insert));
    }

    void Objects::insertModel(const MWWorld::Ptr& ptr, const std::string& mesh, bool allowLight)
    {
        insertBegin(ptr);
        ptr.getRefData().getBaseNode()->setNodeMask(Mask_Object);
        bool animated = ptr.getClass().useAnim();
        std::string animationMesh = mesh;
        if (animated && !mesh.empty())
        {
            animationMesh = Misc::ResourceHelpers::correctActorModelPath(
                VFS::Path::toNormalized(mesh), mResourceSystem->getVFS());
            if (animationMesh == mesh && Misc::StringUtils::ciEndsWith(animationMesh, ".nif"))
                animated = false;
        }

        osg::ref_ptr<ObjectAnimation> anim(
            new ObjectAnimation(ptr, animationMesh, mResourceSystem, animated, allowLight));
        mObjects.emplace(ptr.mRef, std::move(anim));
    }

    void Objects::insertCreature(const MWWorld::Ptr& ptr, const std::string& mesh, bool weaponsShields)
    {
        insertBegin(ptr);
        ptr.getRefData().getBaseNode()->setNodeMask(Mask_Actor);

        bool animated = true;
        std::string animationMesh
            = Misc::ResourceHelpers::correctActorModelPath(VFS::Path::toNormalized(mesh), mResourceSystem->getVFS());
        if (animationMesh == mesh && Misc::StringUtils::ciEndsWith(animationMesh, ".nif"))
            animated = false;

        osg::ref_ptr<Animation> anim;
        if (weaponsShields)
            anim = new CreatureWeaponAnimation(ptr, animationMesh, mResourceSystem, animated);
        else
            anim = new CreatureAnimation(ptr, animationMesh, mResourceSystem, animated);

        if (mObjects.emplace(ptr.mRef, anim).second)
            ptr.getClass().getContainerStore(ptr).setContListener(static_cast<ActorAnimation*>(anim.get()));
    }

    void Objects::insertNPC(const MWWorld::Ptr& ptr)
    {
        insertBegin(ptr);
        ptr.getRefData().getBaseNode()->setNodeMask(Mask_Actor);

        if (ptr.getType() == ESM::REC_NPC_4)
            return; // Morrowind-only build: no ESM4 actors

        osg::ref_ptr<NpcAnimation> anim(new NpcAnimation(ptr, ptr.getRefData().getBaseNode(), mResourceSystem));

        if (mObjects.emplace(ptr.mRef, anim).second)
        {
            ptr.getClass().getInventoryStore(ptr).setInvListener(anim.get());
            ptr.getClass().getInventoryStore(ptr).setContListener(anim.get());
        }
    }

    bool Objects::removeObject(const MWWorld::Ptr& ptr)
    {
        if (!ptr.getRefData().getBaseNode())
            return true;

        const auto iter = mObjects.find(ptr.mRef);
        if (iter != mObjects.end())
        {
            iter->second->removeFromScene();
            mObjects.erase(iter);

            if (ptr.getClass().isActor())
            {
                if (ptr.getClass().hasInventoryStore(ptr))
                    ptr.getClass().getInventoryStore(ptr).setInvListener(nullptr);

                ptr.getClass().getContainerStore(ptr).setContListener(nullptr);
            }

            ptr.getRefData().setBaseNode(nullptr);
            return true;
        }
        return false;
    }

    void Objects::removeCell(const MWWorld::CellStore* store)
    {
        for (PtrAnimationMap::iterator iter = mObjects.begin(); iter != mObjects.end();)
        {
            MWWorld::Ptr ptr = iter->second->getPtr();
            if (ptr.getCell() == store)
            {
                if (ptr.getClass().isActor() && ptr.getRefData().getCustomData())
                {
                    if (ptr.getClass().hasInventoryStore(ptr))
                        ptr.getClass().getInventoryStore(ptr).setInvListener(nullptr);
                    ptr.getClass().getContainerStore(ptr).setContListener(nullptr);
                }

                iter->second->removeFromScene();
                iter = mObjects.erase(iter);
            }
            else
                ++iter;
        }
    }

    void Objects::updatePtr(const MWWorld::Ptr& old, const MWWorld::Ptr& cur)
    {
        PtrAnimationMap::iterator iter = mObjects.find(old.mRef);
        if (iter == mObjects.end())
            return;
        osg::ref_ptr<Animation> animation = iter->second;
        if (old.mRef != cur.mRef)
        {
            mObjects.erase(iter);
            mObjects.emplace(cur.mRef, animation);
        }
        animation->updatePtr(cur);
    }

    Animation* Objects::getAnimation(const MWWorld::Ptr& ptr)
    {
        PtrAnimationMap::const_iterator iter = mObjects.find(ptr.mRef);
        if (iter != mObjects.end())
            return iter->second;

        return nullptr;
    }

    const Animation* Objects::getAnimation(const MWWorld::ConstPtr& ptr) const
    {
        PtrAnimationMap::const_iterator iter = mObjects.find(ptr.mRef);
        if (iter != mObjects.end())
            return iter->second;

        return nullptr;
    }
}
