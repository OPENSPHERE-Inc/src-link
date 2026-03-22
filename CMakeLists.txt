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
find_package(CURL REQUIRED)

target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::libobs CURL::libcurl)

if(OS_WINDOWS)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE ws2_32 iphlpapi)
endif()

target_compile_definitions(
  ${CMAKE_PROJECT_NAME}
  PRIVATE ASIO_STANDALONE $<$<BOOL:${PLUGIN_TESTS}>:PLUGIN_TESTS> $<$<BOOL:${OS_WINDOWS}>:_WEBSOCKETPP_CPP11_STL_>
          $<$<BOOL:${OS_WINDOWS}>:_WIN32_WINNT=0x0603>)

if(ENABLE_FRONTEND_API)
  find_package(obs-frontend-api REQUIRED)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::obs-frontend-api)
endif()

if(ENABLE_QT)
  find_package(Qt6 COMPONENTS Widgets Core)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Qt6::Core Qt6::Widgets)
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

configure_file(lib/obs-websocket/src/plugin-macros.h.in plugin-macros.generated.h)

set(obs-websocket_SRCS
    lib/obs-websocket/src/eventhandler/EventHandler_Config.cpp
    lib/obs-websocket/src/eventhandler/EventHandler_Filters.cpp
    lib/obs-websocket/src/eventhandler/EventHandler_General.cpp
    lib/obs-websocket/src/eventhandler/EventHandler_Inputs.cpp
    lib/obs-websocket/src/eventhandler/EventHandler_MediaInputs.cpp
    lib/obs-websocket/src/eventhandler/EventHandler_Outputs.cpp
    lib/obs-websocket/src/eventhandler/EventHandler_SceneItems.cpp
    lib/obs-websocket/src/eventhandler/EventHandler_Scenes.cpp
    lib/obs-websocket/src/eventhandler/EventHandler_Transitions.cpp
    lib/obs-websocket/src/eventhandler/EventHandler_Ui.cpp
    lib/obs-websocket/src/eventhandler/EventHandler.cpp
    lib/obs-websocket/src/utils/Obs_VolumeMeter.cpp
    lib/obs-websocket/src/utils/Obs_StringHelper.cpp
    lib/obs-websocket/src/utils/Obs_ArrayHelper.cpp
    lib/obs-websocket/src/utils/Obs_ObjectHelper.cpp
    lib/obs-websocket/src/utils/Json.cpp
    lib/obs-websocket/src/utils/Obs_NumberHelper.cpp
    lib/obs-websocket/src/plugin-macros.h.in)

target_sources(${CMAKE_PROJECT_NAME} PRIVATE ${obs-websocket_SRCS})
target_compile_options(${CMAKE_PROJECT_NAME} PRIVATE $<$<COMPILE_LANG_AND_ID:CXX,GNU,AppleClang,Clang>:-Wno-error=shadow
                                                     -Wno-error=conversion>)

add_subdirectory(${CMAKE_SOURCE_DIR}/lib/json)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE nlohmann_json::nlohmann_json)

target_sources(
  ${CMAKE_PROJECT_NAME}
  PRIVATE src/plugin-main.cpp
          src/api-client.cpp
          src/api-websocket.cpp
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
          src/UI/ws-portal-dock.ui
          src/UI/ws-portal-dock.cpp
          src/UI/redeem-invite-code-dialog.ui
          src/UI/redeem-invite-code-dialog.cpp
          src/UI/resources.qrc
          src/sources/ingress-link-source.cpp
          src/sources/audio-capture.cpp
          src/sources/image-renderer.cpp
          src/outputs/egress-link-output.cpp
          src/outputs/audio-source.cpp
          src/ws-portal/ws-portal-client.cpp
          src/ws-portal/event-handler.cpp
          src/net/http-error.cpp
          src/net/curl-http-client.cpp
          src/net/ws-client.cpp
          src/net/local-http-server.cpp
          src/net/oauth2-client.cpp
          src/net/http-request-invoker.cpp)

target_include_directories(
  ${CMAKE_PROJECT_NAME}
  PRIVATE ${CMAKE_SOURCE_DIR}/shared/properties-view
          ${CMAKE_SOURCE_DIR}/shared/qt/vertical-scroll-area ${CMAKE_SOURCE_DIR}/shared/qt/wrappers
          ${CMAKE_SOURCE_DIR}/lib/obs-websocket/lib ${CMAKE_SOURCE_DIR}/lib/obs-websocket/src)

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})
