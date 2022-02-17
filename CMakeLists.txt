cmake_minimum_required(VERSION 3.16.0)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
list(APPEND EXTRA_COMPONENT_DIRS $ENV{ESP_IDF_COMPONENTS}/webrtc-vad)
project(esp32-i2s-mic-vad)
