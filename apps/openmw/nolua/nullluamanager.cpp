#include "nullluamanager.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

#include <components/esm3/esmreader.hpp>
#include <components/esm3/esmwriter.hpp>
#include <components/esm3/loadalch.hpp>
#include <components/esm3/loadarmo.hpp>
#include <components/esm3/loadbook.hpp>
#include <components/esm3/loadclas.hpp>
#include <components/esm3/loadclot.hpp>
#include <components/esm3/loadcrea.hpp>
#include <components/esm3/loadench.hpp>
#include <components/esm3/loadingr.hpp>
#include <components/esm3/loadmgef.hpp>
#include <components/esm3/loadnpc.hpp>
#include <components/esm3/loadskil.hpp>
#include <components/esm3/loadspel.hpp>
#include <components/esm3/loadweap.hpp>
#include <components/esm/records.hpp>
#include <components/fallback/fallback.hpp>
#include <components/misc/resourcehelpers.hpp>
#include <components/misc/rng.hpp>
#include <components/misc/strings/format.hpp>
#include <components/settings/values.hpp>
#include <components/vfs/manager.hpp>
#include <components/vfs/pathutil.hpp>
#include <components/vfs/recursivedirectoryiterator.hpp>

#include <components/resource/resourcesystem.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/mechanicsmanager.hpp"
#include "../mwbase/soundmanager.hpp"
#include "../mwbase/statemanager.hpp"
#include "../mwbase/windowmanager.hpp"
#include "../mwbase/world.hpp"

#include "../mwmechanics/activespells.hpp"
#include "../mwmechanics/actorutil.hpp"
#include "../mwmechanics/creaturestats.hpp"
#include "../mwmechanics/difficultyscaling.hpp"
#include "../mwmechanics/npcstats.hpp"
#include "../mwmechanics/spellcasting.hpp"
#include "../mwmechanics/spelleffects.hpp"
#include "../mwmechanics/spellutil.hpp"

#include "../mwrender/camera.hpp"

#include "../mwworld/action.hpp"
#include "../mwworld/class.hpp"
#include "../mwworld/datetimemanager.hpp"
#include "../mwworld/esmstore.hpp"
#include "../mwworld/inventorystore.hpp"
#include "../mwworld/manualref.hpp"
#include "../mwworld/player.hpp"
#include "../mwworld/worldmodel.hpp"

#include "esmfallbacks.hpp"

namespace MWNoLua
{
    namespace
    {
        const MWWorld::Store<ESM::GameSetting>& gmsts()
        {
            return MWBase::Environment::get().getESMStore()->get<ESM::GameSetting>();
        }

        float gmstFloat(std::string_view id)
        {
            return gmsts().find(id)->mValue.getFloat();
        }

        int gmstInt(std::string_view id)
        {
            return gmsts().find(id)->mValue.getInteger();
        }

        void playSound3d(const MWWorld::Ptr& ptr, std::string_view sound)
        {
            MWBase::Environment::get().getSoundManager()->playSound3D(ptr, ESM::RefId::stringRefId(sound), 1.f, 1.f);
        }

        void playUiSound(std::string_view sound)
        {
            MWBase::Environment::get().getSoundManager()->playSound(ESM::RefId::stringRefId(sound), 1.f, 1.f,
                MWSound::Type::Sfx, MWSound::PlayMode::NoScaling);
        }

        bool isUiSoundPlaying(std::string_view sound)
        {
            return MWBase::Environment::get().getSoundManager()->getSoundPlaying(
                MWWorld::ConstPtr(), ESM::RefId::stringRefId(sound));
        }


        MWWorld::Ptr pickRandomArmor(const MWWorld::Ptr& actor)
        {
            if (!actor.getClass().hasInventoryStore(actor))
                return {};
            const int roll = Misc::Rng::rollDice(100, MWBase::Environment::get().getWorld()->getPrng());
            int slot;
            if (roll >= 90)
                slot = MWWorld::InventoryStore::Slot_CarriedLeft;
            else if (roll >= 85)
                slot = MWWorld::InventoryStore::Slot_RightGauntlet;
            else if (roll >= 80)
                slot = MWWorld::InventoryStore::Slot_LeftGauntlet;
            else if (roll >= 70)
                slot = MWWorld::InventoryStore::Slot_RightPauldron;
            else if (roll >= 60)
                slot = MWWorld::InventoryStore::Slot_LeftPauldron;
            else if (roll >= 50)
                slot = MWWorld::InventoryStore::Slot_Boots;
            else if (roll >= 40)
                slot = MWWorld::InventoryStore::Slot_Greaves;
            else if (roll >= 30)
                slot = MWWorld::InventoryStore::Slot_Helmet;
            else
                slot = MWWorld::InventoryStore::Slot_Cuirass;
            MWWorld::InventoryStore& inv = actor.getClass().getInventoryStore(actor);
            MWWorld::ContainerStoreIterator it = inv.getSlot(slot);
            if (it == inv.end())
                return {};
            return *it;
        }

        void modifyItemCondition(const MWWorld::Ptr& actor, const MWWorld::Ptr& item, int amount)
        {
            const int maxHealth = item.getClass().getItemMaxHealth(item);
            const int condition = std::min(maxHealth, std::max(0, item.getClass().getItemHealth(item) + amount));
            item.getCellRef().setCharge(condition);
            if (condition <= 0 && actor.getClass().hasInventoryStore(actor))
                actor.getClass().getInventoryStore(actor).unequipItem(item);
        }

        void applyArmor(const MWWorld::Ptr& victim, const MWWorld::Ptr& attacker, const MWWorld::Ptr& weapon,
            const MWWorld::Ptr& ammo, float& healthDamage)
        {
            if (healthDamage <= 0)
                return;
            const float armor = victim.getClass().getArmorRating(victim);
            const float x = healthDamage / (healthDamage + armor);
            const float adjusted = healthDamage * std::max(x, gmstFloat("fCombatArmorMinMult"));
            const int diff = static_cast<int>(std::floor(adjusted - healthDamage));
            healthDamage = std::max(adjusted, 1.f);

            const MWWorld::Ptr item = pickRandomArmor(victim);
            const bool isArmor = !item.isEmpty() && item.getType() == ESM::Armor::sRecordId;
            const ESM::RefId skill = isArmor ? item.getClass().getEquipmentSkill(item) : ESM::Skill::Unarmored;
            if (victim == MWMechanics::getPlayer())
                MWBase::Environment::get().getLuaManager()->skillUse(
                    victim, skill, ESM::Skill::Armor_HitByOpponent, 1.f);
            if (!isArmor)
                return;

            const bool attackerIsUnarmedCreature = !attacker.isEmpty() && weapon.isEmpty() && ammo.isEmpty()
                && attacker.getType() == ESM::Creature::sRecordId;
            if (!attackerIsUnarmedCreature)
                modifyItemCondition(victim, item, diff);

            if (skill == ESM::Skill::LightArmor)
                playSound3d(victim, "Light Armor Hit");
            else if (skill == ESM::Skill::MediumArmor)
                playSound3d(victim, "Medium Armor Hit");
            else if (skill == ESM::Skill::HeavyArmor)
                playSound3d(victim, "Heavy Armor Hit");
            else
                playSound3d(victim, "Hand To Hand Hit");
        }

        int getBloodType(const MWWorld::Ptr& ptr)
        {
            if (ptr.getType() == ESM::NPC::sRecordId)
                return ptr.get<ESM::NPC>()->mBase->mBloodType;
            if (ptr.getType() == ESM::Creature::sRecordId)
                return ptr.get<ESM::Creature>()->mBase->mBloodType;
            return 0;
        }

        void spawnBloodEffect(const MWWorld::Ptr& victim, const osg::Vec3f& position)
        {
            if (victim == MWMechanics::getPlayer())
                return;
            MWBase::World& world = *MWBase::Environment::get().getWorld();
            const int index = Misc::Rng::rollDice(3, world.getPrng());
            const std::string model
                = "meshes/" + std::string(Fallback::Map::getString("Blood_Model_" + std::to_string(index)));
            std::string texture(Fallback::Map::getString("Blood_Texture_" + std::to_string(getBloodType(victim))));
            if (texture.empty())
                texture = Fallback::Map::getString("Blood_Texture_0");
            world.spawnEffect(VFS::Path::Normalized(model), texture, position, 1.f, false, false);
        }

        void applyStagger(const MWWorld::Ptr& victim, float rawHealthDamage, float healthDamage, float fatigueDamage)
        {
            if (healthDamage < 0.001f && fatigueDamage < 0.001f)
                return;
            MWMechanics::CreatureStats& stats = victim.getClass().getCreatureStats(victim);
            const float agility = stats.getAttribute(ESM::Attribute::Agility).getModified();
            const float agilityTerm = agility * gmstFloat("fKnockDownMult");
            const float knockdownTerm
                = agility * gmstInt("iKnockDownOddsMult") * 0.01f + gmstInt("iKnockDownOddsBase");
            const int roll = Misc::Rng::rollDice(100, MWBase::Environment::get().getWorld()->getPrng());
            if (rawHealthDamage > 0 && agilityTerm <= rawHealthDamage && knockdownTerm <= roll)
                stats.setKnockedDown(true);
            else
                stats.setHitRecovery(true);
        }


        struct MagicSource
        {
            std::string_view mName;
            const ESM::EffectList* mEffects = nullptr;
            const ESM::Ingredient* mIngredient = nullptr;
        };

        MagicSource findMagicSource(ESM::RefId id)
        {
            const MWWorld::ESMStore& store = *MWBase::Environment::get().getESMStore();
            auto enchanted = [&](const auto* record) {
                return MagicSource{ record->mName, &store.get<ESM::Enchantment>().find(record->mEnchant)->mEffects };
            };
            switch (store.find(id))
            {
                case ESM::REC_ALCH:
                {
                    const ESM::Potion* potion = store.get<ESM::Potion>().find(id);
                    return { potion->mName, &potion->mEffects };
                }
                case ESM::REC_INGR:
                {
                    const ESM::Ingredient* ingredient = store.get<ESM::Ingredient>().find(id);
                    return { ingredient->mName, nullptr, ingredient };
                }
                case ESM::REC_ARMO:
                    return enchanted(store.get<ESM::Armor>().find(id));
                case ESM::REC_BOOK:
                    return enchanted(store.get<ESM::Book>().find(id));
                case ESM::REC_CLOT:
                    return enchanted(store.get<ESM::Clothing>().find(id));
                case ESM::REC_WEAP:
                    return enchanted(store.get<ESM::Weapon>().find(id));
                default:
                {
                    const ESM::Spell* spell = store.get<ESM::Spell>().find(id);
                    return { spell->mName, &spell->mEffects };
                }
            }
        }


        const ESM::Class& getPlayerClass(const MWWorld::Ptr& player)
        {
            const ESM::RefId& classId = player.get<ESM::NPC>()->mBase->mClass;
            return *MWBase::Environment::get().getESMStore()->get<ESM::Class>().find(classId);
        }

        bool contains(const auto& skills, ESM::RefId id)
        {
            return std::find(std::begin(skills), std::end(skills), id) != std::end(skills);
        }

        std::string formatSkillMessage(std::string_view gmst, const ESM::Skill& skill, int level)
        {
            return Misc::StringUtils::format(
                gmsts().find(gmst)->mValue.getString(), skill.mName, level);
        }

        bool levelUpSkill(const MWWorld::Ptr& player, ESM::RefId skillId, std::string_view source)
        {
            MWMechanics::NpcStats& stats = player.getClass().getNpcStats(player);
            MWMechanics::SkillValue& skillStat = stats.getSkill(skillId);
            const ESM::Skill* skill = MWBase::Environment::get().getESMStore()->get<ESM::Skill>().find(skillId);
            const ESM::Class& npcClass = getPlayerClass(player);
            const bool jail = source == "jail";

            int increase = 1;
            if (jail && skillId != ESM::Skill::Security && skillId != ESM::Skill::Sneak)
                increase = -1;
            if ((skillStat.getBase() >= 100 && increase > 0) || (skillStat.getBase() <= 0 && increase < 0))
                return false;

            skillStat.setBase(skillStat.getBase() + increase);
            if (increase > 0)
            {
                int levelUpProgress = 0;
                int attributeIncrease = gmstInt("iLevelupMiscMultAttriubte");
                if (contains(npcClass.mData.mMinorSkills, skillId))
                {
                    levelUpProgress = gmstInt("iLevelUpMinorMult");
                    attributeIncrease = gmstInt("iLevelUpMinorMultAttribute");
                }
                else if (contains(npcClass.mData.mMajorSkills, skillId))
                {
                    levelUpProgress = gmstInt("iLevelUpMajorMult");
                    attributeIncrease = gmstInt("iLevelUpMajorMultAttribute");
                }
                stats.setLevelProgress(stats.getLevelProgress() + levelUpProgress);
                const ESM::RefId attribute = skill->mData.mAttribute;
                stats.setSkillIncreasesForAttribute(
                    attribute, stats.getSkillIncreasesForAttribute(attribute) + attributeIncrease);
                const auto specialization = static_cast<ESM::Class::Specialization>(skill->mData.mSpecialization);
                stats.setSkillIncreasesForSpecialization(specialization,
                    stats.getSkillIncreasesForSpecialization(specialization) + gmstInt("iLevelupSpecialization"));
            }

            if (!jail)
            {
                MWBase::WindowManager* windowManager = MWBase::Environment::get().getWindowManager();
                playUiSound("skillraise");
                std::string message
                    = formatSkillMessage("sNotifyMessage39", *skill, static_cast<int>(skillStat.getBase()));
                if (source == "book")
                    message = "#{sBookSkillMessage}\n" + message;
                windowManager->messageBox(message, MWGui::ShowInDialogueMode_Never);
                if (stats.getLevelProgress() >= gmstInt("iLevelUpTotal"))
                    windowManager->messageBox("#{sLevelUpMsg}", MWGui::ShowInDialogueMode_Never);
                if (source.empty() || source == "usage")
                    skillStat.setProgress(0);
            }
            return true;
        }


        struct Playlist
        {
            std::vector<VFS::Path::Normalized> mTracks;
            std::vector<size_t> mOrder;
            bool mInitialized = false;
        };

        Playlist sExplore;
        Playlist sBattle;
        const Playlist* sCurrentPlaylist = nullptr;

        void initPlaylist(Playlist& playlist, std::string_view dir)
        {
            if (playlist.mInitialized)
                return;
            playlist.mInitialized = true;
            const VFS::Manager* vfs = MWBase::Environment::get().getResourceSystem()->getVFS();
            for (const auto& path : vfs->getRecursiveDirectoryIterator(dir))
                playlist.mTracks.emplace_back(path);
        }

        const VFS::Path::Normalized* nextTrack(Playlist& playlist)
        {
            if (playlist.mTracks.empty())
                return nullptr;
            if (playlist.mOrder.empty())
            {
                for (size_t i = 0; i < playlist.mTracks.size(); ++i)
                    playlist.mOrder.push_back(i);
                auto& prng = MWBase::Environment::get().getWorld()->getPrng();
                for (size_t i = playlist.mOrder.size(); i > 1; --i)
                    std::swap(playlist.mOrder[i - 1], playlist.mOrder[Misc::Rng::rollDice(static_cast<int>(i), prng)]);
            }
            const size_t index = playlist.mOrder.back();
            playlist.mOrder.pop_back();
            return &playlist.mTracks[index];
        }

        void updateMusic()
        {
            MWBase::SoundManager* sound = MWBase::Environment::get().getSoundManager();
            MWBase::World* world = MWBase::Environment::get().getWorld();
            const MWWorld::Ptr player = world->getPlayerPtr();
            if (player.isEmpty())
                return;
            const bool playing = sound->isMusicPlaying();
            if (playing && sound->getMusicType() == MWSound::MusicType::MWScript)
                return;
            if (player.getClass().getCreatureStats(player).isDead() && playing)
                return;

            initPlaylist(sExplore, "music/explore/");
            initPlaylist(sBattle, "music/battle/");

            Playlist* playlist = &sExplore;
            if (!sBattle.mTracks.empty() && world->getPlayer().enemiesNearby())
                playlist = &sBattle;

            if (playlist == sCurrentPlaylist && playing)
                return;
            if (const VFS::Path::Normalized* track = nextTrack(*playlist))
                sound->streamMusic(*track, MWSound::MusicType::Normal);
            sCurrentPlaylist = playlist;
        }
    }

    void NullLuaManager::update(float /*dt*/)
    {
        mPlayerControls.update();
        if (++mMusicUpdateCounter >= 15)
        {
            mMusicUpdateCounter = 0;
            if (MWBase::Environment::get().getStateManager()->getState()
                == MWBase::StateManager::State_Running)
                updateMusic();
        }
    }

    void NullLuaManager::contentFilesLoaded()
    {
        applyEsmFallbacks(*MWBase::Environment::get().getESMStore());
    }

    void NullLuaManager::newGameStarted()
    {
        mPlayerControls.clear();
    }

    void NullLuaManager::clear()
    {
        mPlayerControls.clear();
        sCurrentPlaylist = nullptr;
    }

    void NullLuaManager::objectAddedToScene(const MWWorld::Ptr& ptr)
    {
        if (ptr.getClass().isActor())
            MWBase::Environment::get().getWorldModel()->registerPtr(ptr);
    }

    void NullLuaManager::objectTeleported(const MWWorld::Ptr& ptr)
    {
        if (ptr == MWMechanics::getPlayer())
            MWBase::Environment::get().getWorld()->getCamera()->instantTransition();
    }

    void NullLuaManager::objectActivated(const MWWorld::Ptr& object, const MWWorld::Ptr& actor)
    {
        MWBase::World* world = MWBase::Environment::get().getWorld();
        if (world->getTimeManager()->isPaused())
            return;
        if (object.getContainerStore() != nullptr)
            return;
        if (actor.getClass().isActor())
            world->breakInvisibility(actor);
        if (!object.getRefData().activate())
            return;
        object.getClass().activate(object, actor)->execute(actor);
    }

    void NullLuaManager::useItem(const MWWorld::Ptr& object, const MWWorld::Ptr& actor, bool force)
    {
        if (actor == MWBase::Environment::get().getWorld()->getPlayerPtr())
            MWBase::Environment::get().getWindowManager()->useItem(object, force);
        else
            object.getClass().use(object, force)->execute(actor, true);
    }

    void NullLuaManager::onHit(const MWWorld::Ptr& attacker, const MWWorld::Ptr& victim, const MWWorld::Ptr& weapon,
        const MWWorld::Ptr& ammo, int /*attackType*/, float /*attackStrength*/, float /*attackWindUp*/, float damage,
        bool isHealth, const osg::Vec3f& hitPos, bool successful, MWMechanics::DamageSourceType sourceType)
    {
        float healthDamage = isHealth ? damage : 0.f;
        float fatigueDamage = isHealth ? 0.f : damage;
        const MWWorld::Ptr player = MWMechanics::getPlayer();
        const bool godMode = victim == player && MWBase::Environment::get().getWorld()->getGodModeState();

        if (successful && !godMode)
        {
            const float rawHealthDamage = healthDamage;
            applyArmor(victim, attacker, weapon, ammo, healthDamage);
            if (!attacker.isEmpty())
                healthDamage = scaleDamage(healthDamage, attacker, victim);
            if (healthDamage > 0)
            {
                playSound3d(victim, "Health Damage");
                spawnBloodEffect(victim, hitPos);
            }
            if (!attacker.isEmpty())
                applyStagger(victim, rawHealthDamage, healthDamage, fatigueDamage);
        }
        else if (!attacker.isEmpty() && attacker == player)
            playSound3d(victim, "miss");

        std::map<std::string, float> damages;
        if (isHealth)
            damages["health"] = healthDamage;
        else
            damages["fatigue"] = fatigueDamage;
        ESM::RefId weaponId;
        if (!ammo.isEmpty())
            weaponId = ammo.getCellRef().getRefId();
        else if (!weapon.isEmpty())
            weaponId = weapon.getCellRef().getRefId();
        victim.getClass().onHit(victim, damages, weaponId, attacker, successful, sourceType);
    }

    void NullLuaManager::actorDied(const MWWorld::Ptr& actor)
    {
        if (actor == MWMechanics::getPlayer())
            MWBase::Environment::get().getSoundManager()->streamMusic(
                VFS::Path::Normalized("music/special/mw_death.mp3"), MWSound::MusicType::Normal);
    }

    void NullLuaManager::uiModeChanged(const MWWorld::Ptr& /*arg*/)
    {
        MWBase::WindowManager* windowManager = MWBase::Environment::get().getWindowManager();
        MWWorld::DateTimeManager* timeManager = MWBase::Environment::get().getWorld()->getTimeManager();
        const std::vector<MWGui::GuiMode>& stack = windowManager->getGuiModeStack();
        if (stack.empty())
            timeManager->unpause("ui");
        else
            timeManager->pause("ui");

        static MWGui::GuiMode oldMode = MWGui::GM_None;
        const MWGui::GuiMode newMode = stack.empty() ? MWGui::GM_None : stack.back();
        if (oldMode == newMode)
            return;
        auto isBook = [](MWGui::GuiMode mode) { return mode == MWGui::GM_Journal || mode == MWGui::GM_Book; };
        if (isBook(newMode))
            playUiSound("book open");
        else if (isBook(oldMode))
        {
            if (!isUiSoundPlaying("item book up"))
                playUiSound("book close");
        }
        else if (newMode == MWGui::GM_Scroll || oldMode == MWGui::GM_Scroll)
        {
            if (!isUiSoundPlaying("item book up"))
                playUiSound("scroll");
        }
        oldMode = newMode;
    }

    void NullLuaManager::applyMagicEffects(ESM::RefId id, const MWWorld::Ptr& caster, ESM::RefNum item,
        const MWWorld::Ptr& target, const std::vector<int>& effects, bool ignoreReflect, bool ignoreSpellAbsorption,
        bool stackable, bool /*isReflect*/)
    {
        if (target.isEmpty() || effects.empty())
            return;
        const MWWorld::ESMStore& store = *MWBase::Environment::get().getESMStore();
        const MWWorld::Ptr player = MWMechanics::getPlayer();
        const bool targetIsActor = target.getClass().isActor();
        if (targetIsActor && target.getClass().getCreatureStats(target).isDeathAnimationFinished())
            return;

        const MagicSource source = findMagicSource(id);
        std::vector<ESM::IndexedENAMstruct> enams;
        if (source.mIngredient != nullptr)
        {
            for (int index : effects)
                if (auto effect = MWMechanics::rollIngredientEffect(target, source.mIngredient, index))
                    enams.push_back(effect->mList[0]);
            if (enams.empty() && target == player)
            {
                const std::string message = Misc::StringUtils::format(
                    store.get<ESM::GameSetting>().find("sNotifyMessage50")->mValue.getString(),
                    source.mIngredient->mName);
                MWBase::Environment::get().getWindowManager()->messageBox(message);
            }
        }
        else
        {
            for (int index : effects)
                if (index >= 0 && index < static_cast<int>(source.mEffects->mList.size()))
                    enams.push_back(source.mEffects->mList[index]);
        }
        if (enams.empty())
            return;

        MWMechanics::ActiveSpells::ActiveSpellParams params(caster, id, std::string(source.mName), item);
        params.setFlag(ESM::ActiveSpells::Flag_Temporary);
        if (stackable)
            params.setFlag(ESM::ActiveSpells::Flag_Stackable);
        params.setActiveSpellId(MWBase::Environment::get().getESMStore()->generateId());

        bool affectsHealth = false;
        for (const ESM::IndexedENAMstruct& enam : enams)
        {
            const ESM::MagicEffect* mgef = store.get<ESM::MagicEffect>().find(enam.mData.mEffectID);
            MWMechanics::ActiveSpells::ActiveEffect effect;
            effect.mEffectId = enam.mData.mEffectID;
            effect.mArg = MWMechanics::EffectKey(enam.mData).mArg;
            effect.mMagnitude = 0.f;
            effect.mMinMagnitude = static_cast<float>(enam.mData.mMagnMin);
            effect.mMaxMagnitude = static_cast<float>(enam.mData.mMagnMax);
            effect.mEffectIndex = static_cast<int32_t>(enam.mIndex);
            effect.mFlags = ESM::ActiveEffect::Flag_None;
            if (ignoreReflect)
                effect.mFlags |= ESM::ActiveEffect::Flag_Ignore_Reflect;
            if (ignoreSpellAbsorption)
                effect.mFlags |= ESM::ActiveEffect::Flag_Ignore_SpellAbsorption;
            const bool hasDuration = !(mgef->mData.mFlags & ESM::MagicEffect::NoDuration);
            effect.mDuration = hasDuration ? static_cast<float>(enam.mData.mDuration) : 1.f;
            effect.mTimeLeft = effect.mDuration;
            params.getEffects().emplace_back(effect);

            affectsHealth = affectsHealth || (mgef->mData.mFlags & ESM::MagicEffect::Harmful)
                || effect.mEffectId == ESM::MagicEffect::RestoreHealth;

            if (!targetIsActor && !hasDuration)
                MWMechanics::playEffects(target, *mgef);
        }

        if (targetIsActor)
        {
            target.getClass().getCreatureStats(target).getActiveSpells().addSpell(params);
            if (affectsHealth && caster == player && target != player)
                MWBase::Environment::get().getWindowManager()->setEnemy(target);
            if (target == player && MWBase::Environment::get().getWorld()->getTimeManager()->isPaused())
            {
                MWBase::Environment::get().getMechanicsManager()->updateMagicEffects(player);
                MWBase::Environment::get().getWindowManager()->updateSpellWindow();
            }
        }
        else
        {
            for (auto& effect : params.getEffects())
                MWMechanics::applyMagicEffect(target, caster, params, effect, 0.f);
        }
    }

    void NullLuaManager::magicProjectileHit(ESM::RefId spellId, const MWWorld::Ptr& caster, ESM::RefNum item,
        const MWWorld::Ptr& victim, const osg::Vec3f& position, const osg::Vec3f& /*normal*/)
    {
        MWMechanics::CastSpell cast(caster, victim);
        cast.mHitPosition = position;
        cast.mId = spellId;
        cast.mItem = item;
        const MagicSource source = findMagicSource(spellId);
        if (source.mEffects == nullptr)
            return;
        cast.mSourceName = source.mName;
        cast.inflict(victim, *source.mEffects, ESM::RT_Target);
    }

    void NullLuaManager::skillUse(const MWWorld::Ptr& actor, ESM::RefId skillId, int useType, float scale)
    {
        if (actor != MWMechanics::getPlayer())
            return;
        if (useType < 0 || useType > 3)
            return;
        MWMechanics::NpcStats& stats = actor.getClass().getNpcStats(actor);
        if (stats.isWerewolf())
            return;
        const ESM::Skill* skill = MWBase::Environment::get().getESMStore()->get<ESM::Skill>().find(skillId);
        const float skillGain = skill->mData.mUseValue[useType] * scale;
        MWMechanics::SkillValue& skillStat = stats.getSkill(skillId);
        if ((skillStat.getBase() >= 100 && skillGain > 0) || (skillStat.getBase() <= 0 && skillGain < 0))
            return;
        skillStat.setProgress(
            skillStat.getProgress() + skillGain / stats.getSkillProgressRequirement(skillId, getPlayerClass(actor)));
        if (skillStat.getProgress() >= 1)
            levelUpSkill(actor, skillId, "usage");
    }

    void NullLuaManager::skillLevelUp(const MWWorld::Ptr& actor, ESM::RefId skillId, std::string_view source)
    {
        if (actor != MWMechanics::getPlayer())
            return;
        levelUpSkill(actor, skillId, source);
    }

    void NullLuaManager::jailTimeServed(const MWWorld::Ptr& actor, int days)
    {
        if (actor != MWMechanics::getPlayer() || days <= 0)
            return;
        const MWWorld::Store<ESM::Skill>& skills = MWBase::Environment::get().getESMStore()->get<ESM::Skill>();
        MWMechanics::NpcStats& stats = actor.getClass().getNpcStats(actor);

        std::vector<std::pair<const ESM::Skill*, float>> oldLevels;
        for (const ESM::Skill& skill : skills)
            oldLevels.emplace_back(&skill, stats.getSkill(skill.mId).getBase());
        if (oldLevels.empty())
            return;

        auto& prng = MWBase::Environment::get().getWorld()->getPrng();
        for (int day = 0; day < days; ++day)
        {
            const ESM::Skill* skill = oldLevels[Misc::Rng::rollDice(static_cast<int>(oldLevels.size()), prng)].first;
            levelUpSkill(actor, skill->mId, "jail");
        }

        std::string message = Misc::StringUtils::format(
            gmsts().find(days == 1 ? "sNotifyMessage42" : "sNotifyMessage43")->mValue.getString(), days);
        for (const auto& [skill, oldLevel] : oldLevels)
        {
            const float level = stats.getSkill(skill->mId).getBase();
            if (level == oldLevel)
                continue;
            message += '\n';
            message += formatSkillMessage(
                level < oldLevel ? "sNotifyMessage44" : "sNotifyMessage39", *skill, static_cast<int>(level));
        }
        MWBase::Environment::get().getWindowManager()->interactiveMessageBox(message, { "#{Interface:OK}" });
    }

    MWBase::LuaManager::ActorControls* NullLuaManager::getActorControls(const MWWorld::Ptr& ptr) const
    {
        if (ptr != MWMechanics::getPlayer())
            return nullptr;
        return const_cast<ActorControls*>(&mPlayerControls.getControls());
    }

    void NullLuaManager::write(ESM::ESMWriter& writer, Loading::Listener& /*progress*/)
    {
        writer.startRecord(ESM::REC_LUAM);
        writer.writeHNT<double>("LUAW", MWBase::Environment::get().getWorld()->getTimeManager()->getSimulationTime());
        writer.writeFormId(MWBase::Environment::get().getWorldModel()->getLastGeneratedRefNum(), true);
        writer.endRecord(ESM::REC_LUAM);
    }

    void NullLuaManager::saveLocalScripts(const MWWorld::Ptr&, ESM::LuaScripts& data)
    {
        data.mScripts.clear();
    }

    void NullLuaManager::readRecord(ESM::ESMReader& reader, uint32_t type)
    {
        if (type != ESM::REC_LUAM)
            throw std::runtime_error("ESM::REC_LUAM is expected");
        double simulationTime;
        reader.getHNT(simulationTime, "LUAW");
        MWBase::Environment::get().getWorld()->getTimeManager()->setSimulationTime(simulationTime);
        const ESM::FormId lastGenerated = reader.getFormId(true);
        if (lastGenerated.hasContentFile())
            throw std::runtime_error("Last generated RefNum is invalid");
        MWBase::Environment::get().getWorldModel()->setLastGeneratedRefNum(lastGenerated);
        reader.skipRecord();
    }

    void NullLuaManager::handleConsoleCommand(const std::string&, const std::string&, const MWWorld::Ptr&)
    {
        MWBase::Environment::get().getWindowManager()->printToConsole(
            "Lua is not available in this build", MWBase::WindowManager::sConsoleColor_Error);
    }
}
