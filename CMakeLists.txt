cmake_minimum_required(VERSION 3.21)

include(cmake/LpmMetadata.cmake)
project(${LILYGO_UI_CMAKE_PROJECT_NAME} LANGUAGES C CXX)

include(GNUInstallDirs)
include(CTest)
include(cmake/GpuSdk.cmake)
find_package(LilyGoUI CONFIG REQUIRED)
find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(JSONC REQUIRED IMPORTED_TARGET json-c)
find_library(YOUTUBE_CURL_LIBRARY NAMES curl libcurl.so.4 REQUIRED)

configure_file(
    cmake/app_identity.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/app_identity.h
    @ONLY)

add_library(lilygo_ui_youtube_core STATIC
    src/pages/youtube/youtube_model.cpp
    src/services/process.cpp
    src/services/http.cpp
    src/services/youtube_service.cpp
    src/services/player.cpp)
target_include_directories(lilygo_ui_youtube_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_BINARY_DIR}/generated)
target_include_directories(lilygo_ui_youtube_core PRIVATE third_party/mpv/include third_party/curl/include)
target_link_libraries(lilygo_ui_youtube_core PUBLIC PkgConfig::JSONC Threads::Threads ${CMAKE_DL_LIBS} ${YOUTUBE_CURL_LIBRARY})
if(NOT CM0_SIMULATOR)
    # Use the same ALSA output format as the other CM0 audio applications.
    target_compile_definitions(lilygo_ui_youtube_core PRIVATE YOUTUBE_DEVICE_ALSA=1)
endif()
if(NOT CM0_SIMULATOR AND CM0_DRM_OPENGL)
    target_compile_definitions(lilygo_ui_youtube_core PRIVATE YOUTUBE_ENABLE_OPENGL=1)
    target_include_directories(lilygo_ui_youtube_core PRIVATE ${CM0_EGL_INCLUDE_DIR} ${CM0_GLES_INCLUDE_DIR})
    target_link_libraries(lilygo_ui_youtube_core PUBLIC ${CM0_EGL_LIBRARY} ${CM0_GLES_LIBRARY})
    if(YOUTUBE_GPU_SDK_LIB_DIR)
        target_link_options(lilygo_ui_youtube_core PUBLIC "-Wl,-rpath-link,${YOUTUBE_GPU_SDK_LIB_DIR}")
    endif()
endif()
target_compile_options(lilygo_ui_youtube_core PRIVATE
    -Wall -Wextra -Wpedantic)
target_compile_features(lilygo_ui_youtube_core PUBLIC cxx_std_17)

add_library(lilygo_ui_youtube_ui STATIC
    src/app.cpp
    src/components/components.cpp
    src/pages/youtube/youtube_view.cpp
    src/pages/youtube/youtube_view_model.cpp)
target_include_directories(lilygo_ui_youtube_ui PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_BINARY_DIR}/generated)
target_compile_options(lilygo_ui_youtube_ui PRIVATE
    -Wall -Wextra -Wpedantic)
target_link_libraries(lilygo_ui_youtube_ui PUBLIC
    lilygo_ui_youtube_core
    LilyGoUI::Runtime)

add_executable(lilygo_ui_youtube src/main.cpp)
target_compile_options(lilygo_ui_youtube PRIVATE
    -Wall -Wextra -Wpedantic)
target_link_libraries(lilygo_ui_youtube PRIVATE lilygo_ui_youtube_ui)
set_target_properties(lilygo_ui_youtube PROPERTIES
    OUTPUT_NAME "${LILYGO_UI_EXECUTABLE_NAME}")

configure_file(data/launcher.json.in
    ${CMAKE_CURRENT_BINARY_DIR}/${LILYGO_UI_APP_ID}.json @ONLY)
configure_file(data/application.desktop.in
    ${CMAKE_CURRENT_BINARY_DIR}/${LILYGO_UI_APP_ID}.desktop @ONLY)
configure_file(data/application.metainfo.xml.in
    ${CMAKE_CURRENT_BINARY_DIR}/${LILYGO_UI_APP_ID}.metainfo.xml @ONLY)

install(TARGETS lilygo_ui_youtube
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    COMPONENT app)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${LILYGO_UI_APP_ID}.json
    DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/launcher/apps
    COMPONENT app)
lilygo_ui_install_launcher_icon(
    "${LILYGO_UI_APP_ID}"
    "${CMAKE_CURRENT_SOURCE_DIR}/${LILYGO_UI_APP_ICON}")
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${LILYGO_UI_APP_ID}.desktop
    DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/applications
    COMPONENT app)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${LILYGO_UI_APP_ID}.metainfo.xml
    DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/metainfo
    COMPONENT app)
install(FILES third_party/mpv/include/mpv/client.h third_party/mpv/include/mpv/render.h third_party/mpv/include/mpv/render_gl.h
    DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/doc/${LILYGO_UI_PACKAGE_NAME}/licenses/mpv
    COMPONENT app)
install(DIRECTORY third_party/curl/include/curl
    DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/doc/${LILYGO_UI_PACKAGE_NAME}/licenses
    COMPONENT app)
lilygo_ui_install_font_licenses(
    "${CMAKE_INSTALL_DATAROOTDIR}/doc/${LILYGO_UI_PACKAGE_NAME}"
    app)

add_executable(youtube-player-smoke EXCLUDE_FROM_ALL tests/player_smoke.cpp)
target_link_libraries(youtube-player-smoke PRIVATE lilygo_ui_youtube_core)
target_compile_options(youtube-player-smoke PRIVATE -Wall -Wextra -Wpedantic -UNDEBUG)

if(NOT CM0_SIMULATOR AND CM0_DRM_OPENGL)
    add_executable(youtube-player-gpu-smoke EXCLUDE_FROM_ALL tests/player_gpu_smoke.cpp)
    target_include_directories(youtube-player-gpu-smoke PRIVATE ${CM0_EGL_INCLUDE_DIR} ${CM0_GLES_INCLUDE_DIR})
    target_link_libraries(youtube-player-gpu-smoke PRIVATE lilygo_ui_youtube_core)
    target_compile_options(youtube-player-gpu-smoke PRIVATE -Wall -Wextra -Wpedantic -UNDEBUG)
endif()

if(BUILD_TESTING AND NOT CMAKE_CROSSCOMPILING)
    add_executable(youtube-core-tests tests/test_core.cpp)
    target_compile_options(youtube-core-tests PRIVATE
        -Wall -Wextra -Wpedantic -UNDEBUG)
    target_link_libraries(youtube-core-tests PRIVATE
        lilygo_ui_youtube_core)
    add_test(NAME youtube-core COMMAND youtube-core-tests)

    add_executable(youtube-view-model-tests tests/test_view_model.cpp)
    target_compile_options(youtube-view-model-tests PRIVATE
        -Wall -Wextra -Wpedantic -UNDEBUG)
    target_link_libraries(youtube-view-model-tests PRIVATE
        lilygo_ui_youtube_ui)
    add_test(NAME youtube-view-model COMMAND youtube-view-model-tests)

    add_executable(youtube-render-ui tests/render_ui.cpp)
    target_compile_options(youtube-render-ui PRIVATE
        -Wall -Wextra -Wpedantic -UNDEBUG)
    target_link_libraries(youtube-render-ui PRIVATE
        lilygo_ui_youtube_ui)
    foreach(viewport IN ITEMS portrait landscape)
        if(viewport STREQUAL "portrait")
            set(_width 568)
            set(_height 1232)
        else()
            set(_width 1232)
            set(_height 568)
        endif()
        add_test(NAME youtube-render-${viewport} COMMAND youtube-render-ui
            ${_width} ${_height}
            ${CMAKE_CURRENT_BINARY_DIR}/youtube-${viewport}.ppm)
    endforeach()
endif()

# New applications have no legacy CM0 package to replace. Add Debian migration
# fields only when this project supersedes an actually published package.
set(CPACK_GENERATOR DEB)
set(CPACK_PACKAGE_NAME "${LILYGO_UI_PACKAGE_NAME}")
set(CPACK_PACKAGE_VERSION "${LILYGO_UI_VERSION}")
if(LILYGO_UI_VERSION MATCHES
   "^([0-9]+:)?([0-9]+)\\.([0-9]+)\\.([0-9]+)")
    set(CPACK_PACKAGE_VERSION_MAJOR "${CMAKE_MATCH_2}")
    set(CPACK_PACKAGE_VERSION_MINOR "${CMAKE_MATCH_3}")
    set(CPACK_PACKAGE_VERSION_PATCH "${CMAKE_MATCH_4}")
else()
    set(CPACK_PACKAGE_VERSION_MAJOR 0)
    set(CPACK_PACKAGE_VERSION_MINOR 0)
    set(CPACK_PACKAGE_VERSION_PATCH 0)
endif()
set(CPACK_PACKAGE_CONTACT "${LILYGO_UI_PACKAGE_CONTACT}")
set(CPACK_PACKAGE_VENDOR "${LILYGO_UI_PACKAGE_CONTACT}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${LILYGO_UI_APP_SUMMARY}")
set(CPACK_PACKAGE_HOMEPAGE_URL "${LILYGO_UI_APP_HOMEPAGE}")
set(CPACK_DEB_COMPONENT_INSTALL ON)
set(CPACK_SET_DESTDIR ON)
set(CPACK_COMPONENTS_ALL app)
set(CPACK_DEBIAN_APP_PACKAGE_NAME "${LILYGO_UI_PACKAGE_NAME}")
set(CPACK_DEBIAN_APP_FILE_NAME DEB-DEFAULT)
set(CPACK_DEBIAN_APP_PACKAGE_DEPENDS
    "libc6, libstdc++6, libdrm2, libegl1, libgles2, libgbm1, libfreetype6, libjson-c5, libcurl4t64, libmpv2 (>= 0.38), ffmpeg, ca-certificates, lilygo-ui-appkit-dev (>= ${LILYGO_UI_MIN_APPKIT_VERSION})")
set(CPACK_DEBIAN_APP_PACKAGE_SECTION "utils")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS OFF)
if(CMAKE_CROSSCOMPILING AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$")
    set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE
        "${LILYGO_UI_PACKAGE_ARCHITECTURE}")
endif()
include(CPack)
