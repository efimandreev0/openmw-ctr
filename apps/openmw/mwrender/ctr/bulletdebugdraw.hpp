#ifndef OPENMW_MWRENDER_CTR_BULLETDEBUGDRAW_H
#define OPENMW_MWRENDER_CTR_BULLETDEBUGDRAW_H

#include <LinearMath/btIDebugDraw.h>

namespace MWRender
{
    class DebugDrawer : public btIDebugDraw
    {
    public:
        DebugDrawer() = default;

        void drawLine(const btVector3& from, const btVector3& to, const btVector3& color) override {}
        void drawContactPoint(const btVector3& pointOnB, const btVector3& normalOnB, btScalar distance, int lifeTime,
            const btVector3& color) override
        {
        }
        void reportErrorWarning(const char* warningString) override {}
        void draw3dText(const btVector3& location, const char* textString) override {}
        void setDebugMode(int isOn) override { mDebugMode = isOn; }
        int getDebugMode() const override { return mDebugMode; }

    private:
        int mDebugMode = 0;
    };
}

#endif
