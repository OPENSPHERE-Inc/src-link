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

# =============================================================================
# curl static build (WebSocket support enabled)
#
# obs-deps ships curl without WebSocket protocol support (CURL_VERSION_WEBSOCKETS absent).
# We build curl from source with ENABLE_WEBSOCKETS=ON and statically link it,
# so the plugin carries its own curl instance independent of OBS's libcurl.dll.
# TLS backend: platform-native (Schannel on Windows, Secure Transport on macOS,
# mbedtls on Linux) to use OS CA certificate stores without bundling CA files.
# =============================================================================
include(FetchContent)

# --- mbedtls (Linux only — Windows/macOS use OS-native TLS) ---
if(OS_LINUX)
  FetchContent_Declare(
    mbedtls
    URL https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-3.6.3/mbedtls-3.6.3.tar.bz2
    URL_HASH SHA256=64cd73842cdc05e101172f7b437c65e7312e476206e1dbfd644433d11bc56327)
  set(USE_STATIC_MBEDTLS_LIBRARY ON CACHE BOOL "" FORCE)
  set(USE_SHARED_MBEDTLS_LIBRARY OFF CACHE BOOL "" FORCE)
  set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
  set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
  set(MBEDTLS_AS_SUBPROJECT ON CACHE BOOL "" FORCE)
  set(DISABLE_PACKAGE_CONFIG_AND_INSTALL ON CACHE BOOL "" FORCE)
  FetchContent_GetProperties(mbedtls)
  if(NOT mbedtls_POPULATED)
    FetchContent_Populate(mbedtls)
    add_subdirectory(${mbedtls_SOURCE_DIR} ${mbedtls_BINARY_DIR} EXCLUDE_FROM_ALL)
  endif()
endif()

# --- curl (static, WebSocket enabled) ---
FetchContent_Declare(
  curl
  URL https://github.com/curl/curl/releases/download/curl-8_12_1/curl-8.12.1.tar.gz
  URL_HASH SHA256=7b40ea64947e0b440716a4d7f0b7aa56230a5341c8377d7b609649d4aea8dbcf)
# Static library only
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_CURL_EXE OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(CURL_STATICLIB ON CACHE BOOL "" FORCE)
# TLS backend: platform-native for OS CA store access
if(OS_WINDOWS)
  set(CURL_USE_SCHANNEL ON CACHE BOOL "" FORCE)
  set(CURL_USE_MBEDTLS OFF CACHE BOOL "" FORCE)
  set(CURL_USE_OPENSSL OFF CACHE BOOL "" FORCE)
elseif(OS_MACOS)
  set(CURL_USE_SECTRANSP ON CACHE BOOL "" FORCE)
  set(CURL_USE_MBEDTLS OFF CACHE BOOL "" FORCE)
  set(CURL_USE_OPENSSL OFF CACHE BOOL "" FORCE)
else()
  set(CURL_USE_MBEDTLS ON CACHE BOOL "" FORCE)
  set(CURL_USE_OPENSSL OFF CACHE BOOL "" FORCE)
  set(MBEDTLS_INCLUDE_DIR "${mbedtls_SOURCE_DIR}/include" CACHE PATH "" FORCE)
  set(MBEDTLS_LIBRARY mbedtls CACHE STRING "" FORCE)
  set(MBEDX509_LIBRARY mbedx509 CACHE STRING "" FORCE)
  set(MBEDCRYPTO_LIBRARY mbedcrypto CACHE STRING "" FORCE)
endif()
# WebSocket support
set(ENABLE_WEBSOCKETS ON CACHE BOOL "" FORCE)
# Disable unused protocols (binary size reduction)
set(CURL_DISABLE_LDAP ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_LDAPS ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_FTP ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_FTPS ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_TFTP ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_SMTP ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_SMTPS ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_POP3 ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_POP3S ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_IMAP ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_IMAPS ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_TELNET ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_DICT ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_GOPHER ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_MQTT ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_RTSP ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_ALTSVC ON CACHE BOOL "" FORCE)
set(CURL_USE_LIBPSL OFF CACHE BOOL "" FORCE)
set(CURL_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
set(CURL_ENABLE_EXPORT_TARGET OFF CACHE BOOL "" FORCE)
set(HTTP_ONLY OFF CACHE BOOL "" FORCE)
FetchContent_GetProperties(curl)
if(NOT curl_POPULATED)
  FetchContent_Populate(curl)
  add_subdirectory(${curl_SOURCE_DIR} ${curl_BINARY_DIR} EXCLUDE_FROM_ALL)
endif()
# =============================================================================

target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE CURL_STATICLIB)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::libobs CURL::libcurl_static)

if(OS_WINDOWS)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE ws2_32 iphlpapi crypt32 bcrypt)
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
