cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

option(ENABLE_FRONTEND_API "Use obs-frontend-api for UI functionality" ON)
option(ENABLE_QT "Use Qt functionality" ON)
option(STREAM_NOTIFIER_INSTALL_TO_OBS_ROOT
       "Also install into an OBS Studio root/portable layout"
       OFF)

include(compilerconfig)
include(defaults)
include(helpers)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(${CMAKE_PROJECT_NAME} MODULE)

find_package(libobs REQUIRED)
find_package(obs-frontend-api REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Core Widgets)
find_package(CURL REQUIRED)

target_sources(${CMAKE_PROJECT_NAME} PRIVATE src/stream-notifier.cpp)

target_compile_definitions(
  ${CMAKE_PROJECT_NAME}
  PRIVATE PLUGIN_ID="${CMAKE_PROJECT_NAME}"
          PLUGIN_NAME="StreamNotifier"
          PLUGIN_VERSION="${PROJECT_VERSION}"
)

target_link_libraries(
  ${CMAKE_PROJECT_NAME}
  PRIVATE OBS::libobs OBS::obs-frontend-api Qt6::Core Qt6::Widgets CURL::libcurl
)

set_target_properties(
  ${CMAKE_PROJECT_NAME}
  PROPERTIES AUTOMOC ON AUTOUIC ON AUTORCC ON
)

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})
