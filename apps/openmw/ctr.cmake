# 3DS native build of the openmw executable. Game logic is shared with the desktop build; rendering (mwrender/ctr),
# Lua (nolua), sound output (mwsound NDSP backend) and the platform layer (ctr/) are 3DS-specific.

add_openmw_dir (mwinput
    actions actionmanager bindingsmanager controllermanager controlswitch
    inputmanagerimp mousemanager keyboardmanager sensormanager gyromanager
    )

add_openmw_dir (mwgui
    layout textinput widgets race class birth review windowmanagerimp console dialogue
    windowbase statswindow messagebox journalwindow charactercreation
    mapwindow windowpinnablebase tooltips scrollwindow bookwindow resourceskin
    formatting inventorywindow container hud countdialog tradewindow settingswindow
    confirmationdialog alchemywindow referenceinterface spellwindow mainmenu quickkeysmenu
    itemselection spellbuyingwindow loadingscreen levelupdialog waitdialog spellcreationdialog
    enchantingdialog trainingwindow travelwindow exposedwindow cursor spellicons
    merchantrepair repair soulgemdialog companionwindow bookpage journalviewmodel journalbooks
    itemmodel containeritemmodel inventoryitemmodel sortfilteritemmodel itemview
    tradeitemmodel companionitemmodel pickpocketitemmodel controllers savegamedialog
    recharge mode backgroundimage itemwidget screenfader debugwindow spellmodel spellview
    draganddrop timeadvancer jailscreen itemchargeview keyboardnavigation textcolours statswatcher
    settings worlditemmodel itemtransfer controllerbuttonsoverlay inventorytabsoverlay
    )

add_openmw_dir (mwdialogue
    dialoguemanagerimp journalimp journalentry quest topic filter selectwrapper keywordsearch scripttest
    )

add_openmw_dir (mwscript
    locals scriptmanagerimp compilercontext interpretercontext cellextensions miscextensions
    guiextensions soundextensions skyextensions statsextensions containerextensions
    aiextensions controlextensions extensions globalscripts ref dialogueextensions
    animationextensions transformationextensions consoleextensions userextensions
    )

add_openmw_dir (mwsound
    soundmanagerimp nsadecoder sound soundbuffer sounddecoder soundoutput
    loudness regionsoundselector watersoundupdater headcache warmqueue ndspoutput ctrdecoders adpcmpack
    )

add_openmw_dir (mwworld
    refdata worldimp scenectr globals class action nullaction actionteleport
    containerstore actiontalk actiontake manualref player cellvisitors failedaction
    worldmodel localscripts customdata inventorystore ptr actionopen actionread actionharvest
    actionequip timestamp actionalchemy cellstore actionapply actioneat
    store esmstore fallback actionrepair actionsoulgem livecellref actiondoor
    contentloader esmloader actiontrap cellreflist cellref weather projectilemanager
    cellpreloader datetimemanager groundcoverstore magiceffects cell ptrregistry
    positioncellgrid
    )

add_openmw_dir (mwphysics
    physicssystem trace collisiontype actor convert object heightfield closestnotmerayresultcallback
    contacttestresultcallback stepper movementsolver projectile
    actorconvexcallback raycasting mtphysics contacttestwrapper projectileconvexcallback
    )

add_openmw_dir (mwclass
    classes activator creature npc weapon armor potion apparatus book clothing container door
    ingredient creaturelevlist itemlevlist light lockpick misc probe repair static actor bodypart
    esm4base esm4npc light4
    )

add_openmw_dir (mwmechanics
    mechanicsmanagerimp stat creaturestats magiceffects movement actorutil spelllist
    drawstate spells activespells npcstats aipackage aisequence aipursue alchemy aiwander aitravel aifollow aiavoiddoor aibreathe
    aicast aiescort aiface aiactivate aicombat recharge repair enchanting pathfinding pathgrid security spellcasting spellresistance
    disease pickpocket levelledlist combat steering obstacle autocalcspell difficultyscaling aicombataction summoning
    character actors objects aistate weaponpriority spellpriority weapontype spellutil
    spelleffects
    )

add_openmw_dir (mwstate
    statemanagerimp charactermanager character quicksavemanager
    )

add_openmw_dir (mwrender
    landmanager fogmanager actorutil
    )

add_openmw_dir (mwbase
    environment world scriptmanager dialoguemanager journal soundmanager mechanicsmanager
    inputmanager windowmanager statemanager luamanager
    )

file(GLOB _ctr_render_sources "${CMAKE_CURRENT_SOURCE_DIR}/mwrender/ctr/*.cpp")
file(GLOB _ctr_nolua_sources "${CMAKE_CURRENT_SOURCE_DIR}/nolua/*.cpp")
file(GLOB _ctr_platform_sources "${CMAKE_CURRENT_SOURCE_DIR}/ctr/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/ctr/*.c")

add_library(openmw-lib STATIC
    ${OPENMW_FILES}
    ${_ctr_render_sources}
    ${_ctr_nolua_sources}
    )
target_link_libraries(openmw-lib oics components)

set(_ctr_shaders)
file(GLOB _ctr_render_shaders "${CMAKE_CURRENT_SOURCE_DIR}/mwrender/ctr/shaders/*.v.pica")
if (_ctr_render_shaders)
    include(${OpenMW_SOURCE_DIR}/cmake/CtrEmbed.cmake)
    ctr_add_shaders(_ctr_shaders ${_ctr_render_shaders})
    target_sources(openmw-lib PRIVATE ${_ctr_shaders})
endif()

add_executable(openmw
    main.cpp
    enginectr.cpp
    options.cpp
    ${_ctr_platform_sources}
    )
set_target_properties(openmw PROPERTIES SUFFIX ".elf")
target_link_libraries(openmw openmw-lib
    -Wl,--start-group
    components
    oics
    citro3d
    mpg123
    ctru
    m
    -Wl,--end-group
    )
target_link_options(openmw PRIVATE -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/openmw.map)
