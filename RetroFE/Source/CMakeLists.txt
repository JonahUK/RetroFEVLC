cmake_minimum_required(VERSION 3.16...4.4)
# Tested with CMake 4.4 / Visual Studio 2022; policy baseline remains 3.16.

project (retrofe)

set(CMAKE_FIND_FRAMEWORK FIRST)
set(RETROFE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/..)
set(RETROFE_THIRD_PARTY_DIR "${RETROFE_DIR}/ThirdParty")
list(APPEND CMAKE_MODULE_PATH "${RETROFE_DIR}/CMake")

##############################################################
# Setup some variables to help find external libraries
##############################################################

set(SQLITE3_ROOT "${RETROFE_THIRD_PARTY_DIR}/sqlite3")
set(RAPIDXML_ROOT "${RETROFE_THIRD_PARTY_DIR}/rapidxml-1.13")

if(WIN32)
if(MSVC)
	set(SDL2_ROOT "${RETROFE_THIRD_PARTY_DIR}/SDL2-2.0.14")
	set(SDL2_IMAGE_ROOT "${RETROFE_THIRD_PARTY_DIR}/SDL2_image-2.0.5")
	set(SDL2_MIXER_ROOT "${RETROFE_THIRD_PARTY_DIR}/SDL2_mixer-2.0.4")
	set(SDL2_TTF_ROOT "${RETROFE_THIRD_PARTY_DIR}/SDL2_ttf-2.0.15")
endif()


  
if(MSVC)  
	set(DIRENT_INCLUDE_DIR "${RETROFE_THIRD_PARTY_DIR}/dirent-1.20.1/include")
endif()
endif()

if(WIN32 AND MSVC AND CMAKE_SIZEOF_VOID_P EQUAL 8)
    # The bundled FindSDL modules may otherwise select Win32 libraries.
    # Set only missing cache values; existing explicit -D overrides win.
    set(_sdl_lib_root "${RETROFE_THIRD_PARTY_DIR}")
    foreach(_pair
        "SDL2MAIN_LIBRARY|SDL2-2.0.14/lib/x64/SDL2main.lib"
        "SDL2_LIBRARY|SDL2-2.0.14/lib/x64/SDL2.lib"
        "SDL2_IMAGE_LIBRARY|SDL2_image-2.0.5/lib/x64/SDL2_image.lib"
        "SDL2_MIXER_LIBRARY|SDL2_mixer-2.0.4/lib/x64/SDL2_mixer.lib"
        "SDL2_TTF_LIBRARY|SDL2_ttf-2.0.15/lib/x64/SDL2_ttf.lib"
        "ZLIB_LIBRARY_RELEASE|zlib-x64/lib/zlibstatic.lib")
        string(REPLACE "|" ";" _parts "${_pair}")
        list(GET _parts 0 _key)
        list(GET _parts 1 _rel)
        if(NOT DEFINED ${_key} OR "${${_key}}" MATCHES "-NOTFOUND$")
            if(EXISTS "${_sdl_lib_root}/${_rel}")
                set(${_key} "${_sdl_lib_root}/${_rel}" CACHE FILEPATH "Bundled MSVC x64 library")
            endif()
        endif()
    endforeach()
    if(NOT DEFINED ZLIB_LIBRARY_DEBUG)
        set(ZLIB_LIBRARY_DEBUG "ZLIB_LIBRARY_DEBUG-NOTFOUND" CACHE FILEPATH "Optional debug zlib")
    endif()
endif()

if(WIN32)
	find_package(SDL2 REQUIRED)
	find_package(SDL2_image REQUIRED)
	find_package(SDL2_mixer REQUIRED)
	find_package(SDL2_ttf REQUIRED)
    if(MSVC AND CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(ZLIB_INCLUDE_DIR "${RETROFE_THIRD_PARTY_DIR}/zlib-x64/include")
        set(ZLIB_LIBRARY_RELEASE "${RETROFE_THIRD_PARTY_DIR}/zlib-x64/lib/zlib.lib")
    endif()
    find_package(ZLIB REQUIRED)
else()
	include(FindPkgConfig)
	pkg_search_module(SDL2 REQUIRED sdl2)
	pkg_search_module(SDL2_IMAGE REQUIRED SDL2_image)
	pkg_search_module(SDL2_MIXER REQUIRED SDL2_mixer)
	pkg_search_module(SDL2_TTF REQUIRED SDL2_ttf)
	pkg_search_module(ZLIB REQUIRED zlib)
	find_package(Threads REQUIRED)

if(APPLE)
	find_package(SDL2 REQUIRED)
endif()
endif()


# LibVLC 3.x Windows SDK/runtime, kept under RetroFE/ThirdParty/VLC.
# VLC_ROOT can be overridden for custom layouts, but no absolute path is required.
if(WIN32)
    set(VLC_ROOT "${RETROFE_THIRD_PARTY_DIR}/VLC" CACHE PATH "LibVLC 3.x root (SDK and runtime)")
    find_path(LIBVLC_INCLUDE_DIR NAMES vlc/vlc.h
        PATHS "${VLC_ROOT}/sdk/include" "${VLC_ROOT}/include" NO_DEFAULT_PATH)
    find_library(LIBVLC_LIBRARY NAMES libvlc vlc
        PATHS "${VLC_ROOT}/sdk/lib" "${VLC_ROOT}/lib" NO_DEFAULT_PATH)
    if(NOT LIBVLC_INCLUDE_DIR OR NOT LIBVLC_LIBRARY)
        message(FATAL_ERROR "LibVLC SDK missing: expected ThirdParty/VLC/sdk/include/vlc/vlc.h and ThirdParty/VLC/sdk/lib/libvlc.lib")
    endif()
    foreach(_vlc_file libvlc.dll libvlccore.dll)
        if(NOT EXISTS "${VLC_ROOT}/${_vlc_file}")
            message(FATAL_ERROR "LibVLC runtime missing: ${VLC_ROOT}/${_vlc_file}")
        endif()
    endforeach()
    if(NOT IS_DIRECTORY "${VLC_ROOT}/plugins")
        message(FATAL_ERROR "LibVLC plugins folder missing: ${VLC_ROOT}/plugins")
    endif()
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBVLC REQUIRED IMPORTED_TARGET libvlc)
endif()

set(RETROFE_INCLUDE_DIRS
    ${LIBVLC_INCLUDE_DIR}
	"${SDL2_INCLUDE_DIRS}"
	"${SDL2_IMAGE_INCLUDE_DIRS}"
	"${SDL2_MIXER_INCLUDE_DIRS}"
	"${SDL2_TTF_INCLUDE_DIRS}"
	"${ZLIB_INCLUDE_DIRS}"
	"${SQLITE3_ROOT}"
	"${RAPIDXML_ROOT}"
)

if(MSVC)
LIST(APPEND RETROFE_INCLUDE_DIRS 	"${DIRENT_INCLUDE_DIR}")
endif()

set(RETROFE_LIBRARIES
    ${LIBVLC_LIBRARY}
	${SDL2_LIBRARIES}
	${SDL2_IMAGE_LIBRARIES}
	${SDL2_MIXER_LIBRARIES}
	${SDL2_TTF_LIBRARIES}
	${ZLIB_LIBRARIES}
)

if(NOT WIN32)
	LIST(APPEND RETROFE_LIBRARIES ${CMAKE_DL_LIBS} ${CMAKE_THREAD_LIBS_INIT})
endif()

set(RETROFE_HEADERS
	"${RETROFE_DIR}/Source/Collection/CollectionInfo.h"
	"${RETROFE_DIR}/Source/Collection/CollectionInfoBuilder.h"
	"${RETROFE_DIR}/Source/Collection/Item.h"
	"${RETROFE_DIR}/Source/Collection/MenuParser.h"
	"${RETROFE_DIR}/Source/Control/UserInput.h"
	"${RETROFE_DIR}/Source/Control/InputHandler.h"
	"${RETROFE_DIR}/Source/Control/JoyAxisHandler.h"
	"${RETROFE_DIR}/Source/Control/JoyButtonHandler.h"
	"${RETROFE_DIR}/Source/Control/JoyHatHandler.h"
	"${RETROFE_DIR}/Source/Control/KeyboardHandler.h"
	"${RETROFE_DIR}/Source/Control/MouseButtonHandler.h"
	"${RETROFE_DIR}/Source/Database/Configuration.h"
	"${RETROFE_DIR}/Source/Database/DB.h"
	"${RETROFE_DIR}/Source/Database/MetadataDatabase.h"
	"${RETROFE_DIR}/Source/Execute/AttractMode.h"
	"${RETROFE_DIR}/Source/Execute/Launcher.h"
	"${RETROFE_DIR}/Source/Graphics/Animate/Tween.h"
	"${RETROFE_DIR}/Source/Graphics/Animate/TweenTypes.h"
	"${RETROFE_DIR}/Source/Graphics/Animate/TweenSet.h"
	"${RETROFE_DIR}/Source/Graphics/Animate/Animation.h"
	"${RETROFE_DIR}/Source/Graphics/Animate/AnimationEvents.h"
	"${RETROFE_DIR}/Source/Graphics/ComponentItemBinding.h"
	"${RETROFE_DIR}/Source/Graphics/Component/Container.h"
	"${RETROFE_DIR}/Source/Graphics/Component/Component.h"
	"${RETROFE_DIR}/Source/Graphics/Component/Image.h"
	"${RETROFE_DIR}/Source/Graphics/Component/ImageBuilder.h"
	"${RETROFE_DIR}/Source/Graphics/Component/ReloadableMedia.h"
	"${RETROFE_DIR}/Source/Graphics/Component/ReloadableText.h"
	"${RETROFE_DIR}/Source/Graphics/Component/ReloadableScrollingText.h"
	"${RETROFE_DIR}/Source/Graphics/Component/ScrollingList.h"
	"${RETROFE_DIR}/Source/Graphics/Component/Text.h"
	"${RETROFE_DIR}/Source/Graphics/Component/VideoComponent.h"
	"${RETROFE_DIR}/Source/Graphics/Component/VideoBuilder.h"
	"${RETROFE_DIR}/Source/Graphics/Component/Video.h"
	"${RETROFE_DIR}/Source/Graphics/Font.h"
	"${RETROFE_DIR}/Source/Graphics/FontCache.h"
	"${RETROFE_DIR}/Source/Graphics/SearchOverlay.h"
	"${RETROFE_DIR}/Source/Collection/SearchMatcher.h"
	"${RETROFE_DIR}/Source/Graphics/PageBuilder.h"
	"${RETROFE_DIR}/Source/Graphics/Page.h"
	"${RETROFE_DIR}/Source/Menu/Menu.h"
	"${RETROFE_DIR}/Source/Sound/Sound.h"
	"${RETROFE_DIR}/Source/Utility/Log.h"
	"${RETROFE_DIR}/Source/Utility/Utils.h"
	"${RETROFE_DIR}/Source/Video/IVideo.h"
	"${RETROFE_DIR}/Source/Video/LibVLCVideo.h"
	"${RETROFE_DIR}/Source/Video/VideoFactory.h"
	"${RETROFE_DIR}/Source/Graphics/ComponentItemBindingBuilder.h"
	"${RETROFE_DIR}/Source/Graphics/ViewInfo.h"
	"${RETROFE_DIR}/Source/RetroFE.h"
	"${RETROFE_DIR}/Source/SDL.h"
	"${RETROFE_DIR}/Source/Version.h"
)

set(RETROFE_SOURCES
	"${RETROFE_DIR}/Source/Collection/CollectionInfo.cpp"
	"${RETROFE_DIR}/Source/Collection/CollectionInfoBuilder.cpp"
	"${RETROFE_DIR}/Source/Collection/Item.cpp"
	"${RETROFE_DIR}/Source/Collection/MenuParser.cpp"
	"${RETROFE_DIR}/Source/Control/UserInput.cpp"
	"${RETROFE_DIR}/Source/Control/JoyAxisHandler.cpp"
	"${RETROFE_DIR}/Source/Control/JoyButtonHandler.cpp"
	"${RETROFE_DIR}/Source/Control/JoyHatHandler.cpp"
	"${RETROFE_DIR}/Source/Control/KeyboardHandler.cpp"
	"${RETROFE_DIR}/Source/Control/MouseButtonHandler.cpp"
	"${RETROFE_DIR}/Source/Database/Configuration.cpp"
	"${RETROFE_DIR}/Source/Database/DB.cpp"
	"${RETROFE_DIR}/Source/Database/MetadataDatabase.cpp"
	"${RETROFE_DIR}/Source/Execute/AttractMode.cpp"
	"${RETROFE_DIR}/Source/Execute/Launcher.cpp"
	"${RETROFE_DIR}/Source/Graphics/Font.cpp"
	"${RETROFE_DIR}/Source/Graphics/FontCache.cpp"
	"${RETROFE_DIR}/Source/Graphics/PageBuilder.cpp"
	"${RETROFE_DIR}/Source/Graphics/Page.cpp"
	"${RETROFE_DIR}/Source/Graphics/ViewInfo.cpp"
	"${RETROFE_DIR}/Source/Graphics/Animate/Animation.cpp"
	"${RETROFE_DIR}/Source/Graphics/Animate/AnimationEvents.cpp"
	"${RETROFE_DIR}/Source/Graphics/Animate/Tween.cpp"
	"${RETROFE_DIR}/Source/Graphics/Animate/TweenSet.cpp"
	"${RETROFE_DIR}/Source/Graphics/SearchOverlay.cpp"
	"${RETROFE_DIR}/Source/Graphics/ComponentItemBindingBuilder.cpp"
	"${RETROFE_DIR}/Source/Graphics/ComponentItemBinding.cpp"
	"${RETROFE_DIR}/Source/Graphics/Component/Container.cpp"
	"${RETROFE_DIR}/Source/Graphics/Component/Component.cpp"
	"${RETROFE_DIR}/Source/Graphics/Component/Image.cpp"
	"${RETROFE_DIR}/Source/Graphics/Component/ImageBuilder.cpp"
	"${RETROFE_DIR}/Source/Graphics/Component/Text.cpp"
	"${RETROFE_DIR}/Source/Graphics/Component/ReloadableMedia.cpp"
	"${RETROFE_DIR}/Source/Graphics/Component/ReloadableText.cpp"
	"${RETROFE_DIR}/Source/Graphics/Component/ReloadableScrollingText.cpp"
	"${RETROFE_DIR}/Source/Graphics/Component/ScrollingList.cpp"
	"${RETROFE_DIR}/Source/Graphics/Component/VideoBuilder.cpp"
	"${RETROFE_DIR}/Source/Graphics/Component/VideoComponent.cpp"
	"${RETROFE_DIR}/Source/Graphics/Component/Video.cpp"
	"${RETROFE_DIR}/Source/Menu/Menu.cpp"
	"${RETROFE_DIR}/Source/Sound/Sound.cpp"
	"${RETROFE_DIR}/Source/Utility/Log.cpp"
	"${RETROFE_DIR}/Source/Utility/Utils.cpp"
	"${RETROFE_DIR}/Source/Video/LibVLCVideo.cpp"
	"${RETROFE_DIR}/Source/Video/VideoFactory.cpp"
	"${RETROFE_DIR}/Source/Main.cpp"
	"${RETROFE_DIR}/Source/RetroFE.cpp"
	"${RETROFE_DIR}/Source/SDL.cpp"
	"${RETROFE_DIR}/Source/Version.cpp"
	"${SQLITE3_ROOT}/sqlite3.c"
)


set(EXECUTABLE_OUTPUT_PATH "${CMAKE_BINARY_DIR}" CACHE PATH "Build directory" FORCE)
set(LIBRARY_OUTPUT_PATH "${CMAKE_BINARY_DIR}" CACHE PATH "Build directory" FORCE)


include_directories(${RETROFE_INCLUDE_DIRS})
add_executable(retrofe  ${RETROFE_SOURCES} ${RETROFE_HEADERS})
target_link_libraries(retrofe ${RETROFE_LIBRARIES})
if(NOT WIN32)
    target_link_libraries(retrofe PkgConfig::LIBVLC)
endif()
# Deploy the VLC runtime beside the built executable, including decoder plugins.
# Do not copy VLC SDK headers/libs or the full VLC application into Build.
if(WIN32)
    add_custom_command(TARGET retrofe POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${VLC_ROOT}/libvlc.dll" "$<TARGET_FILE_DIR:retrofe>/libvlc.dll"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${VLC_ROOT}/libvlccore.dll" "$<TARGET_FILE_DIR:retrofe>/libvlccore.dll"
        COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${VLC_ROOT}/plugins" "$<TARGET_FILE_DIR:retrofe>/plugins"
        COMMENT "Deploying LibVLC runtime and plugins"
        VERBATIM)

    # Stage x64 SDL and supporting DLLs beside retrofe.exe when available.
    # Package.py also resolves dependencies from ThirdParty and checks PE bitness.
    if(MSVC AND CMAKE_SIZEOF_VOID_P EQUAL 8)
        foreach(_dll_name SDL2.dll SDL2_image.dll SDL2_mixer.dll SDL2_ttf.dll
                          libfreetype-6.dll zlib1.dll)
            file(GLOB_RECURSE _dll_candidates LIST_DIRECTORIES FALSE
                "${RETROFE_THIRD_PARTY_DIR}/*/${_dll_name}")
            set(_chosen_dll "")
            foreach(_candidate IN LISTS _dll_candidates)
                string(REPLACE "\\" "/" _normalized "${_candidate}")
                if(_normalized MATCHES "[/](x64|win64|amd64)[/]" OR
                   _normalized MATCHES "[/](zlib-x64|freetype-x64)[/]")
                    set(_chosen_dll "${_candidate}")
                    break()
                endif()
            endforeach()
            if(_chosen_dll)
                add_custom_command(TARGET retrofe POST_BUILD
                    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                        "${_chosen_dll}" "$<TARGET_FILE_DIR:retrofe>/${_dll_name}"
                    VERBATIM)
            else()
                message(STATUS "Runtime DLL ${_dll_name} not found under an x64 ThirdParty directory; packager will validate dependencies")
            endif()
        endforeach()
    endif()
endif()
set_target_properties(retrofe PROPERTIES LINKER_LANGUAGE CXX)


if(MSVC)
	set(CMAKE_DEBUG_POSTFIX "d")
	add_definitions(-D_CRT_SECURE_NO_DEPRECATE)
	add_definitions(-D_CRT_NONSTDC_NO_DEPRECATE)
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /MP /WX /MT")
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /MP /WX /MT")

	set_target_properties(retrofe PROPERTIES LINK_FLAGS "/SUBSYSTEM:WINDOWS")
else()
  set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -Wall -ggdb")
  set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -Wall -ggdb")
  set(CMAKE_C_FLAGS "${CMAKE_CXX_FLAGS} -Wall")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall")
endif()

