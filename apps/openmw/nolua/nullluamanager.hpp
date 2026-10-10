#ifndef OPENMW_NOLUA_NULLLUAMANAGER_H
#define OPENMW_NOLUA_NULLLUAMANAGER_H

#include <string>

#include <components/esm3/refnum.hpp>

#include "../mwbase/luamanager.hpp"

#include "playercontrols.hpp"

namespace MWNoLua
{
    class NullLuaManager : public MWBase::LuaManager
    {
    public:
        NullLuaManager() = default;

        void update(float dt);

        PlayerControls& getPlayerControls() { return mPlayerControls; }

        void contentFilesLoaded() override;
        void newGameStarted() override;
        void gameLoaded() override {}
        void gameEnded() override {}
        void noGame() override {}
        void objectAddedToScene(const MWWorld::Ptr& ptr) override;
        void objectRemovedFromScene(const MWWorld::Ptr&) override {}
        void objectTeleported(const MWWorld::Ptr& ptr) override;
        void itemConsumed(const MWWorld::Ptr&, const MWWorld::Ptr&) override {}
        void objectDropped(const MWWorld::Ptr&, const MWWorld::Ptr&, const osg::Vec3f&, const osg::Quat&) override {}
        void objectPlaced(const MWWorld::Ptr&, const MWWorld::Ptr&, const osg::Vec3f&, const osg::Quat&) override {}
        void objectActivated(const MWWorld::Ptr& object, const MWWorld::Ptr& actor) override;
        void useItem(const MWWorld::Ptr& object, const MWWorld::Ptr& actor, bool force) override;
        void animationTextKey(const MWWorld::Ptr&, const std::string&) override {}
        void playAnimation(const MWWorld::Ptr&, const std::string&, const MWRender::AnimPriority&, int, bool, float,
            std::string_view, std::string_view, float, uint32_t, bool) override
        {
        }
        void animationEnded(
            const MWWorld::Ptr&, std::string_view, float, float, std::string_view, std::string_view) override
        {
        }
        void jailTimeServed(const MWWorld::Ptr& actor, int days) override;
        void skillLevelUp(const MWWorld::Ptr& actor, ESM::RefId skillId, std::string_view source) override;
        void skillUse(const MWWorld::Ptr& actor, ESM::RefId skillId, int useType, float scale) override;
        void onHit(const MWWorld::Ptr& attacker, const MWWorld::Ptr& victim, const MWWorld::Ptr& weapon,
            const MWWorld::Ptr& ammo, int attackType, float attackStrength, float attackWindUp, float damage,
            bool isHealth, const osg::Vec3f& hitPos, bool successful, MWMechanics::DamageSourceType) override;
        void exteriorCreated(MWWorld::CellStore&) override {}
        void actorDied(const MWWorld::Ptr& actor) override;
        void onDialogueResponse(const MWWorld::Ptr&, const ESM::DialInfo&, const ESM::Dialogue&) override {}
        void questUpdated(const ESM::RefId&, int) override {}
        void uiModeChanged(const MWWorld::Ptr& arg) override;
        void viewportResized(int, int) override {}
        void savePermanentStorage(const std::filesystem::path&) override {}
        void applyMagicEffects(ESM::RefId id, const MWWorld::Ptr& caster, ESM::RefNum item,
            const MWWorld::Ptr& target, const std::vector<int>& effects, bool ignoreReflect, bool ignoreSpellAbsorption,
            bool stackable, bool isReflect) override;
        void magicProjectileHit(ESM::RefId spellId, const MWWorld::Ptr& caster, ESM::RefNum item,
            const MWWorld::Ptr& victim, const osg::Vec3f& position, const osg::Vec3f& normal) override;
        void inputEvent(const InputEvent&) override {}
        ActorControls* getActorControls(const MWWorld::Ptr& ptr) const override;

        void clear() override;
        void setupPlayer(const MWWorld::Ptr&) override {}

        void write(ESM::ESMWriter& writer, Loading::Listener& progress) override;
        void saveLocalScripts(const MWWorld::Ptr&, ESM::LuaScripts& data) override;
        void applyDelayedActions() override {}
        void readRecord(ESM::ESMReader& reader, uint32_t type) override;
        void loadLocalScripts(const MWWorld::Ptr&, const ESM::LuaScripts&) override {}
        void setContentFileMapping(const std::map<int, int>&) override {}
        void reloadAllScripts() override {}
        void handleConsoleCommand(const std::string&, const std::string&, const MWWorld::Ptr&) override;
        std::string formatResourceUsageStats() const override { return {}; }

    private:
        PlayerControls mPlayerControls;
        int mMusicUpdateCounter = 0;
    };
}

#endif
