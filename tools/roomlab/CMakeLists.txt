cmake_minimum_required(VERSION 3.20)
project(StruckRoom CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# The object model is the plugin's own ModalBank (src/Module), built in place
# so the experiments always hear what the plugin plays.
set(STRUCKROOM_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../.." CACHE PATH "StruckRoom checkout (ModalBank lives in its src/Module)")
set(MODAL_DIR "${STRUCKROOM_DIR}/src/Module")
if(NOT EXISTS "${MODAL_DIR}/ModalBank.cpp")
    message(FATAL_ERROR "ModalBank.cpp not found under ${MODAL_DIR}; set STRUCKROOM_DIR")
endif()

add_executable(roomlab roomlab.cpp "${MODAL_DIR}/ModalBank.cpp")
target_include_directories(roomlab PRIVATE "${MODAL_DIR}")
# -ffast-math lets the room-mode loop vectorise its two sums.
target_compile_options(roomlab PRIVATE -O3 -ffast-math -Wall -Wno-unused-parameter)
