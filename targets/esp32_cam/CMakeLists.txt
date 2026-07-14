cmake_minimum_required(VERSION 3.16)

set(IDF_TARGET "esp32" CACHE STRING "ESP-IDF target" FORCE)
set(PROJECT_VER "1.0.0-rc3")
set(EXTRA_COMPONENT_DIRS "../../components")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(typx_esp32_cam)
