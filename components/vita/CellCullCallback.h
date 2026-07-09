#ifndef OPENMW_COMPONENTS_VITA_CELLCULLCALLBACK_H
#define OPENMW_COMPONENTS_VITA_CELLCULLCALLBACK_H

#if defined(__vita__) || defined(__3DS__)

#include <osg/BoundingBox>
#include <osg/NodeCallback>

namespace osg { class Node; }

namespace Vita
{
    // Tight-AABB cull callback for cell root Groups.
    class CellCullCallback : public osg::NodeCallback
    {
    public:
        explicit CellCullCallback(const osg::BoundingBox& aabb)
            : mAABB(aabb)
        {
        }

        void operator()(osg::Node* node, osg::NodeVisitor* nv) override;

    private:
        osg::BoundingBox mAABB;
    };

    // Padding covers actor wander and door swings beyond the captured bounds.
    osg::BoundingBox computeCellAABB(osg::Node* root, float padding = 1000.f);
}

#endif // __vita__
#endif // OPENMW_COMPONENTS_VITA_CELLCULLCALLBACK_H
