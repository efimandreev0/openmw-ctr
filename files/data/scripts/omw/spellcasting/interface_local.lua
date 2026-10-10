local auxUtil = require('openmw_aux.util')
local core = require('openmw.core')
local I = require('openmw.interfaces')



local applyMagicEffectsHandlers = {}

return {
    interfaceName = 'SpellCasting',
    interface = {
        version = 0,

        applyMagicEffects = function(options)
            auxUtil.callEventHandlers(applyMagicEffectsHandlers, options)
        end,

        addApplyMagicEffectsHandler = function(handler)
            applyMagicEffectsHandlers[#applyMagicEffectsHandlers+1] = handler
        end,

        explodeSpell = function(spellCast, options)
            core.sendGlobalEvent('ExplodeSpell', {spellCast = spellCast, options = options})
        end,

        inflict = function(spellCast, target, range)
        end,
    },

    eventHandlers = {
        ApplyMagicEffects = function(options) I.SpellCasting.applyMagicEffects(options) end,
    },
}
