#ifndef OPENMW_COMPONENTS_SCENEUTIL_COLOURUTIL_H
#define OPENMW_COMPONENTS_SCENEUTIL_COLOURUTIL_H

#include <osg/Vec4f>

namespace SceneUtil
{

    inline float makeOsgColorComponent(unsigned int value, unsigned int shift)
    {
        return float((value >> shift) & 0xFFu) / 255.0f;
    }

    inline osg::Vec4f colourFromRGB(unsigned int clr)
    {
        return osg::Vec4f(
            ((clr >> 0) & 0xFF) / 255.0f, ((clr >> 8) & 0xFF) / 255.0f, ((clr >> 16) & 0xFF) / 255.0f, 1.f);
    }

    inline osg::Vec4f colourFromRGBA(unsigned int value)
    {
        return osg::Vec4f(makeOsgColorComponent(value, 0), makeOsgColorComponent(value, 8),
            makeOsgColorComponent(value, 16), makeOsgColorComponent(value, 24));
    }
}

#endif
