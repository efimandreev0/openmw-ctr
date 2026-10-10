#include "esmfallbacks.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>

#include <components/esm/attr.hpp>
#include <components/esm3/loadgmst.hpp>
#include <components/esm3/loaddoor.hpp>
#include <components/esm3/loadmgef.hpp>
#include <components/esm3/loadskil.hpp>
#include <components/esm3/loadstat.hpp>
#include <components/fallback/fallback.hpp>

#include "../mwworld/esmstore.hpp"

namespace MWNoLua
{
    namespace
    {
        using GmstStore = MWWorld::Store<ESM::GameSetting>;

        std::string getString(const GmstStore& gmsts, std::string_view key)
        {
            const ESM::GameSetting* gmst = gmsts.search(ESM::RefId::stringRefId(key));
            if (gmst == nullptr || gmst->mValue.getType() != ESM::VT_String || gmst->mValue.getString().empty())
                return std::string(key);
            return gmst->mValue.getString();
        }

        float getNumber(const GmstStore& gmsts, std::string_view key)
        {
            const ESM::GameSetting* gmst = gmsts.search(ESM::RefId::stringRefId(key));
            if (gmst == nullptr)
                return 0.f;
            switch (gmst->mValue.getType())
            {
                case ESM::VT_Int:
                case ESM::VT_Short:
                case ESM::VT_Long:
                    return static_cast<float>(gmst->mValue.getInteger());
                case ESM::VT_Float:
                    return gmst->mValue.getFloat();
                default:
                    return 0.f;
            }
        }

        void setNumber(GmstStore& gmsts, std::string_view key, double value)
        {
            const ESM::RefId id = ESM::RefId::stringRefId(key);
            ESM::GameSetting gmst;
            bool preferFloat;
            if (const ESM::GameSetting* found = gmsts.search(id))
            {
                gmst = *found;
                preferFloat = gmst.mValue.getType() == ESM::VT_Float;
            }
            else
            {
                gmst.blank();
                gmst.mId = id;
                preferFloat = id.startsWith("f");
            }
            const int32_t intV = static_cast<int32_t>(value);
            const float floatV = static_cast<float>(value);
            if (intV == value && (!preferFloat || floatV != value))
            {
                gmst.mValue.setType(ESM::VT_Int);
                gmst.mValue.setInteger(intV);
            }
            else
            {
                gmst.mValue.setType(ESM::VT_Float);
                gmst.mValue.setFloat(floatV);
            }
            gmsts.insertStatic(gmst);
        }

        void setString(GmstStore& gmsts, std::string_view key, std::string_view value)
        {
            const ESM::RefId id = ESM::RefId::stringRefId(key);
            ESM::GameSetting gmst;
            if (const ESM::GameSetting* found = gmsts.search(id))
                gmst = *found;
            else
            {
                gmst.blank();
                gmst.mId = id;
            }
            gmst.mValue.setType(ESM::VT_String);
            gmst.mValue.setString(std::string(value));
            gmsts.insertStatic(gmst);
        }

        void generateAttributes(MWWorld::ESMStore& store)
        {
            struct Value
            {
                const ESM::RefId& mId;
                std::string_view mName, mDescription, mIcon, mWerewolf;
            };
            const std::array<Value, 8> values{ {
                { ESM::Attribute::Strength, "sAttributeStrength", "sStrDesc", "icons/k/attribute_strength.dds",
                    "fWerewolfStrength" },
                { ESM::Attribute::Intelligence, "sAttributeIntelligence", "sIntDesc", "icons/k/attribute_int.dds",
                    "fWerewolfIntellegence" },
                { ESM::Attribute::Willpower, "sAttributeWillpower", "sWilDesc", "icons/k/attribute_wilpower.dds",
                    "fWerewolfWillpower" },
                { ESM::Attribute::Agility, "sAttributeAgility", "sAgiDesc", "icons/k/attribute_agility.dds",
                    "fWerewolfAgility" },
                { ESM::Attribute::Speed, "sAttributeSpeed", "sSpdDesc", "icons/k/attribute_speed.dds",
                    "fWerewolfSpeed" },
                { ESM::Attribute::Endurance, "sAttributeEndurance", "sEndDesc", "icons/k/attribute_endurance.dds",
                    "fWerewolfEndurance" },
                { ESM::Attribute::Personality, "sAttributePersonality", "sPerDesc",
                    "icons/k/attribute_personality.dds", "fWerewolfPersonality" },
                { ESM::Attribute::Luck, "sAttributeLuck", "sLucDesc", "icons/k/attribute_luck.dds", "fWerewolfLuck" },
            } };
            const GmstStore& gmsts = store.get<ESM::GameSetting>();
            auto& attributes = store.getWritable<ESM::Attribute>();
            for (const Value& value : values)
            {
                ESM::Attribute attribute;
                attribute.mId = value.mId;
                attribute.mName = getString(gmsts, value.mName);
                attribute.mDescription = getString(gmsts, value.mDescription);
                attribute.mIcon = value.mIcon;
                attribute.mWerewolfValue = getNumber(gmsts, value.mWerewolf);
                attributes.insertStatic(attribute);
            }
        }

        void setSkillProperties(MWWorld::ESMStore& store)
        {
            struct Value
            {
                const ESM::RefId& mId;
                std::string_view mName, mIcon, mWerewolf, mSchool;
            };
            const std::array<Value, 27> values{ {
                { ESM::Skill::Block, "sSkillBlock", "icons/k/combat_block.dds", "fWerewolfBlock", {} },
                { ESM::Skill::Armorer, "sSkillArmorer", "icons/k/combat_armor.dds", "fWerewolfArmorer", {} },
                { ESM::Skill::MediumArmor, "sSkillMediumarmor", "icons/k/combat_mediumarmor.dds",
                    "fWerewolfMediumarmor", {} },
                { ESM::Skill::HeavyArmor, "sSkillHeavyarmor", "icons/k/combat_heavyarmor.dds", "fWerewolfHeavyarmor",
                    {} },
                { ESM::Skill::BluntWeapon, "sSkillBluntweapon", "icons/k/combat_blunt.dds", "fWerewolfBluntweapon",
                    {} },
                { ESM::Skill::LongBlade, "sSkillLongblade", "icons/k/combat_longblade.dds", "fWerewolfLongblade", {} },
                { ESM::Skill::Axe, "sSkillAxe", "icons/k/combat_axe.dds", "fWerewolfAxe", {} },
                { ESM::Skill::Spear, "sSkillSpear", "icons/k/combat_spear.dds", "fWerewolfSpear", {} },
                { ESM::Skill::Athletics, "sSkillAthletics", "icons/k/combat_athletics.dds", "fWerewolfAthletics", {} },
                { ESM::Skill::Enchant, "sSkillEnchant", "icons/k/magic_enchant.dds", "fWerewolfEnchant", {} },
                { ESM::Skill::Destruction, "sSkillDestruction", "icons/k/magic_destruction.dds",
                    "fWerewolfDestruction", "destruction" },
                { ESM::Skill::Alteration, "sSkillAlteration", "icons/k/magic_alteration.dds", "fWerewolfAlteration",
                    "alteration" },
                { ESM::Skill::Illusion, "sSkillIllusion", "icons/k/magic_illusion.dds", "fWerewolfIllusion",
                    "illusion" },
                { ESM::Skill::Conjuration, "sSkillConjuration", "icons/k/magic_conjuration.dds",
                    "fWerewolfConjuration", "conjuration" },
                { ESM::Skill::Mysticism, "sSkillMysticism", "icons/k/magic_mysticism.dds", "fWerewolfMysticism",
                    "mysticism" },
                { ESM::Skill::Restoration, "sSkillRestoration", "icons/k/magic_restoration.dds",
                    "fWerewolfRestoration", "restoration" },
                { ESM::Skill::Alchemy, "sSkillAlchemy", "icons/k/magic_alchemy.dds", "fWerewolfAlchemy", {} },
                { ESM::Skill::Unarmored, "sSkillUnarmored", "icons/k/magic_unarmored.dds", "fWerewolfUnarmored", {} },
                { ESM::Skill::Security, "sSkillSecurity", "icons/k/stealth_security.dds", "fWerewolfSecurity", {} },
                { ESM::Skill::Sneak, "sSkillSneak", "icons/k/stealth_sneak.dds", "fWerewolfSneak", {} },
                { ESM::Skill::Acrobatics, "sSkillAcrobatics", "icons/k/stealth_acrobatics.dds", "fWerewolfAcrobatics",
                    {} },
                { ESM::Skill::LightArmor, "sSkillLightarmor", "icons/k/stealth_lightarmor.dds", "fWerewolfLightarmor",
                    {} },
                { ESM::Skill::ShortBlade, "sSkillShortblade", "icons/k/stealth_shortblade.dds", "fWerewolfShortblade",
                    {} },
                { ESM::Skill::Marksman, "sSkillMarksman", "icons/k/stealth_marksman.dds", "fWerewolfMarksman", {} },
                { ESM::Skill::Mercantile, "sSkillMercantile", "icons/k/stealth_mercantile.dds",
                    "fWerewolfMerchantile", {} },
                { ESM::Skill::Speechcraft, "sSkillSpeechcraft", "icons/k/stealth_speechcraft.dds",
                    "fWerewolfSpeechcraft", {} },
                { ESM::Skill::HandToHand, "sSkillHandtohand", "icons/k/stealth_handtohand.dds", "fWerewolfHandtohand",
                    {} },
            } };
            const GmstStore& gmsts = store.get<ESM::GameSetting>();
            auto& skills = store.getWritable<ESM::Skill>();
            for (const Value& value : values)
            {
                const ESM::Skill* found = skills.search(value.mId);
                if (found == nullptr)
                    continue;
                ESM::Skill skill = *found;
                skill.mName = getString(gmsts, value.mName);
                skill.mIcon = value.mIcon;
                skill.mWerewolfValue = getNumber(gmsts, value.mWerewolf);
                if (!value.mSchool.empty())
                {
                    const std::string school(value.mSchool);
                    ESM::MagicSchool magicSchool;
                    magicSchool.mAreaSound = ESM::RefId::stringRefId(school + " area");
                    magicSchool.mBoltSound = ESM::RefId::stringRefId(school + " bolt");
                    magicSchool.mCastSound = ESM::RefId::stringRefId(school + " cast");
                    magicSchool.mFailureSound = ESM::RefId::stringRefId("Spell Failure " + school);
                    magicSchool.mHitSound = ESM::RefId::stringRefId(school + " hit");
                    magicSchool.mName = getString(gmsts, "sSchool" + school);
                    magicSchool.mAutoCalcMax = static_cast<int32_t>(getNumber(gmsts, "iAutoSpell" + school + "Max"));
                    skill.mSchool = std::move(magicSchool);
                }
                skills.insertStatic(skill);
            }
        }

        void generateDefaultDoors(MWWorld::ESMStore& store)
        {
            auto& doors = store.getWritable<ESM::Door>();
            const ESM::RefId id = ESM::RefId::stringRefId("prisonmarker");
            if (doors.search(id) != nullptr)
                return;
            ESM::Door door;
            door.blank();
            door.mId = id;
            door.mModel = "meshes/marker_prison.nif";
            doors.insertStatic(door);
        }

        void generateDefaultGMSTs(MWWorld::ESMStore& store)
        {
            auto& gmsts = store.getWritable<ESM::GameSetting>();
            const std::pair<std::string_view, std::string_view> strings[] = {
                { "sCompanionShare", "Companion Share" },
                { "sCompanionWarningMessage", "Warning message" },
                { "sCompanionWarningButtonOne", "Button 1" },
                { "sCompanionWarningButtonTwo", "Button 2" },
                { "sProfitValue", "Profit Value" },
                { "sTeleportDisabled", "Teleport disabled" },
                { "sLevitateDisabled", "Levitate disabled" },
                { "sDifficulty", "Difficulty" },
                { "sAuto_Run", "Auto Run" },
                { "sServiceRefusal", "Service Refusal" },
                { "sNeedOneSkill", "Need one skill" },
                { "sNeedTwoSkills", "Need two skills" },
                { "sEasy", "Easy" },
                { "sHard", "Hard" },
                { "sDeleteNote", "Delete Note" },
                { "sEditNote", "Edit Note" },
                { "sAdmireSuccess", "Admire Success" },
                { "sAdmireFail", "Admire Fail" },
                { "sIntimidateSuccess", "Intimidate Success" },
                { "sIntimidateFail", "Intimidate Fail" },
                { "sTauntSuccess", "Taunt Success" },
                { "sTauntFail", "Taunt Fail" },
                { "sBribeSuccess", "Bribe Success" },
                { "sBribeFail", "Bribe Fail" },
                { "sMaxSale", "Max Sale" },
                { "sAnd", "and" },
            };
            const std::pair<std::string_view, double> numbers[] = {
                { "fDifficultyMult", 5 },
                { "fNPCHealthBarTime", 5 },
                { "fNPCHealthBarFade", 1 },
                { "fFleeDistance", 3000 },
                { "fWereWolfRunMult", 1.3 },
                { "fWereWolfSilverWeaponDamageMult", 2 },
                { "iWerewolfFightMod", 100 },
                { "iWereWolfFleeMod", 100 },
                { "iWereWolfLevelToAttack", 20 },
                { "iWereWolfBounty", 1000 },
                { "fCombatDistanceWerewolfMod", 0.3 },
            };
            for (const auto& [key, value] : strings)
                if (gmsts.search(ESM::RefId::stringRefId(key)) == nullptr)
                    setString(gmsts, key, value);
            for (const auto& [key, value] : numbers)
                if (gmsts.search(ESM::RefId::stringRefId(key)) == nullptr)
                    setNumber(gmsts, key, value);

            for (const auto& [key, value] : Fallback::Map::getIntFallbackMap())
                setNumber(gmsts, key, value);
            for (const auto& [key, value] : Fallback::Map::getFloatFallbackMap())
                setNumber(gmsts, key, value);
            for (const auto& [key, value] : Fallback::Map::getNonNumericFallbackMap())
                setString(gmsts, key, value);
        }

        void generateDefaultStatics(MWWorld::ESMStore& store)
        {
            const std::pair<std::string_view, std::string_view> defaults[] = {
                { "divinemarker", "meshes/marker_divine.nif" },
                { "doormarker", "meshes/marker_arrow.nif" },
                { "northmarker", "meshes/marker_north.nif" },
                { "templemarker", "meshes/marker_temple.nif" },
                { "travelmarker", "meshes/marker_travel.nif" },
            };
            auto& statics = store.getWritable<ESM::Static>();
            for (const auto& [id, model] : defaults)
            {
                const ESM::RefId refId = ESM::RefId::stringRefId(id);
                if (statics.search(refId) != nullptr)
                    continue;
                ESM::Static record;
                record.blank();
                record.mId = refId;
                record.mModel = model;
                statics.insertStatic(record);
            }
        }

        void setMagicEffectNames(MWWorld::ESMStore& store)
        {
            const GmstStore& gmsts = store.get<ESM::GameSetting>();
            auto& effects = store.getWritable<ESM::MagicEffect>();
            for (int i = 0; i < ESM::MagicEffect::Length; ++i)
            {
                const ESM::RefId id = ESM::MagicEffect::indexToRefId(i);
                const ESM::MagicEffect* found = effects.search(id);
                if (found == nullptr)
                    continue;
                ESM::MagicEffect effect = *found;
                effect.mName = getString(gmsts, ESM::MagicEffect::refIdToGmstString(id));
                effects.insertStatic(effect);
            }
        }
    }

    void applyEsmFallbacks(MWWorld::ESMStore& store)
    {
        generateAttributes(store);
        setSkillProperties(store);
        generateDefaultDoors(store);
        generateDefaultGMSTs(store);
        generateDefaultStatics(store);
        setMagicEffectNames(store);
    }
}
