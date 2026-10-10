local auxUtil = require('openmw_aux.util')
local core = require('openmw.core')
local I = require('openmw.interfaces')
local types = {
    Magic = 'Magic',
    Weapon = 'Weapon',
}

local onProjectileHitHandlers = {}



return {
    engineHandlers = {
        _onProjectileHit = function(projectile, hitResult)
            assert(projectile.type)
            local handlers = onProjectileHitHandlers[projectile.type]
            if handlers then
                auxUtil.callEventHandlers(handlers, projectile, hitResult)
            end
        end,
    },

    interfaceName = 'Projectiles',
    interface = {
        version = 1,

        addOnProjectileHitHandler = function(type, handler)
            assert(type)
            onProjectileHitHandlers[type] = onProjectileHitHandlers[type] or {}
            onProjectileHitHandlers[type][#onProjectileHitHandlers[type]+1] = handler
        end,
        TYPES = types,
    },
}
