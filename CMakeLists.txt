cmake_minimum_required(VERSION 3.16...3.26)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

option(ENABLE_FRONTEND_API "Use obs-frontend-api for UI functionality" ON)
option(ENABLE_QT "Use Qt functionality" ON)

if(DEFINED ENV{API_SERVER})
  add_compile_definitions(API_SERVER="$ENV{API_SERVER}")
endif()
if(DEFINED ENV{API_WS_SERVER})
  add_compile_definitions(API_WS_SERVER="$ENV{API_WS_SERVER}")
endif()
if(DEFINED ENV{FRONTEND_SERVER})
  add_compile_definitions(FRONTEND_SERVER="$ENV{FRONTEND_SERVER}")
endif()
if(DEFINED ENV{CLIENT_ID})
  add_compile_definitions(CLIENT_ID="$ENV{CLIENT_ID}")
endif()
if(DEFINED ENV{CLIENT_SECRET})
  add_compile_definitions(CLIENT_SECRET="$ENV{CLIENT_SECRET}")
endif()
if(DEFINED ENV{SCHEMA_DEBUG})
  add_compile_definitions(SCHEMA_DEBUG)
endif()
if(DEFINED ENV{API_DEBUG})
  add_compile_definitions(API_DEBUG)
endif()

include(compilerconfig)
include(defaults)
include(helpers)

add_library(${CMAKE_PROJECT_NAME} MODULE)

find_package(libobs REQUIRED)

target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::libobs)

target_compile_definitions(
  ${CMAKE_PROJECT_NAME}
  PRIVATE ASIO_STANDALONE $<$<BOOL:${PLUGIN_TESTS}>:PLUGIN_TESTS> $<$<BOOL:${OS_WINDOWS}>:_WEBSOCKETPP_CPP11_STL_>
          $<$<BOOL:${OS_WINDOWS}>:_WIN32_WINNT=0x0603>)

if(ENABLE_FRONTEND_API)
  find_package(obs-frontend-api REQUIRED)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::obs-frontend-api)
endif()

if(ENABLE_QT)
  find_package(Qt6 COMPONENTS Widgets Core Network WebSockets)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Qt6::Core Qt6::Widgets Qt6::Network Qt6::WebSockets)
  target_compile_options(
    ${CMAKE_PROJECT_NAME} PRIVATE $<$<C_COMPILER_ID:Clang,AppleClang>:-Wno-quoted-include-in-framework-header
                                  -Wno-comma>)
  set_target_properties(
    ${CMAKE_PROJECT_NAME}
    PROPERTIES AUTOMOC ON
               AUTOUIC ON
               AUTORCC ON)
endif()

add_subdirectory(${CMAKE_SOURCE_DIR}/shared/properties-view)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::properties-view)

set(o2_SRCS
    lib/o2/src/o2.cpp
    lib/o2/src/o2pollserver.cpp
    lib/o2/src/o2reply.cpp
    lib/o2/src/o2replyserver.cpp
    lib/o2/src/o2requestor.cpp
    lib/o2/src/o2simplecrypt.cpp
    lib/o2/src/o0jsonresponse.cpp
    lib/o2/src/o0settingsstore.cpp
    lib/o2/src/o0baseauth.cpp)

target_sources(${CMAKE_PROJECT_NAME} PRIVATE ${o2_SRCS})

add_subdirectory(${CMAKE_SOURCE_DIR}/lib/json)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE nlohmann_json::nlohmann_json)

target_sources(
  ${CMAKE_PROJECT_NAME}
  PRIVATE src/plugin-main.cpp
          src/api-client.cpp
          src/api-websocket.cpp
          src/request-invoker.cpp
          src/schema.hpp
          src/utils.cpp
          src/settings.cpp
          src/UI/settings-dialog.ui
          src/UI/settings-dialog.cpp
          src/UI/output-dialog.ui
          src/UI/output-dialog.cpp
          src/UI/egress-link-dock.ui
          src/UI/egress-link-dock.cpp
          src/UI/egress-link-connection-widget.ui
          src/UI/egress-link-connection-widget.cpp
          src/UI/resources.qrc
          src/sources/ingress-link-source.cpp
          src/sources/audio-capture.cpp
          src/sources/image-renderer.cpp
          src/outputs/egress-link-output.cpp
          src/outputs/audio-source.cpp)

target_include_directories(
  ${CMAKE_PROJECT_NAME}
  PRIVATE ${CMAKE_SOURCE_DIR}/lib/o2/src ${CMAKE_SOURCE_DIR}/shared/properties-view
          ${CMAKE_SOURCE_DIR}/shared/qt/vertical-scroll-area ${CMAKE_SOURCE_DIR}/shared/qt/wrappers)

if(NOT buildspec)
  file(READ "${CMAKE_CURRENT_SOURCE_DIR}/buildspec.json" buildspec)
endif()
string(
  JSON
  qtversion
  GET
  ${buildspec}
  dependencies
  qt6
  version)

if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
  install(
    DIRECTORY "${CMAKE_SOURCE_DIR}/.deps/obs-deps-qt6-${qtversion}-universal/lib/QtWebSockets.framework"
    CONFIGURATIONS RelWithDebInfo Debug Release
    DESTINATION "."
    OPTIONAL)
  install(
    DIRECTORY "${CMAKE_SOURCE_DIR}/.deps/obs-deps-qt6-${qtversion}-universal/plugins/tls"
    CONFIGURATIONS RelWithDebInfo Debug Release
    DESTINATION "."
    OPTIONAL)

  install(
    CODE [===[
    if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/QtWebSockets.framework" AND NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/QtWebSockets.framework")
      file(INSTALL DESTINATION "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/package/Library/Application Support/obs-studio/plugins"
        TYPE DIRECTORY FILES "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/QtWebSockets.framework" USE_SOURCE_PERMISSIONS)
    endif()
    ]===])
  install(
    CODE [===[
    if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/tls" AND NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/tls")
      file(INSTALL DESTINATION "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/package/Library/Application Support/obs-studio/plugins"
        TYPE DIRECTORY FILES "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/tls" USE_SOURCE_PERMISSIONS)
    endif()
    ]===])
endif()

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})

if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
  install(
    FILES "${CMAKE_SOURCE_DIR}/.deps/obs-deps-qt6-${qtversion}-x64/bin/Qt6WebSockets.dll"
    CONFIGURATIONS RelWithDebInfo Debug Release
    DESTINATION bin/64bit
    OPTIONAL)
  install(
    DIRECTORY "${CMAKE_SOURCE_DIR}/.deps/obs-deps-qt6-${qtversion}-x64/plugins/tls"
    CONFIGURATIONS RelWithDebInfo Debug Release
    DESTINATION bin/64bit
    OPTIONAL)
endif()
