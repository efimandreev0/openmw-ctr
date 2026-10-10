#ifndef OPENMW_COMPONENTS_POSITIONATTITUDE_TRANSFORM_H
#define OPENMW_COMPONENTS_POSITIONATTITUDE_TRANSFORM_H

#ifdef __3DS__

#include <cstdint>

#include <osg/Quat>
#include <osg/Referenced>
#include <osg/Vec3f>

namespace SceneUtil
{
    class PositionAttitudeTransform : public osg::Referenced
    {
    public:
        PositionAttitudeTransform() = default;

        void setPosition(const osg::Vec3f& pos)
        {
            mPosition = pos;
            ++mRevision;
        }
        const osg::Vec3f& getPosition() const { return mPosition; }

        void setAttitude(const osg::Quat& quat)
        {
            mAttitude = quat;
            ++mRevision;
        }
        const osg::Quat& getAttitude() const { return mAttitude; }

        void setScale(const osg::Vec3f& scale)
        {
            mScale = scale;
            ++mRevision;
        }
        const osg::Vec3f& getScale() const { return mScale; }

        void setNodeMask(unsigned int mask) { mNodeMask = mask; }
        unsigned int getNodeMask() const { return mNodeMask; }

        std::uint32_t getRevision() const { return mRevision; }

        void* mRenderInstance = nullptr;

    protected:
        ~PositionAttitudeTransform() override = default;

    private:
        osg::Vec3f mPosition;
        osg::Quat mAttitude;
        osg::Vec3f mScale{ 1.f, 1.f, 1.f };
        unsigned int mNodeMask = ~0u;
        std::uint32_t mRevision = 0;
    };
}

#else

#include <osg/Transform>

namespace SceneUtil
{

    /// @brief A customized version of osg::PositionAttitudeTransform optimized for speed.
    /// Uses single precision values. Also removed _pivotPoint which we don't need.
    class PositionAttitudeTransform : public osg::Transform
    {
    public:
        PositionAttitudeTransform();

        PositionAttitudeTransform(
            const PositionAttitudeTransform& pat, const osg::CopyOp& copyop = osg::CopyOp::SHALLOW_COPY)
            : Transform(pat, copyop)
            , _position(pat._position)
            , _attitude(pat._attitude)
            , _scale(pat._scale)
        {
        }

        META_Node(SceneUtil, PositionAttitudeTransform)

        inline void setPosition(const osg::Vec3f& pos)
        {
            _position = pos;
            dirtyBound();
        }
        inline const osg::Vec3f& getPosition() const { return _position; }

        inline void setAttitude(const osg::Quat& quat)
        {
            _attitude = quat;
            dirtyBound();
        }
        inline const osg::Quat& getAttitude() const { return _attitude; }

        inline void setScale(const osg::Vec3f& scale)
        {
            _scale = scale;
            dirtyBound();
        }
        inline const osg::Vec3f& getScale() const { return _scale; }

        bool computeLocalToWorldMatrix(osg::Matrix& matrix, osg::NodeVisitor* nv) const override;
        bool computeWorldToLocalMatrix(osg::Matrix& matrix, osg::NodeVisitor* nv) const override;

    protected:
        virtual ~PositionAttitudeTransform() {}

        osg::Vec3f _position;
        osg::Quat _attitude;
        osg::Vec3f _scale;
    };

}

#endif // __3DS__

#endif
