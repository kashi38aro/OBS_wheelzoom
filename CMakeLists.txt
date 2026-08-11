cmake_minimum_required(VERSION 3.28)

project(obs-wheelzoom VERSION 0.1.3 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(OBS_BUILD_DIR "" CACHE PATH "OBS Studio build/install prefix containing CMake packages")
if(OBS_BUILD_DIR)
  list(PREPEND CMAKE_PREFIX_PATH "${OBS_BUILD_DIR}")
endif()

find_package(libobs REQUIRED)
find_package(obs-frontend-api REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Core Widgets)

add_library(${PROJECT_NAME} MODULE
  src/plugin-main.cpp
)

target_link_libraries(${PROJECT_NAME}
  PRIVATE
    OBS::libobs
    OBS::obs-frontend-api
    Qt6::Core
    Qt6::Widgets
)

target_compile_definitions(${PROJECT_NAME}
  PRIVATE
    QT_NO_KEYWORDS
)

set_target_properties(${PROJECT_NAME} PROPERTIES
  OUTPUT_NAME "obs-wheelzoom"
  PREFIX ""
  AUTOMOC ON
  AUTOUIC ON
  AUTORCC ON
)

include(GNUInstallDirs)

# These destinations match the layout used by the OBS Studio plugin loader.
install(TARGETS ${PROJECT_NAME}
  RUNTIME DESTINATION "obs-plugins/64bit"
  LIBRARY DESTINATION "obs-plugins/64bit"
)

install(DIRECTORY data/
  DESTINATION "data/obs-plugins/obs-wheelzoom"
)
