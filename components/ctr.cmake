# Component sources for the Nintendo 3DS native build (included from
# components/CMakeLists.txt when CTR is set). Everything that only exists to
# feed OpenSceneGraph/OpenGL, Lua, ICU, Recast/SQLite or Qt is left out; the
# few pieces of those areas the game logic still needs have 3DS replacements
# under components/ctr.

configure_resource_file(${VERSION_RESOURCE_FILE_IN} ${OpenMW_BINARY_DIR} ${VERSION_RESOURCE_FILE_RELATIVE})
configure_file("${OpenMW_SOURCE_DIR}/${VERSION_CPP_FILE}.in" "${OpenMW_BINARY_DIR}/${VERSION_CPP_FILE}")
list(APPEND COMPONENT_FILES "${OpenMW_BINARY_DIR}/${VERSION_CPP_FILE}")

add_component_dir (l10n
    messagebundles manager
    )

add_component_dir (settings
    categories
    gyroscopeaxis
    hrtfmode
    navmeshrendermode
    parser
    sanitizer
    sanitizerimpl
    settings
    settingvalue
    shadermanager
    values
    windowmode
    )
file(GLOB _ctr_settings_categories "settings/categories/*.cpp")
list(APPEND COMPONENT_FILES ${_ctr_settings_categories})

add_component_dir (bsa
    bsafile compressedbsafile memorystream
    )

add_component_dir (bullethelpers
    collisionobject heightfield operators processtrianglecallback transformboundingbox
    )

add_component_dir (vfs
    manager archive bsaarchive filesystemarchive pathutil registerarchives
    )

add_component_dir (resource
    bulletshapemanager bulletshape niffilemanager objectcache resourcesystem resourcemanager
    cachestats animblendrulesmanager
    )

add_component_dir (sceneutil
    positionattitudetransform workqueue unrefqueue textkeymap animblendrules lightcommon
    )

add_component_dir (nif
    base controller data effect extra niffile nifkey nifstream niftypes node parent particle physics property record
    recordptr texture
    )

add_component_dir (nifbullet
    bulletnifloader
    )

add_component_dir (toutf8
    tablesgen toutf8
    )

add_component_dir (esm attr common defs esmcommon records util luascripts format refid esmbridge esmterrain
    formid fourcc stringrefid generatedrefid indexrefid serializerefid esm3exteriorcellrefid quaternion vector3
    exteriorcelllocation
    )

add_component_dir (esm3
    ctrstrpool ctrinfoblob esmreader esmwriter loadacti loadalch loadappa loadarmo loadbody loadbook loadbsgn loadcell
    loadclas loadclot loadcont loadcrea loaddial loaddoor loadench loadfact loadglob loadgmst
    loadinfo loadingr loadland loadlevlist loadligh loadlock loadprob loadrepa loadltex loadmgef loadmisc
    loadnpc loadpgrd loadrace loadregn loadscpt loadskil loadsndg loadsoun loadspel loadsscr loadstat
    loadweap aipackage effectlist spelllist variant variantimp loadtes3 cellref filter
    savedgame journalentry queststate locals globalscript player objectstate cellid cellstate globalmap
    inventorystate containerstate npcstate creaturestate dialoguestate statstate npcstats creaturestats
    weatherstate quickkeys fogstate spellstate activespells creaturelevliststate doorstate projectilestate debugprofile
    aisequence magiceffects custommarkerstate stolenitems transport animationstate controlsstate mappings readerscache
    infoorder timestamp formatversion landrecorddata selectiongroup dialoguecondition
    refnum actoridconverter
    )

add_component_dir (esmterrain
    gridsampling landobject
    )

file(GLOB _ctr_esm4_sources "esm4/*.cpp")
list(APPEND COMPONENT_FILES ${_ctr_esm4_sources})

add_component_dir (misc
    barrier budgetmeasurement color compression constants convert coordinateconverter display endianness float16
    frameratelimiter guarded math mathutil messageformatparser notnullptr objectpool progressreporter resourcehelpers
    rng strongtypedef thread timeconvert timer tuplehelpers tuplemeta utf8stream weakcache windows
    )

add_component_dir (misc/strings
    algorithm conversion format lower
    )

add_component_dir (debug
    debugging debuglog writeflags
    )

add_component_dir (files
    linuxpath fixedpath multidircollection collections configurationmanager
    constrainedfilestream memorystream hash configfileparser openfile constrainedfilestreambuf conversion
    istreamptr streamwithbuffer utils scancache vitapath
    )
set_source_files_properties(files/configfileparser.cpp PROPERTIES COMPILE_FLAGS -Wno-shadow)

add_component_dir (compiler
    context controlparser errorhandler exception exprparser extensions fileparser generator
    lineparser literals locals output parser scanner scriptparser skipparser streamerrorhandler
    stringparser tokenloc nullerrorhandler opcodes extensions0 declarationparser
    quickfileparser discardparser junkparser
    )

add_component_dir (interpreter
    context controlopcodes genericopcodes installopcodes interpreter localopcodes mathopcodes
    miscopcodes opcodes program runtime types defines
    )

add_component_dir (translation
    translation
    )

add_component_dir (loadinglistener
    loadinglistener reporter
    )

add_component_dir (myguiplatform
    myguidatamanager myguiloglistener additivelayer scalinglayer
    )
add_component_dir (myguiplatform/ctr
    myguiplatform myguirendermanager myguitexture
    )

add_component_dir (widgets
    box fontwrapper imagebutton tags list numericeditbox sharedstatebutton windowcaption widgets
    )

add_component_dir (fontloader
    fontloader
    )

add_component_dir (sdlutil
    events sdlinputwrapper sdlmappings
    )

add_component_dir (version
    version
    )

add_component_dir (fallback
    fallback validate
    )

add_component_dir (detournavigator
    agentbounds areatype bounds collisionshapetype flags navigatorstub objectid objecttransform recastmeshtiles
    settings status tileposition version
    )

add_component_dir (serialization
    binaryreader binarywriter sizeaccumulator
    )

add_component_dir (esmloader
    lessbyid load esmdata
    )

add_component_dir (platform
    platform file filestdio
    )

# 3DS replacements: SDL shim (libctru HID), ICU-free l10n backend, citro3d
# MyGUI backend and platform glue live under components/ctr.
file(GLOB_RECURSE _ctr_platform_sources "ctr/*.cpp" "ctr/*.c")
list(APPEND COMPONENT_FILES ${_ctr_platform_sources})

include(${OpenMW_SOURCE_DIR}/cmake/CtrEmbed.cmake)
file(GLOB _ctr_shaders "${CMAKE_CURRENT_SOURCE_DIR}/ctr/shaders/*.v.pica")
ctr_add_shaders(COMPONENT_FILES ${_ctr_shaders})

add_library(components STATIC ${COMPONENT_FILES})

target_link_libraries(components
    boost_program_options
    MyGUIEngineStatic
    freetype
    png16
    bz2
    yaml-cpp
    lz4
    z
    smhasher
    Base64
    # Full paths: devkitPro's portlibs carry an older Bullet with the same library names.
    ${CTR_DEPS_PREFIX}/lib/libBulletCollision.a
    ${CTR_DEPS_PREFIX}/lib/libLinearMath.a
    osglite
    citro3d
    ctru
    )

target_compile_definitions(components PUBLIC MYGUI_DONT_USE_OBSOLETE=ON)

set(COMPONENT_FILES ${COMPONENT_FILES} PARENT_SCOPE)
