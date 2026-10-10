-- Contains timers and coroutine utilities. All functions require
-- the package itself as a first argument.
-- @module async
-- @usage local async = require('openmw.async')



-- Register a function as a timer callback.
-- @function [parent=#async] registerTimerCallback
-- @param #string name
-- @return #TimerCallback

-- Calls callback(arg) in `delay` simulation seconds.
-- @function [parent=#async] newSimulationTimer
-- @param #number delay

-- Calls callback(arg) in `delay` game seconds.
-- @function [parent=#async] newGameTimer
-- @param #number delay


-- Calls `func()` in `delay` simulation seconds.
-- @function [parent=#async] newUnsavableSimulationTimer
-- @param #number delay

-- Calls `func()` in `delay` game seconds.
-- @function [parent=#async] newUnsavableGameTimer
-- @param #number delay


-- Wraps a Lua function with a `Callback` object that can be used in async API calls.
-- @function [parent=#async] callback
-- @return #Callback

return nil

