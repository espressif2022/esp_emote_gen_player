# Standalone test application for emote_gen_player
cmake_minimum_required(VERSION 3.5)
if(EXISTS "$ENV{IDF_PATH}/tools/unit-test-app/components")
    list(APPEND EXTRA_COMPONENT_DIRS "$ENV{IDF_PATH}/tools/unit-test-app/components")
endif()
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(emote_gen_player_test)
