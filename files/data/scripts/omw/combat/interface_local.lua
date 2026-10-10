local I = require('openmw.interfaces')
local util = require('openmw.util')
local auxUtil = require('openmw_aux.util')

local onHitHandlers = {}

-- Table of possible attack source types
-- @type AttackSourceType
-- @field #string Magic
-- @field #string Melee
-- @field #string Ranged
-- @field #string Unspecified


-- @type AttackInfo
-- @field [parent=#AttackInfo] #table damage A table mapping a stat name (health, fatigue, or magicka) to a number. For example, {health = 50, fatigue = 10} will cause 50 damage to health and 10 to fatigue (before adjusting for armor and difficulty). This field is ignored for failed attacks.
-- @field [parent=#AttackInfo] #number strength A number between 0 and 1 representing the attack strength. This field is ignored for failed attacks.
-- @field [parent=#AttackInfo] #boolean successful Whether the attack was successful or not.
-- @field [parent=#AttackInfo] #AttackSourceType sourceType What class of attack this is.
-- @field [parent=#AttackInfo] openmw.types#Actor attacker (Optional) Attacking actor
-- @field [parent=#AttackInfo] openmw.types#Weapon weapon (Optional) Attacking weapon
-- @field [parent=#AttackInfo] #string ammo (Optional) Ammo record ID
-- @field [parent=#AttackInfo] openmw.util#Vector3 hitPos (Optional) Where on the victim the attack is landing. Used to spawn blood effects. Blood effects are skipped if nil.
return {
    --- Basic combat interface
    -- @module Combat
    -- @usage local I = require('openmw.interfaces')
    --
    --I.Combat.addOnHitHandler(function(attack)
    --    -- Adds fatigue loss when hit by draining fatigue when taking health damage
    --    if attack.damage.health and not attack.damage.fatigue then
    --        local strengthFactor = Actor.stats.attributes.strength(self).modified / 100 * 0.66
    --        local enduranceFactor = Actor.stats.attributes.endurance(self).modified / 100 * 0.34
    --        local factor = 1 - math.min(strengthFactor + enduranceFactor, 1)
    --        if factor > 0 then
    --            attack.damage.fatigue = attack.damage.health * factor
    --        end
    --    end
    --end)

    interfaceName = 'Combat',
    interface = {
        --- Interface version
        -- @field [parent=#Combat] #number version
        version = 4,

        -- If `handler(attack)` returns false, other handlers for
        -- the call will be skipped. Where attack is the same @{#AttackInfo} passed to #Combat.onHit
        -- @function [parent=#Combat] addOnHitHandler
        -- @param #function handler The handler.
        addOnHitHandler = function(handler)
            onHitHandlers[#onHitHandlers + 1] = handler
        end,

        -- Note that this function only adjusts the number, use #Combat.applyArmor
        -- to include other side effects.
        -- @function [parent=#Combat] adjustDamageForArmor
        -- @param #number Damage The numeric damage to adjust
        -- @return #number Damage adjusted for armor
        adjustDamageForArmor = function(damage, actor) return damage end,

        -- and adjusts damage accordingly. Has no effect if both this actor and the
        -- attacker are NPCs, or if both are Players.
        -- @function [parent=#Combat] adjustDamageForDifficulty
        -- @param #Attack attack The attack to adjust
        adjustDamageForDifficulty = function(attack, defendant) end,

        -- condition accordingly, progresses armor skill, and plays the armor appropriate
        -- hit sound.
        -- @function [parent=#Combat] applyArmor
        applyArmor = function(attack) end,

        applyStagger = function(attack, rawHealthDamage) end,

        -- Note that this interface function is read by the engine to update the UI.
        -- This function can still be overridden same as any other interface, but must not call any functions or interfaces that modify anything.
        -- @function [parent=#Combat] getArmorRating
        -- @return #number
        getArmorRating = function(actor) return 0 end,

        -- You can override this to return any skill you wish (including non-armor skills, if you so wish).
        -- Note that this interface function is read by the engine to update the UI.
        -- This function can still be overridden same as any other interface, but must not call any functions or interfaces that modify anything.
        -- @function [parent=#Combat] getArmorSkill
        -- @return #string The armor skill identifier, or unarmored if the item was nil or not an instance of @{openmw.types#Armor}. Can return nil if unimplemented.
        getArmorSkill = function(itemOrId) return nil end,

        -- Note that this interface function is read by the engine to update the UI.
        -- This function can still be overridden same as any other interface, but must not call any functions or interfaces that modify anything.
        -- @function [parent=#Combat] getSkillAdjustedArmorRating
        -- @return #number
        getSkillAdjustedArmorRating = function(itemOrId, actor) return 0 end,

        -- @function [parent=#Combat] getEffectiveArmorRating
        -- @param openmw.core#GameObject item The item
        -- @return #number
        getEffectiveArmorRating = function(item, actor) return 0 end,

        -- @function [parent=#Combat] spawnBloodEffect
        -- @param openmw.util#Vector3 position
        spawnBloodEffect = function(position) end,

        -- @function [parent=#Combat] onHit
        -- @param #AttackInfo attackInfo
        onHit = function(attackInfo) auxUtil.callEventHandlers(onHitHandlers, attackInfo) end,

        -- Used to pick which armor to damage / skill to increase when hit during combat.
        -- @function [parent=#Combat] pickRandomArmor
        -- @return openmw.core#GameObject The armor equipped in the chosen slot. nil if nothing was equipped in that slot.
        pickRandomArmor = function(actor) return nil end,

        --- @{#AttackSourceType}
        -- @field [parent=#Combat] #AttackSourceType ATTACK_SOURCE_TYPES Available attack source types
        ATTACK_SOURCE_TYPES = util.makeStrictReadOnly({
            Magic = 'magic',
            Melee = 'melee',
            Ranged = 'ranged',
            Unspecified = 'unspecified',
        }),

        ATTACK_TYPES = util.makeStrictReadOnly({
            Chop = 0,
            Slash = 1,
            Thrust = 2,
        }),
    },

    eventHandlers = {
        Hit = function(data) I.Combat.onHit(data) end,
    },
}
