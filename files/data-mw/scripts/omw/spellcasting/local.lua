local self = require('openmw.self')
local types = require('openmw.types')
local I = require('openmw.interfaces')
local auxUtil = require('openmw_aux.util')
local common = require('scripts.omw.spellcasting.common')
local Actor = types.Actor

local interface = auxUtil.shallowCopy(I.SpellCasting)
interface.inflict = common.inflict

I.SpellCasting.addApplyMagicEffectsHandler(function(options)
    if Actor.isDeathFinished(self) then return end
    local id = Actor.activeSpells(self):add(options)
    if id then
        self:sendEvent('PlayOnHitEffects', {activeSpellId = id})
    end
end)

local function onPlayOnHitEffects(options)
    local aSpell = Actor.activeSpells(self):getByActiveSpellId(options.activeSpellId)
    if aSpell then
        common.playMagicEffects(self, 'hit', aSpell.effects, true)
    end
end

return {
    interfaceName = 'SpellCasting',
    interface = interface,

    eventHandlers = {
        PlayOnHitEffects = onPlayOnHitEffects,
    },
}
