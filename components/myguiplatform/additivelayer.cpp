#include "additivelayer.hpp"

#ifndef __3DS__
#include <osg/BlendFunc>
#include <osg/StateSet>
#endif

#include "myguirendermanager.hpp"

namespace MyGUIPlatform
{

    AdditiveLayer::AdditiveLayer()
    {
#ifndef __3DS__
        mStateSet = new osg::StateSet;
        mStateSet->setAttributeAndModes(new osg::BlendFunc(osg::BlendFunc::SRC_ALPHA, osg::BlendFunc::ONE));
#endif
    }

    AdditiveLayer::~AdditiveLayer()
    {
        // defined in .cpp file since we can't delete incomplete types
    }

    void AdditiveLayer::renderToTarget(MyGUI::IRenderTarget* target, bool update)
    {
        RenderManager& renderManager = static_cast<RenderManager&>(MyGUI::RenderManager::getInstance());

#ifdef __3DS__
        renderManager.setAdditiveBlend(true);
        MyGUI::OverlappedLayer::renderToTarget(target, update);
        renderManager.setAdditiveBlend(false);
#else
        renderManager.setInjectState(mStateSet.get());

        MyGUI::OverlappedLayer::renderToTarget(target, update);

        renderManager.setInjectState(nullptr);
#endif
    }

}
