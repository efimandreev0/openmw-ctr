local core = require('openmw.core')
local types = require('openmw.types')
local auxUtil = require('openmw_aux.util')
local Actor = types.Actor
local Armor = types.Armor
local Book = types.Book
local Clothing = types.Clothing
local Container = types.Container
local Ingredient = types.Ingredient
local Lockable = types.Lockable
local Player = types.Player
local Potion = types.Potion
local Weapon = types.Weapon
local l10n = core.l10n('Mechanics')

local common = {}

common.UnitsPerFoot = 22

function common.getMagicRecord(id)
    local record = core.magic.spells.records[id]
    if record then
        return record, core.magic.spells
    end
    record = core.magic.enchantments.records[id]
    if record then
        return record, core.magic.enchantments
    end

    for _, enchantable in ipairs({ Armor, Weapon, Clothing, Book }) do
        record = enchantable.records[id]
        if record and record.enchant then
            return core.magic.enchantments.records[record.enchant], core.magic.enchantments
        end
    end

    record = Potion.records[id]
    if record then
        return record, Potion
    end
    record = Ingredient.records[id]
    if record then
        return record, Ingredient
    end

    print('Warning: No such magic item: '..tostring(id))

end

function common.getMagicEffectSound(mgef, type)
    local sound = mgef[type..'Sound']
    if not sound and mgef.school then
        local skill = core.stats.Skill.records[mgef.school]
        if skill and skill.school then
            return skill.school[type..'Sound']
        end
    end
    return sound
end

function common.playMagicEffectVfx(mgef, staticId, target, allowEffectLoop)
    local static = types.Static.records[staticId]
    if static and static.model then
        local eventParams = {
            model = static.model,
            options = {
                vfxId = mgef.id,
                particleTextureOverride = mgef.particle,
                loop = mgef.continuousVfx and allowEffectLoop,
            }
        }
        if Actor.objectIsInstance(target) then
            target:sendEvent('AddVfx', eventParams)
        else
            eventParams.position = target.position
            core.sendGlobalEvent('SpawnVfx', eventParams)
        end
    end
end

function common.playMagicEffects(target, type, effects, allowEffectLoop)
    local addedStatic = {}
    local addedSounds = {}
    for _, effect in pairs(effects) do
        local mgef = core.magic.effects.records[effect.id]

        local sound = common.getMagicEffectSound(mgef, type)
        if sound and not addedSounds[sound] then
            addedSounds[sound] = true
            core.sendGlobalEvent('PlaySound3d', {sound = sound, position = target})
        end

        local staticId = mgef[type..'Static'] or ('vfx_default'..type)
        if not addedStatic[staticId] then
            addedStatic[staticId] = true
            common.playMagicEffectVfx(mgef, staticId, target, allowEffectLoop)
        end

        if not Actor.objectIsInstance(target) then
            target:sendEvent('AddGlow', {
                color = mgef.color,
                duration = 1.5
            })
        end
    end
end

function common.isOrganicContainer(target)
    if Container.objectIsInstance(target) then
        local record = Container.records[target.recordId]
        return record and record.isOrganic
    end
    return false
end

function common.targetIsValid(target)
    if not target then return false end
    local isValidActor = Actor.objectIsInstance(target) and Actor.isInActorsProcessingRange(target)
    return isValidActor or (Lockable.objectIsInstance(target) and not common.isOrganicContainer(target))
end


local lockableEffects = {
    [core.magic.EFFECT_TYPE.Lock] = true,
    [core.magic.EFFECT_TYPE.Open] = true,
}

function common.isLockableEffect(effectId)
    return lockableEffects[effectId] == true
end

function common.filterApplicableEffects(indexes, effects, target)
    local applicableIndexes = {}
    local isLockable = types.Lockable.objectIsInstance(target)

    for _, index in ipairs(indexes) do
        local effect = effects[index + 1]

        if common.isLockableEffect(effect.id) == isLockable then
            applicableIndexes[#applicableIndexes+1] = index
        end
    end
    return applicableIndexes
end


function common.findByIndex(effects, index)
    if not effects then return nil end
    for _, effect in pairs(effects) do
        if effect.index == index then return effect end
    end
    return nil
end

function common.filterByIndex(effects, filter)
    if not filter or #filter == 0 then return effects end
    local effectsByIndex = {}
    for _, index in pairs(filter) do
        local effect = common.findByIndex(effects, index)
        if effect then
            effectsByIndex[#effectsByIndex + 1] = effect
        else
            print('Warning: Spell has no such effect [index='..tostring(index)..']')
        end
    end
    return effectsByIndex
end

function common.filterByRange(effects, range)
    if not range or not effects then return effects end

    local ret = {}
    for _, effect in pairs(effects) do
        if effect.range == range then
            ret[#ret+ 1] = effect
        end
    end
    return ret
end

function common.inflict(spellCast, target, range)
    if not common.targetIsValid(target) then
        return
    end

    local record, type = common.getMagicRecord(spellCast.id)
    local effectsWithParams = common.filterByRange(record.effects, range)
    if #effectsWithParams == 0 then
        return
    end
    if type == Ingredient then
        effectsWithParams = {
            effectsWithParams[1]
        }
    end

    local targetIsActor = Actor.objectIsInstance(target)
    local casterIsActor = spellCast.caster and Actor.objectIsInstance(spellCast.caster)

    local effects = {}
    local recastable = false
    for _, enam in pairs(effectsWithParams) do
        recastable = recastable or not enam.effect.nonRecastable
        if casterIsActor or not enam.effect.casterLinked then
            effects[#effects+1] = enam.index
        end
    end
    if targetIsActor and not recastable and Actor.activeSpells(target):isSpellActive(spellCast.id) then
        if casterIsActor and Player.objectIsInstance(spellCast.caster) then
            spellCast.caster:sendEvent('ShowMessage', { message = l10n'MagicCannotRecast' })
        end
        return
    end
    effects = common.filterApplicableEffects(effects, record.effects, target)
    if #effects == 0 then
        return
    end

    local options = auxUtil.shallowCopy(spellCast)
    options.effects = effects
    target:sendEvent('ApplyMagicEffects', options)
end

function common.hasAoe(effects, range)
    for _, effect in pairs(common.filterByRange(effects, range)) do
        if effect.area > 0 then
            return true
        end
    end
    return false
end

return common
