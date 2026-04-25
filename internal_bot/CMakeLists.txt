cmake_minimum_required(VERSION 3.18)
project(GoySDK LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
include(CTest)


find_package(Torch REQUIRED)


if(NOT DEFINED VIGEM_DIR)
    set(VIGEM_DIR "${CMAKE_CURRENT_SOURCE_DIR}/deps/ViGEmClient")
endif()
add_subdirectory(${VIGEM_DIR} ${CMAKE_BINARY_DIR}/ViGEmClient)


set(RLINFERENCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../repos/RLInference")
add_subdirectory(${RLINFERENCE_DIR} ${CMAKE_BINARY_DIR}/RLInference)


file(GLOB_RECURSE TNTS_SOURCES
    "Components/*.cpp"
    "Modules/*.cpp"
    "Extensions/*.cpp"
    "ImGui/*.cpp"
    "RLSDK/*.cpp"
    "OverlayRenderer.cpp"
    "pch.cpp"
)

file(GLOB_RECURSE GOYSDK_SOURCES
    "GoySDK/*.cpp"
)

add_library(GoySDKCore SHARED
    dllmain.cpp
    resources.rc
    ${TNTS_SOURCES}
    ${GOYSDK_SOURCES}
)


target_precompile_headers(GoySDKCore PRIVATE pch.hpp)


target_include_directories(GoySDKCore PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/RLSDK
    ${RLINFERENCE_DIR}/include
    ${VIGEM_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/Detours
    ${CMAKE_CURRENT_SOURCE_DIR}/ImGui/Kiero/minhook
)


target_link_libraries(GoySDKCore PRIVATE
    RLInference
    ${TORCH_LIBRARIES}
    ViGEmClient
)


target_link_directories(GoySDKCore PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Detours)
target_link_libraries(GoySDKCore PRIVATE detours.lib)


target_link_directories(GoySDKCore PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/ImGui/Kiero/minhook)
target_link_libraries(GoySDKCore PRIVATE
    $<IF:$<CONFIG:Debug>,minhook.x64d.lib,minhook.x64.lib>
)


target_link_libraries(GoySDKCore PRIVATE psapi.lib setupapi.lib)


if(MSVC)
    target_compile_definitions(GoySDKCore PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_options(GoySDKCore PRIVATE /wd4267 /wd4244 /wd4005)

    target_compile_options(GoySDKCore PRIVATE /EHa)
endif()


if(MSVC)
    file(GLOB TORCH_DLLS "${TORCH_INSTALL_PREFIX}/lib/*.dll")
    foreach(dll ${TORCH_DLLS})
        add_custom_command(TARGET GoySDKCore POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${dll} $<TARGET_FILE_DIR:GoySDKCore>)
    endforeach()
endif()




add_library(GoySDK SHARED Loader.cpp)
target_link_libraries(GoySDK PRIVATE kernel32.lib)
if(MSVC)
    target_compile_options(GoySDK PRIVATE /wd4267 /wd4244)
endif()


set_target_properties(GoySDK PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY $<TARGET_FILE_DIR:GoySDKCore>
    LIBRARY_OUTPUT_DIRECTORY $<TARGET_FILE_DIR:GoySDKCore>
)
