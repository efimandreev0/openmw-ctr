#include <osg/CopyOp>
#include <osg/Object>
#include <osg/Observer>

namespace osg
{
    Referenced* CopyOp::operator()(const Referenced* ref) const
    {
        return const_cast<Referenced*>(ref);
    }

    Object* CopyOp::operator()(const Object* obj) const
    {
        if (obj && (_flags & DEEP_COPY_OBJECTS))
            return obj->clone(*this);
        return const_cast<Object*>(obj);
    }

    Observer::Observer() {}

    Observer::~Observer() {}

    ObserverSet::ObserverSet(const Referenced* observedObject)
        : _observedObject(const_cast<Referenced*>(observedObject))
    {
    }

    ObserverSet::~ObserverSet() {}

    void ObserverSet::addObserver(Observer* observer)
    {
        OpenThreads::ScopedLock<OpenThreads::Mutex> lock(_mutex);
        _observers.insert(observer);
    }

    void ObserverSet::removeObserver(Observer* observer)
    {
        OpenThreads::ScopedLock<OpenThreads::Mutex> lock(_mutex);
        _observers.erase(observer);
    }

    Referenced* ObserverSet::addRefLock()
    {
        OpenThreads::ScopedLock<OpenThreads::Mutex> lock(_mutex);
        if (!_observedObject)
            return nullptr;
        int refCount = _observedObject->ref();
        if (refCount == 1)
        {
            _observedObject->unref_nodelete();
            return nullptr;
        }
        return _observedObject;
    }

    void ObserverSet::signalObjectDeleted(void* ptr)
    {
        OpenThreads::ScopedLock<OpenThreads::Mutex> lock(_mutex);
        for (Observer* observer : _observers)
            observer->objectDeleted(ptr);
        _observers.clear();
        _observedObject = nullptr;
    }
}
