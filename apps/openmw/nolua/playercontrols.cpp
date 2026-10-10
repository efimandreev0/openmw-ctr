#include "playercontrols.hpp"

#include <algorithm>
#include <cmath>

#include <components/esm3/loadmgef.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/inputmanager.hpp"
#include "../mwbase/mechanicsmanager.hpp"
#include "../mwbase/windowmanager.hpp"
#include "../mwbase/world.hpp"

#include "../mwinput/actions.hpp"

#include "../mwmechanics/actorutil.hpp"
#include "../mwmechanics/creaturestats.hpp"
#include "../mwmechanics/npcstats.hpp"

#include "../mwworld/class.hpp"
#include "../mwworld/containerstore.hpp"
#include "../mwworld/datetimemanager.hpp"
#include "../mwworld/globals.hpp"
#include "../mwworld/player.hpp"

namespace MWNoLua
{
    namespace
    {
        MWBase::InputManager& input()
        {
            return *MWBase::Environment::get().getInputManager();
        }

        bool checkNotWerewolf(const MWWorld::Ptr& player)
        {
            if (player.getClass().getNpcStats(player).isWerewolf())
            {
                MWBase::Environment::get().getWindowManager()->messageBox("#{sWerewolfRefusal}");
                return false;
            }
            return true;
        }

        bool controlsAllowed()
        {
            MWBase::World& world = *MWBase::Environment::get().getWorld();
            if (world.getTimeManager()->isPaused())
                return false;
            if (!input().getControlSwitch("playercontrols"))
                return false;
            if (MWBase::Environment::get().getWindowManager()->isGuiMode())
                return false;
            if (world.getGodModeState())
                return true;
            const MWWorld::Ptr player = MWMechanics::getPlayer();
            return player.getClass()
                       .getCreatureStats(player)
                       .getMagicEffects()
                       .getOrDefault(ESM::MagicEffect::Paralyze)
                       .getMagnitude()
                <= 0;
        }

        bool uiAllowed()
        {
            return input().getControlSwitch("playercontrols");
        }

        float axis(int action)
        {
            return input().getActionValue(action) * 2.f - 1.f;
        }

        float digital(int action, int axisAction, float direction)
        {
            const bool actionActive = input().actionIsActive(action);
            const bool axisActive = axis(axisAction) * direction > 0;
            return (actionActive || axisActive) ? 1.f : 0.f;
        }

        void setStance(const MWWorld::Ptr& player, MWMechanics::DrawState newDrawState)
        {
            MWMechanics::CreatureStats& stats = player.getClass().getCreatureStats(player);
            if (stats.getDrawState() == newDrawState)
                return;
            if (newDrawState == MWMechanics::DrawState::Spell
                && MWBase::Environment::get().getWindowManager()->getSelectedSpell().empty())
            {
                const MWWorld::ContainerStore& store = player.getClass().getContainerStore(player);
                if (store.getSelectedEnchantItem() == store.end())
                    return;
            }
            MWBase::MechanicsManager* mechanics = MWBase::Environment::get().getMechanicsManager();
            if (mechanics->isAttackPreparing(player))
                stats.setAttackingOrSpell(false);
            else if (mechanics->isAttackingOrSpell(player))
                return;
            stats.setDrawState(newDrawState);
        }
    }

    void PlayerControls::onAction(int action)
    {
        using namespace MWInput;
        MWBase::WindowManager* windowManager = MWBase::Environment::get().getWindowManager();
        const MWWorld::Ptr player = MWMechanics::getPlayer();
        if (player.isEmpty())
            return;

        switch (action)
        {
            case A_Jump:
                if (controlsAllowed())
                    mAttemptToJump = input().getControlSwitch("playerjumping");
                break;
            case A_Sneak:
                if (controlsAllowed() && mToggleSneak)
                    mControls.mSneak = !mControls.mSneak;
                break;
            case A_AlwaysRun:
                if (controlsAllowed())
                    mAlwaysRun = !mAlwaysRun;
                break;
            case A_AutoMove:
                if (controlsAllowed())
                    mAutoMove = !mAutoMove;
                break;
            case A_ToggleSpell:
            {
                if (!controlsAllowed())
                    break;
                const MWMechanics::DrawState state = player.getClass().getCreatureStats(player).getDrawState();
                if (state == MWMechanics::DrawState::Spell)
                    setStance(player, MWMechanics::DrawState::Nothing);
                else if (input().getControlSwitch("playermagic") && checkNotWerewolf(player))
                    setStance(player, MWMechanics::DrawState::Spell);
                break;
            }
            case A_ToggleWeapon:
            {
                if (!controlsAllowed())
                    break;
                const MWMechanics::DrawState state = player.getClass().getCreatureStats(player).getDrawState();
                if (state == MWMechanics::DrawState::Weapon)
                    setStance(player, MWMechanics::DrawState::Nothing);
                else if (input().getControlSwitch("playerfighting"))
                    setStance(player, MWMechanics::DrawState::Weapon);
                break;
            }
            case A_Use:
                if (controlsAllowed())
                    mStartUse = true;
                break;
            case A_Inventory:
                if (!uiAllowed())
                    break;
                if (!windowManager->isGuiMode())
                    windowManager->pushGuiMode(MWGui::GM_Inventory);
                else if (windowManager->getMode() == MWGui::GM_Inventory
                    || windowManager->getMode() == MWGui::GM_Container)
                    windowManager->removeGuiMode(windowManager->getMode());
                break;
            case A_Journal:
                if (!uiAllowed())
                    break;
                if (windowManager->getMode() == MWGui::GM_Journal)
                    windowManager->removeGuiMode(MWGui::GM_Journal);
                else if (windowManager->isAllowed(MWGui::GW_Magic))
                    windowManager->pushGuiMode(MWGui::GM_Journal);
                break;
            case A_QuickKeysMenu:
                if (!uiAllowed())
                    break;
                if (windowManager->getMode() == MWGui::GM_QuickKeysMenu)
                    windowManager->removeGuiMode(MWGui::GM_QuickKeysMenu);
                else if (checkNotWerewolf(player)
                    && MWBase::Environment::get().getWorld()->getGlobalInt(MWWorld::Globals::sCharGenState) == -1)
                    windowManager->pushGuiMode(MWGui::GM_QuickKeysMenu);
                break;
            case A_TogglePOV:
                if (controlsAllowed() && input().getControlSwitch("playerviewswitch"))
                    MWBase::Environment::get().getWorld()->togglePOV();
                break;
            default:
                break;
        }
    }

    void PlayerControls::processMovement()
    {
        using namespace MWInput;
        float movement;
        float sideMovement;
        bool run = input().actionIsActive(A_Run) != mAlwaysRun;

        const float stickForward = -axis(A_MoveForwardBackward);
        const float stickRight = axis(A_MoveLeftRight);
        const float stickLength = std::sqrt(stickForward * stickForward + stickRight * stickRight);
        if (mSmoothControllerMovement && stickLength > 0.05f)
        {
            const MWWorld::Ptr player = MWMechanics::getPlayer();
            const MWBase::World& world = *MWBase::Environment::get().getWorld();
            const bool alwaysRun = mControls.mSneak || !world.isOnGround(player) || world.isSwimming(player);
            float scale;
            bool smoothRun;
            if (alwaysRun)
            {
                smoothRun = true;
                scale = 1.f;
            }
            else if (stickLength < 0.5f)
            {
                smoothRun = false;
                scale = 2.f;
            }
            else
            {
                const MWWorld::Class& cls = player.getClass();
                const float runSpeed = cls.getRunSpeed(player);
                const float matchWalkingSpeed = runSpeed > 0 ? cls.getWalkSpeed(player) / runSpeed : 1.f;
                const float runSpeedRatio
                    = 2.f * (stickLength - 0.5f) * (1.f - matchWalkingSpeed) + matchWalkingSpeed;
                smoothRun = true;
                scale = std::min(1.f, runSpeedRatio) / stickLength;
            }
            movement = std::clamp(stickForward * scale, -1.f, 1.f);
            sideMovement = std::clamp(stickRight * scale, -1.f, 1.f);
            run = smoothRun != mAlwaysRun;
        }
        else
        {
            movement = digital(A_MoveForward, A_MoveForwardBackward, -1.f)
                - digital(A_MoveBackward, A_MoveForwardBackward, 1.f);
            sideMovement
                = digital(A_MoveRight, A_MoveLeftRight, 1.f) - digital(A_MoveLeft, A_MoveLeftRight, -1.f);
        }

        if (movement != 0)
            mAutoMove = false;
        else if (mAutoMove)
            movement = 1;

        mControls.mMovement = movement;
        mControls.mSideMovement = sideMovement;
        mControls.mRun = run;
        mControls.mJump = mAttemptToJump;

        if (!mToggleSneak)
            mControls.mSneak = input().actionIsActive(A_Sneak);
    }

    void PlayerControls::processAttacking()
    {
        const MWWorld::Ptr player = MWMechanics::getPlayer();
        const MWMechanics::DrawState state = player.getClass().getCreatureStats(player).getDrawState();
        if (state == MWMechanics::DrawState::Spell)
            mControls.mUse = mStartUse ? MWMechanics::AttackType::Any : MWMechanics::AttackType::NoAttack;
        else if (state == MWMechanics::DrawState::Weapon && input().actionIsActive(MWInput::A_Use))
            mControls.mUse = MWMechanics::AttackType::Any;
        else
            mControls.mUse = MWMechanics::AttackType::NoAttack;
        mStartUse = false;
    }

    void PlayerControls::update()
    {
        if (MWMechanics::getPlayer().isEmpty())
            return;
        if (controlsAllowed())
        {
            processMovement();
            processAttacking();
        }
        else
        {
            mControls.mMovement = 0;
            mControls.mSideMovement = 0;
            mControls.mJump = false;
        }
        mAttemptToJump = false;
        mControls.mChanged = true;
    }

    void PlayerControls::clear()
    {
        const bool alwaysRun = mAlwaysRun;
        *this = PlayerControls();
        mAlwaysRun = alwaysRun;
    }
}
