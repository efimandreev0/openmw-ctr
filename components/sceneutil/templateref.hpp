#ifndef OPENMW_COMPONENTS_SCENEUTIL_TEMPLATEREF_H
#define OPENMW_COMPONENTS_SCENEUTIL_TEMPLATEREF_H

#include <osg/CopyOp>
#include <osg/Object>
#include <osg/UserDataContainer>
#include <osg/ref_ptr>

namespace SceneUtil
{
    class TemplateRef : public osg::Object
    {
    public:
        TemplateRef() = default;

        explicit TemplateRef(const osg::Object* object)
            : mObject(object)
        {
        }

        TemplateRef(const TemplateRef& copy, const osg::CopyOp&)
            : mObject(copy.mObject)
        {
        }

        META_Object(SceneUtil, TemplateRef)

    private:
        osg::ref_ptr<const osg::Object> mObject;
    };

    inline void addTemplateRef(osg::Object& derived, const osg::Object* tmpl)
    {
        if (tmpl != nullptr)
            derived.getOrCreateUserDataContainer()->addUserObject(new TemplateRef(tmpl));
    }
}

#endif
