cmake_minimum_required(VERSION 3.16)
project(skyroads C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
add_compile_options(-Wall -Wextra)

if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
  set(SR_IOS ON)
endif()

set(CORE_SRC
  src/core/lzs.c
  src/core/assets.c
  src/core/gfx.c
  src/core/game.c
  src/core/play.c
  src/core/render.c
  src/core/tables.c
  src/core/hud.c
  src/core/cfg.c
  src/core/text.c
  src/core/audio.c
  src/thirdparty/opl3.c
)

set(DATA_FILES_ALL
  ANIM.LZS CARS.LZS DASHBRD.LZS DEMO.REC FUL_DISP.DAT GOMENU.LZS
  HELPMENU.LZS INTRO.LZS INTRO.SND MAINMENU.LZS MUZAX.LZS OXY_DISP.DAT
  ROADS.LZS SETMENU.LZS SFX.SND SPEED.DAT TREKDAT.LZS
  WORLD0.LZS WORLD1.LZS WORLD2.LZS WORLD3.LZS WORLD4.LZS
  WORLD5.LZS WORLD6.LZS WORLD7.LZS WORLD8.LZS WORLD9.LZS)
# The game data ships separately (SkyRoads is Bluemoon's freeware); only
# bundle what's actually present so the engine builds from a clean checkout.
set(DATA_FILES "")
foreach(f ${DATA_FILES_ALL})
  if(EXISTS ${CMAKE_SOURCE_DIR}/${f})
    list(APPEND DATA_FILES ${f})
  endif()
endforeach()
list(LENGTH DATA_FILES SR_DATA_COUNT)
if(SR_DATA_COUNT EQUAL 0)
  message(STATUS "SkyRoads data files not present - building engine only. "
                 "Drop the freeware game files into the repo root to bundle "
                 "them (iOS/web) or pass their folder to the executable.")
endif()

# ---------------------------------------------------------------- iOS ------
if(SR_IOS)
  include(FetchContent)
  # Archives must contain ONLY the .app to count as an "iOS App archive"
  # (otherwise Xcode calls it generic and hides the TestFlight option):
  # skip install for every target, then opt the app back in below.
  set(CMAKE_XCODE_ATTRIBUTE_SKIP_INSTALL YES)
  # App Store Connect wants a separate dSYM for crash symbolication:
  # compile Release with debug info and split it into a dSYM at archive.
  set(CMAKE_XCODE_ATTRIBUTE_DEBUG_INFORMATION_FORMAT "dwarf-with-dsym")
  set(CMAKE_XCODE_ATTRIBUTE_GCC_GENERATE_DEBUGGING_SYMBOLS YES)
  set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -g")
  set(SDL_SHARED OFF CACHE BOOL "" FORCE)
  set(SDL_STATIC ON CACHE BOOL "" FORCE)
  FetchContent_Declare(SDL2
    URL https://github.com/libsdl-org/SDL/releases/download/release-2.30.9/SDL2-2.30.9.tar.gz)
  FetchContent_MakeAvailable(SDL2)

  set(BUNDLED_DATA "")
  foreach(f ${DATA_FILES})
    list(APPEND BUNDLED_DATA "${CMAKE_SOURCE_DIR}/${f}")
  endforeach()

  add_executable(skyroads MACOSX_BUNDLE
    ${CORE_SRC} src/platform/sdl/main_sdl.c ${BUNDLED_DATA}
    ${CMAKE_SOURCE_DIR}/src/platform/ios/Assets.xcassets)
  set_source_files_properties(${BUNDLED_DATA}
    ${CMAKE_SOURCE_DIR}/src/platform/ios/Assets.xcassets PROPERTIES
    MACOSX_PACKAGE_LOCATION Resources)
  # SDL renames our main() to SDL_main(); SDL2main provides the real entry.
  target_link_libraries(skyroads PRIVATE SDL2::SDL2main SDL2::SDL2-static)
  set(SR_IOS_TEAM "" CACHE STRING
      "Apple Development Team ID; set for on-device builds, empty = simulator unsigned")
  set(SR_IOS_BUILD "2" CACHE STRING
      "CFBundleVersion; must increase for every App Store Connect upload")
  set_target_properties(skyroads PROPERTIES
    MACOSX_BUNDLE_GUI_IDENTIFIER ee.bluemoon.skyroads.port
    MACOSX_BUNDLE_BUNDLE_NAME SkyRoads
    MACOSX_BUNDLE_BUNDLE_VERSION ${SR_IOS_BUILD}
    MACOSX_BUNDLE_SHORT_VERSION_STRING 1.0
    MACOSX_BUNDLE_INFO_PLIST ${CMAKE_SOURCE_DIR}/src/platform/ios/Info.plist.in
    XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
    XCODE_ATTRIBUTE_IPHONEOS_DEPLOYMENT_TARGET 14.0)
  set_target_properties(skyroads PROPERTIES
    XCODE_ATTRIBUTE_ASSETCATALOG_COMPILER_APPICON_NAME AppIcon
    XCODE_ATTRIBUTE_SKIP_INSTALL NO
    XCODE_ATTRIBUTE_INSTALL_PATH "$(LOCAL_APPS_DIR)"
    XCODE_ATTRIBUTE_DEBUG_INFORMATION_FORMAT "dwarf-with-dsym")
  # Generate the dSYM at link time (object files still on disk). Write it
  # to Xcode's DWARF_DSYM_FOLDER_PATH so 'archive' collects it into the
  # xcarchive's dSYMs/ (App Store Connect requires it for symbolication).
  add_custom_command(TARGET skyroads POST_BUILD
    COMMAND sh -c
      "dsymutil \"$<TARGET_FILE:skyroads>\" -o \"\$DWARF_DSYM_FOLDER_PATH/skyroads.app.dSYM\"")
  if(SR_IOS_TEAM)
    set_target_properties(skyroads PROPERTIES
      XCODE_ATTRIBUTE_DEVELOPMENT_TEAM ${SR_IOS_TEAM}
      XCODE_ATTRIBUTE_CODE_SIGN_STYLE Automatic
      XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "Apple Development")
  else()
    set_target_properties(skyroads PROPERTIES
      XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED NO)
  endif()
  return()
endif()

# ----------------------------------------------------------- web (emcc) ----
add_executable(skyroads ${CORE_SRC} src/platform/sdl/main_sdl.c)

if(EMSCRIPTEN)
  set(PRELOAD "")
  foreach(f ${DATA_FILES})
    string(APPEND PRELOAD " --preload-file ${CMAKE_SOURCE_DIR}/${f}@/data/${f}")
  endforeach()
  set_target_properties(skyroads PROPERTIES
    SUFFIX ".html"
    COMPILE_FLAGS "-sUSE_SDL=2"
    LINK_FLAGS "-sUSE_SDL=2 -sALLOW_MEMORY_GROWTH=1 ${PRELOAD}")
  return()
endif()

# ------------------------------------------------------- desktop (SDL2) ----
find_package(SDL2 REQUIRED)
if(WIN32 AND TARGET SDL2::SDL2main)
  # SDL renames main() to SDL_main(); SDL2main supplies WinMain.
  target_link_libraries(skyroads PRIVATE SDL2::SDL2main)
endif()
if(TARGET SDL2::SDL2)
  target_link_libraries(skyroads PRIVATE SDL2::SDL2)
else()
  target_include_directories(skyroads PRIVATE ${SDL2_INCLUDE_DIRS})
  target_link_libraries(skyroads PRIVATE ${SDL2_LIBRARIES})
endif()
if(WIN32)
  # ship SDL2.dll beside the exe
  add_custom_command(TARGET skyroads POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:SDL2::SDL2> $<TARGET_FILE_DIR:skyroads>)
endif()

add_executable(sr_dump       ${CORE_SRC} src/tools/dump_frames.c)
add_executable(sr_render_test ${CORE_SRC} src/tools/render_test.c)
add_executable(sr_demo_test  src/core/lzs.c src/core/assets.c src/core/play.c
                             src/tools/demo_test.c)
add_executable(sr_music_test src/core/lzs.c src/core/assets.c src/core/audio.c
                             src/thirdparty/opl3.c src/tools/music_test.c)

enable_testing()
add_test(NAME demo_playback COMMAND sr_demo_test ${CMAKE_SOURCE_DIR})
