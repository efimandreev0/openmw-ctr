#ifndef OPENMW_MWRENDER_CTR_OBJECTS_H
#define OPENMW_MWRENDER_CTR_OBJECTS_H

#include <map>
#include <string>
#include <vector>

#include <osg/ref_ptr>

#include "../../mwworld/ptr.hpp"

namespace Resource
{
    class ResourceSystem;
}

namespace MWWorld
{
    class CellStore;
}

namespace SceneUtil
{
    class UnrefQueue;
}

namespace MWRender
{
    class Animation;

    class Objects
    {
        using PtrAnimationMap = std::map<const MWWorld::LiveCellRefBase*, osg::ref_ptr<Animation>>;

        PtrAnimationMap mObjects;
        Resource::ResourceSystem* mResourceSystem;

        void insertBegin(const MWWorld::Ptr& ptr);

    public:
        Objects(Resource::ResourceSystem* resourceSystem);
        ~Objects();

        void insertModel(const MWWorld::Ptr& ptr, const std::string& model, bool allowLight = true);

        void insertNPC(const MWWorld::Ptr& ptr);
        void insertCreature(const MWWorld::Ptr& ptr, const std::string& model, bool weaponsShields);

        Animation* getAnimation(const MWWorld::Ptr& ptr);
        const Animation* getAnimation(const MWWorld::ConstPtr& ptr) const;

        bool removeObject(const MWWorld::Ptr& ptr);

        void removeCell(const MWWorld::CellStore* store);

        void updatePtr(const MWWorld::Ptr& old, const MWWorld::Ptr& cur);

        template <class Function>
        void forEach(Function&& function) const
        {
            for (const auto& [ref, animation] : mObjects)
                function(*animation);
        }

    private:
        void operator=(const Objects&);
        Objects(const Objects&);
    };
}

#endif
