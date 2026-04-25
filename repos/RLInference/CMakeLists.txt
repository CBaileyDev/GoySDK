# RLInference static library — builds from source in this extracted workspace.
cmake_minimum_required(VERSION 3.18)
project(RLInference LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Torch REQUIRED)

file(GLOB_RECURSE RLINFERENCE_SOURCES "src/*.cpp")

add_library(RLInference STATIC ${RLINFERENCE_SOURCES})

target_include_directories(RLInference PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(RLInference PUBLIC ${TORCH_LIBRARIES})

if(MSVC)
    target_compile_options(RLInference PRIVATE /wd4267 /wd4244)
endif()
