#if defined(__vita__) || defined(__3DS__)

#include "CellCullCallback.h"

#include <osg/ComputeBoundsVisitor>
#include <osg/Node>
#include <osgUtil/CullVisitor>

namespace Vita
{
    void CellCullCallback::operator()(osg::Node* node, osg::NodeVisitor* nv)
    {
        if (auto* cv = dynamic_cast<osgUtil::CullVisitor*>(nv))
        {
            if (cv->isCulled(mAABB))
                return;
        }
        traverse(node, nv);
    }

    osg::BoundingBox computeCellAABB(osg::Node* root, float padding)
    {
        osg::ComputeBoundsVisitor visitor;
        root->accept(visitor);
        osg::BoundingBox bb = visitor.getBoundingBox();
        if (!bb.valid())
            return bb;
        const osg::Vec3f pad(padding, padding, padding);
        osg::BoundingBox padded(bb._min - pad, bb._max + pad);
        return padded;
    }
}

#endif
