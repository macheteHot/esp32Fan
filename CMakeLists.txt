cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS
    ${CMAKE_CURRENT_LIST_DIR}/components/common
    ${CMAKE_CURRENT_LIST_DIR}/components/homekit
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp32Fan)