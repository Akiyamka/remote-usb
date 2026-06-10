# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
set(PROJECT_VER "0.0.8")
project(remote_usb)

littlefs_create_partition_image(webfs web_dist FLASH_IN_PROJECT)
