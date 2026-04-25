# RLInference static library — builds from source in this extracted workspace.
cmake_minimum_required(VERSION 3.18)
project(RLInference LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Torch REQUIRED)

file(GLOB_RECURSE RLINFERENCE_SOURCES CONFIGURE_DEPENDS "src/*.cpp")

add_library(RLInference STATIC
    ${RLINFERENCE_SOURCES}
    third_party/miniz/miniz_tinfl.c
)

target_include_directories(RLInference
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/miniz
)

target_compile_definitions(RLInference PRIVATE
    MINIZ_NO_ARCHIVE_APIS
    MINIZ_NO_DEFLATE_APIS
    MINIZ_NO_STDIO
)

target_link_libraries(RLInference PUBLIC ${TORCH_LIBRARIES})

if(MSVC)
    target_compile_options(RLInference PRIVATE /wd4267 /wd4244)
endif()
