#ifndef OPENMW_COMPONENTS_SCENEUTIL_USERDATA_HPP
#define OPENMW_COMPONENTS_SCENEUTIL_USERDATA_HPP

#include <utility>

#include <osg/CopyOp>
#include <osg/Object>
#include <osg/UserDataContainer>

namespace SceneUtil
{
    template <class T>
    class UserData final : public osg::Object
    {
    public:
        UserData() = default;

        explicit UserData(T value)
            : mValue(std::move(value))
        {
        }

        UserData(const UserData& copy, const osg::CopyOp& copyop)
            : osg::Object(copy, copyop)
            , mValue(copy.mValue)
        {
        }

        osg::Object* cloneType() const override { return new UserData; }

        osg::Object* clone(const osg::CopyOp& copyop) const override { return new UserData(*this, copyop); }

        bool isSameKindAs(const osg::Object* obj) const override
        {
            return dynamic_cast<const UserData*>(obj) != nullptr;
        }

        const char* libraryName() const override { return "SceneUtil"; }

        const char* className() const override { return "UserData"; }

        T mValue{};
    };

    template <class T>
    T* findUserData(osg::Object& object)
    {
        osg::UserDataContainer* container = object.getUserDataContainer();
        if (container == nullptr)
            return nullptr;
        for (unsigned int i = 0; i < container->getNumUserObjects(); ++i)
            if (UserData<T>* data = dynamic_cast<UserData<T>*>(container->getUserObject(i)))
                return &data->mValue;
        return nullptr;
    }

    template <class T, class F>
    void forEachUserData(osg::Object& object, F&& f)
    {
        osg::UserDataContainer* container = object.getUserDataContainer();
        if (container == nullptr)
            return;
        for (unsigned int i = 0; i < container->getNumUserObjects(); ++i)
            if (UserData<T>* data = dynamic_cast<UserData<T>*>(container->getUserObject(i)))
                f(data->mValue);
    }

    template <class T>
    void addUserData(osg::Object& object, T value)
    {
        object.getOrCreateUserDataContainer()->addUserObject(new UserData<T>(std::move(value)));
    }

}

#endif
