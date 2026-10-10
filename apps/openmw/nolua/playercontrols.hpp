#ifndef OPENMW_NOLUA_PLAYERCONTROLS_H
#define OPENMW_NOLUA_PLAYERCONTROLS_H

#include "../mwbase/luamanager.hpp"

namespace MWNoLua
{
    class PlayerControls
    {
    public:
        void onAction(int action);

        void update();

        void clear();

        MWBase::LuaManager::ActorControls& getControls() { return mControls; }
        const MWBase::LuaManager::ActorControls& getControls() const { return mControls; }

        bool getAlwaysRun() const { return mAlwaysRun; }
        void setAlwaysRun(bool value) { mAlwaysRun = value; }

    private:
        void processMovement();
        void processAttacking();

        MWBase::LuaManager::ActorControls mControls;
        bool mAlwaysRun = true;
        bool mToggleSneak = false;
        bool mSmoothControllerMovement = true;
        bool mAutoMove = false;
        bool mAttemptToJump = false;
        bool mStartUse = false;
    };
}

#endif
