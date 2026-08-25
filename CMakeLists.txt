cmake_minimum_required(VERSION 3.15)

project(sekai-deck-recommend-cpp VERSION 0.4.2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(SEKAI_DECK_RECOMMEND_BUILD_PYTHON "Build the pybind11 extension" ON)
option(SEKAI_DECK_RECOMMEND_BUILD_NATIVE "Build the native C++ library" OFF)

include_directories(${PROJECT_SOURCE_DIR}/src)
include_directories(${PROJECT_SOURCE_DIR}/3rdparty/json/single_include)

file(GLOB_RECURSE SRC_FILES CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/src/*.cpp")

if (EMSCRIPTEN)
    set(SEKAI_DECK_RECOMMEND_WASM_OUTPUT_DIR "" CACHE PATH
        "Optional directory that receives the generated WASM browser assets")
    add_executable(sekai_deck_recommend_wasm ${SRC_FILES})
    set_target_properties(sekai_deck_recommend_wasm PROPERTIES
        OUTPUT_NAME "sekai_deck_recommend_wasm"
        SUFFIX ".js"
    )
    target_compile_options(sekai_deck_recommend_wasm PRIVATE
        -Wall
        -Wextra
        -Wuninitialized
        -fexceptions
        -flto
    )
    target_link_options(sekai_deck_recommend_wasm PRIVATE
        --bind
        -fexceptions
        -flto
        "-sALLOW_MEMORY_GROWTH=1"
        "-sDISABLE_EXCEPTION_CATCHING=0"
        "-sENVIRONMENT=web,worker"
        "-sEXPORT_ES6=1"
        "-sEXPORT_NAME=createSekaiDeckRecommendModule"
        "-sFILESYSTEM=1"
        "-sINITIAL_MEMORY=268435456"
        "-sMODULARIZE=1"
        "--preload-file=${PROJECT_SOURCE_DIR}/data@/data"
    )
    if (SEKAI_DECK_RECOMMEND_WASM_OUTPUT_DIR)
        add_custom_command(TARGET sekai_deck_recommend_wasm POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "${SEKAI_DECK_RECOMMEND_WASM_OUTPUT_DIR}"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:sekai_deck_recommend_wasm>"
                "${SEKAI_DECK_RECOMMEND_WASM_OUTPUT_DIR}/sekai_deck_recommend_wasm.js"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE_DIR:sekai_deck_recommend_wasm>/sekai_deck_recommend_wasm.wasm"
                "${SEKAI_DECK_RECOMMEND_WASM_OUTPUT_DIR}/sekai_deck_recommend_wasm.wasm"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE_DIR:sekai_deck_recommend_wasm>/sekai_deck_recommend_wasm.data"
                "${SEKAI_DECK_RECOMMEND_WASM_OUTPUT_DIR}/sekai_deck_recommend_wasm.data"
        )
    endif()
else()
    if (SEKAI_DECK_RECOMMEND_BUILD_NATIVE)
        add_library(sekai_deck_recommend_core STATIC ${SRC_FILES})
        add_library(SekaiDeckRecommend::Core ALIAS sekai_deck_recommend_core)
        set_target_properties(sekai_deck_recommend_core PROPERTIES
            POSITION_INDEPENDENT_CODE ON
            EXPORT_NAME Core
        )
        target_include_directories(sekai_deck_recommend_core
            PUBLIC
                $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
                $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/3rdparty/json/single_include>
                $<INSTALL_INTERFACE:include>
            PRIVATE
                ${PROJECT_SOURCE_DIR}/src
        )

        if (MSVC)
            target_compile_options(sekai_deck_recommend_core PRIVATE /W4 /MP /utf-8)
        else()
            target_compile_options(sekai_deck_recommend_core PRIVATE -Wall -Wextra -Wuninitialized)
        endif()
    endif()

    if (SEKAI_DECK_RECOMMEND_BUILD_PYTHON)
        add_subdirectory(3rdparty/pybind11)

        pybind11_add_module(sekai_deck_recommend ${SRC_FILES})
        target_compile_definitions(sekai_deck_recommend PRIVATE SEKAI_DECK_RECOMMEND_PYTHON=1)

        if (MSVC)
            target_compile_options(sekai_deck_recommend PRIVATE
                /W4
                /MP
                /utf-8
                /Z7
            )
            target_link_options(sekai_deck_recommend PRIVATE
                /DEBUG:NONE
            )
        else()
            target_compile_options(sekai_deck_recommend PRIVATE
                -Wall -Wextra -Wuninitialized
            )
        endif()

        set(INSTALL_DIR "sekai_deck_recommend_cpp")

        install(TARGETS sekai_deck_recommend DESTINATION ${INSTALL_DIR})
        install(DIRECTORY "${PROJECT_SOURCE_DIR}/data" DESTINATION ${INSTALL_DIR})
        install(FILES "${PROJECT_SOURCE_DIR}/src/sekai_deck_recommend.pyi" DESTINATION ${INSTALL_DIR})
        install(FILES "${PROJECT_SOURCE_DIR}/src/__init__.py" DESTINATION ${INSTALL_DIR})
    endif()
endif()
