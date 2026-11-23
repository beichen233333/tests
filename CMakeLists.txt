cmake_minimum_required(VERSION 3.22)
project(AstraeOfflineBackend VERSION 1.0.0 LANGUAGES C CXX)
include(CTest)

if(ANDROID)
    set(ASTRAE_ANDROID_RUNTIME_DEFAULT ON)
    set(ASTRAE_ANDROID_TOUCH_DEFAULT ON)
    set(ASTRAE_ANDROID_REQUEST_DEFAULT ON)
else()
    set(ASTRAE_ANDROID_RUNTIME_DEFAULT OFF)
    set(ASTRAE_ANDROID_TOUCH_DEFAULT OFF)
    set(ASTRAE_ANDROID_REQUEST_DEFAULT OFF)
endif()
option(ASTRAE_ENABLE_RUNTIME_AUTOSTART "Start the delayed Android runtime worker after the library is loaded" ${ASTRAE_ANDROID_RUNTIME_DEFAULT})
option(ASTRAE_ENABLE_TOUCH_DIRECT_HOOK "Enable the Phase-1 lobby entry Hook #3" ${ASTRAE_ANDROID_TOUCH_DEFAULT})
option(ASTRAE_ENABLE_LOCAL_REQUEST_DIRECT_HOOK "Enable Hook #5 local request interception required before Hook #3 can safely fall back to original touch" ${ASTRAE_ANDROID_REQUEST_DEFAULT})
option(ASTRAE_ENABLE_SHADOWHOOK "Use ByteDance ShadowHook for Android inline hooks" ON)
if(ANDROID AND ASTRAE_ENABLE_TOUCH_DIRECT_HOOK AND NOT ASTRAE_ENABLE_LOCAL_REQUEST_DIRECT_HOOK)
    message(FATAL_ERROR "Unsafe Android profile: Hook #3 replaces the login touch handler, so Hook #5 local request interception must be enabled before Hook #3 can forward to the original game path. Build Hook #5 or disable Hook #3 explicitly.")
endif()
set(ASTRAE_SHADOWHOOK_SOURCE_DIR "" CACHE PATH "Optional path to a checked out bytedance/android-inline-hook source tree (v2.0.1)")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)
find_package(ZLIB REQUIRED)

# Content-derived identity covers all executable sources, build scripts, schemas,
# rules and embedded tables. A change outside lobby_restore.cpp must also produce
# a different ID, so device logs can reliably reject stale 13-MB binaries.
execute_process(COMMAND git rev-parse --short=12 HEAD WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" OUTPUT_VARIABLE ASTRAE_GIT_REV OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if(NOT ASTRAE_GIT_REV)
    set(ASTRAE_GIT_REV "nogit")
endif()
file(GLOB_RECURSE ASTRAE_BUILD_ID_FILES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/*"
    "${CMAKE_CURRENT_SOURCE_DIR}/compat/*"
    "${CMAKE_CURRENT_SOURCE_DIR}/android/*"
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/*"
    "${CMAKE_CURRENT_SOURCE_DIR}/scripts/*"
)
if(ASTRAE_SHADOWHOOK_SOURCE_DIR AND EXISTS "${ASTRAE_SHADOWHOOK_SOURCE_DIR}/shadowhook/src/main/cpp")
    file(GLOB_RECURSE ASTRAE_SHADOWHOOK_ID_FILES CONFIGURE_DEPENDS
        "${ASTRAE_SHADOWHOOK_SOURCE_DIR}/shadowhook/src/main/cpp/*.c"
        "${ASTRAE_SHADOWHOOK_SOURCE_DIR}/shadowhook/src/main/cpp/*.h"
        "${ASTRAE_SHADOWHOOK_SOURCE_DIR}/shadowhook/src/main/cpp/*.S"
    )
    list(APPEND ASTRAE_BUILD_ID_FILES ${ASTRAE_SHADOWHOOK_ID_FILES})
endif()
list(APPEND ASTRAE_BUILD_ID_FILES "${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt")
list(SORT ASTRAE_BUILD_ID_FILES)
set(ASTRAE_ID_MATERIAL "rev=${ASTRAE_GIT_REV};type=${CMAKE_BUILD_TYPE};abi=${ANDROID_ABI};platform=${ANDROID_PLATFORM};touch=${ASTRAE_ENABLE_TOUCH_DIRECT_HOOK};request=${ASTRAE_ENABLE_LOCAL_REQUEST_DIRECT_HOOK};runtime=${ASTRAE_ENABLE_RUNTIME_AUTOSTART};shadowhook=${ASTRAE_ENABLE_SHADOWHOOK}\n")
foreach(ASTRAE_ID_FILE IN LISTS ASTRAE_BUILD_ID_FILES)
    if(EXISTS "${ASTRAE_ID_FILE}" AND NOT IS_DIRECTORY "${ASTRAE_ID_FILE}")
        file(SHA256 "${ASTRAE_ID_FILE}" ASTRAE_FILE_SHA256)
        file(RELATIVE_PATH ASTRAE_RELATIVE_FILE "${CMAKE_CURRENT_SOURCE_DIR}" "${ASTRAE_ID_FILE}")
        string(APPEND ASTRAE_ID_MATERIAL "${ASTRAE_RELATIVE_FILE}:${ASTRAE_FILE_SHA256}\n")
    endif()
endforeach()
string(SHA256 ASTRAE_PROJECT_SHA256 "${ASTRAE_ID_MATERIAL}")
string(SUBSTRING "${ASTRAE_PROJECT_SHA256}" 0 16 ASTRAE_PROJECT_HASH)
set(ASTRAE_BUILD_ID "${ASTRAE_GIT_REV}-${ASTRAE_PROJECT_HASH}")
message(STATUS "AstraeOffline build identity: ${ASTRAE_BUILD_ID}")

set(ASTRAE_CORE_SOURCES
    src/api.cpp
    src/backend.cpp
    src/json.cpp
    src/embedded_assets.cpp
)
set(ASTRAE_RUNTIME_SOURCES
    src/runtime_bootstrap.cpp
    src/debug_log.cpp
    src/il2cpp_runtime.cpp
    src/managed_wire.cpp
    src/lobby_restore.cpp
    src/character99_restore.cpp
    src/hook_runtime.cpp
)
set(ASTRAE_COMPAT_SOURCES
    compat/src/cJSON_compat.cpp
    compat/src/store_core.cpp
    compat/src/original_store_abi.cpp
    compat/src/errno_compat.cpp
)

# Hooking is opt-in. A passive/runtime-only build must not initialize any inline-hook engine.
# When a hook is enabled, use the upstream ByteDance ShadowHook implementation (pinned v2.0.1).
set(ASTRAE_NEEDS_HOOK_ENGINE OFF)
if(ANDROID AND (ASTRAE_ENABLE_TOUCH_DIRECT_HOOK OR ASTRAE_ENABLE_LOCAL_REQUEST_DIRECT_HOOK))
    set(ASTRAE_NEEDS_HOOK_ENGINE ON)
    if(NOT ASTRAE_ENABLE_RUNTIME_AUTOSTART)
        message(FATAL_ERROR "Direct hooks require ASTRAE_ENABLE_RUNTIME_AUTOSTART=ON")
    endif()
    if(NOT ASTRAE_ENABLE_SHADOWHOOK)
        message(FATAL_ERROR "Direct hooks require ASTRAE_ENABLE_SHADOWHOOK=ON")
    endif()
    if(NOT ASTRAE_SHADOWHOOK_SOURCE_DIR OR NOT EXISTS "${ASTRAE_SHADOWHOOK_SOURCE_DIR}/shadowhook/src/main/cpp/include/shadowhook.h")
        message(FATAL_ERROR "ShadowHook v2.0.1 source not found. Run `bash scripts/fetch_shadowhook.sh` or set ASTRAE_SHADOWHOOK_SOURCE_DIR.")
    endif()
    set(ASTRAE_SHADOWHOOK_CPP_DIR "${ASTRAE_SHADOWHOOK_SOURCE_DIR}/shadowhook/src/main/cpp")
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    execute_process(
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/patch_shadowhook.py" "${ASTRAE_SHADOWHOOK_SOURCE_DIR}"
        RESULT_VARIABLE ASTRAE_SHADOWHOOK_PATCH_RESULT
    )
    if(NOT ASTRAE_SHADOWHOOK_PATCH_RESULT EQUAL 0)
        message(FATAL_ERROR "Failed to apply the ShadowHook direct-address-only patch")
    endif()
    file(GLOB ASTRAE_SHADOWHOOK_SOURCES CONFIGURE_DEPENDS
        "${ASTRAE_SHADOWHOOK_CPP_DIR}/*.c"
        "${ASTRAE_SHADOWHOOK_CPP_DIR}/arch/arm64/*.c"
        "${ASTRAE_SHADOWHOOK_CPP_DIR}/arch/arm64/*.S"
        "${ASTRAE_SHADOWHOOK_CPP_DIR}/common/*.c"
        "${ASTRAE_SHADOWHOOK_CPP_DIR}/third_party/*/*.c"
    )
    if(NOT ASTRAE_SHADOWHOOK_SOURCES)
        message(FATAL_ERROR "ShadowHook source file list is empty")
    endif()
    enable_language(ASM)
    add_library(astrae_shadowhook STATIC ${ASTRAE_SHADOWHOOK_SOURCES})
    target_compile_features(astrae_shadowhook PRIVATE c_std_11)
    target_include_directories(astrae_shadowhook PUBLIC "${ASTRAE_SHADOWHOOK_CPP_DIR}/include" PRIVATE
        "${ASTRAE_SHADOWHOOK_CPP_DIR}"
        "${ASTRAE_SHADOWHOOK_CPP_DIR}/arch/arm64"
        "${ASTRAE_SHADOWHOOK_CPP_DIR}/common"
        "${ASTRAE_SHADOWHOOK_CPP_DIR}/third_party/xdl"
        "${ASTRAE_SHADOWHOOK_CPP_DIR}/third_party/bsd"
        "${ASTRAE_SHADOWHOOK_CPP_DIR}/third_party/lss"
    )
    target_compile_options(astrae_shadowhook PRIVATE $<$<CONFIG:Release>:-Oz> -ffunction-sections -fdata-sections)
    target_link_libraries(astrae_shadowhook PRIVATE log)
    set_target_properties(astrae_shadowhook PROPERTIES POSITION_INDEPENDENT_CODE ON C_VISIBILITY_PRESET hidden)

    # This project hooks already-loaded libil2cpp functions by absolute address.
    # Disable ShadowHook's private linker/soinfo monitor and let sh_task_do()
    # resolve the containing ELF metadata via xDL for each direct target.
    target_compile_definitions(astrae_shadowhook PRIVATE ASTRAE_SHADOWHOOK_DIRECT_ADDRESS_ONLY=1)

endif()

add_library(astraeoffline_objects OBJECT
    ${ASTRAE_CORE_SOURCES}
    ${ASTRAE_RUNTIME_SOURCES}
    ${ASTRAE_COMPAT_SOURCES}
)

target_include_directories(astraeoffline_objects
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/compat/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)
if(ASTRAE_NEEDS_HOOK_ENGINE)
    target_include_directories(astraeoffline_objects PRIVATE "${ASTRAE_SHADOWHOOK_CPP_DIR}/include")
endif()

target_compile_definitions(astraeoffline_objects PRIVATE ASTRAE_BACKEND_BUILD=1 ASTRAE_BUILD_ID=\"${ASTRAE_BUILD_ID}\")
if(ANDROID AND ASTRAE_ENABLE_RUNTIME_AUTOSTART)
    target_compile_definitions(astraeoffline_objects PRIVATE ASTRAE_ENABLE_RUNTIME_AUTOSTART=1)
endif()
if(ASTRAE_NEEDS_HOOK_ENGINE)
    target_compile_definitions(astraeoffline_objects PRIVATE ASTRAE_USE_SHADOWHOOK=1)
endif()
if(ANDROID AND ASTRAE_ENABLE_TOUCH_DIRECT_HOOK)
    target_compile_definitions(astraeoffline_objects PRIVATE ASTRAE_ENABLE_TOUCH_DIRECT_HOOK=1)
endif()
if(ANDROID AND ASTRAE_ENABLE_LOCAL_REQUEST_DIRECT_HOOK)
    target_compile_definitions(astraeoffline_objects PRIVATE ASTRAE_ENABLE_LOCAL_REQUEST_DIRECT_HOOK=1)
endif()
set_target_properties(astraeoffline_objects PROPERTIES POSITION_INDEPENDENT_CODE ON C_VISIBILITY_PRESET hidden CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN YES)
if(ANDROID)
    target_compile_options(astraeoffline_objects PRIVATE $<$<CONFIG:Release>:-Oz> -ffunction-sections -fdata-sections)
endif()

if(NOT ANDROID)
    target_compile_options(astraeoffline_objects PRIVATE -Wall -Wextra -Wpedantic -Wno-misleading-indentation)
endif()

if(BUILD_TESTING AND NOT ANDROID)
    add_executable(permanent_raid_tables_test tests/permanent_raid_tables_test.cpp $<TARGET_OBJECTS:astraeoffline_objects>)
    target_include_directories(permanent_raid_tables_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/include ${CMAKE_CURRENT_SOURCE_DIR}/compat/include)
    target_link_libraries(permanent_raid_tables_test PRIVATE Threads::Threads ${CMAKE_DL_LIBS} ZLIB::ZLIB)
    target_compile_options(permanent_raid_tables_test PRIVATE -UNDEBUG)
    add_test(NAME permanent_raid_tables COMMAND permanent_raid_tables_test)
    add_executable(artifact_capacity_tables_test tests/artifact_capacity_tables_test.cpp $<TARGET_OBJECTS:astraeoffline_objects>)
    target_include_directories(artifact_capacity_tables_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/include ${CMAKE_CURRENT_SOURCE_DIR}/compat/include)
    target_link_libraries(artifact_capacity_tables_test PRIVATE Threads::Threads ${CMAKE_DL_LIBS} ZLIB::ZLIB)
    target_compile_options(artifact_capacity_tables_test PRIVATE -UNDEBUG)
    add_test(NAME artifact_capacity_tables COMMAND artifact_capacity_tables_test)
    add_executable(character99_asset_test tests/character99_asset_test.cpp $<TARGET_OBJECTS:astraeoffline_objects>)
    target_include_directories(character99_asset_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/include ${CMAKE_CURRENT_SOURCE_DIR}/compat/include)
    target_link_libraries(character99_asset_test PRIVATE Threads::Threads ${CMAKE_DL_LIBS} ZLIB::ZLIB)
    target_compile_options(character99_asset_test PRIVATE -UNDEBUG)
    add_test(NAME character99_progression_asset COMMAND character99_asset_test)
    add_executable(character99_object_graph_test tests/character99_object_graph_test.cpp)
    target_include_directories(character99_object_graph_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_options(character99_object_graph_test PRIVATE -UNDEBUG)
    add_test(NAME character99_object_graph COMMAND character99_object_graph_test)
    add_executable(managed_class_describe_test tests/managed_class_describe_test.cpp src/il2cpp_runtime.cpp src/debug_log.cpp)
    target_include_directories(managed_class_describe_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(managed_class_describe_test PRIVATE Threads::Threads ${CMAKE_DL_LIBS})
    target_compile_options(managed_class_describe_test PRIVATE -UNDEBUG)
    add_test(NAME managed_class_metadata COMMAND managed_class_describe_test)
    add_executable(touch_fallback_policy_test tests/touch_fallback_policy_test.cpp)
    target_include_directories(touch_fallback_policy_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_options(touch_fallback_policy_test PRIVATE -UNDEBUG)
    add_test(NAME touch_fallback_policy COMMAND touch_fallback_policy_test)
    add_executable(legacy_save_migration_test tests/legacy_save_migration_test.cpp src/json.cpp)
    target_include_directories(legacy_save_migration_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_options(legacy_save_migration_test PRIVATE -UNDEBUG)
    add_test(NAME legacy_save_migration COMMAND legacy_save_migration_test)
    add_executable(character99_fromxml_safety_policy_test tests/character99_fromxml_safety_policy_test.cpp)
    target_include_directories(character99_fromxml_safety_policy_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_options(character99_fromxml_safety_policy_test PRIVATE -UNDEBUG)
    add_test(NAME character99_fromxml_safety_policy COMMAND character99_fromxml_safety_policy_test)
    add_executable(backend_legacy_save_integration_test tests/backend_legacy_save_integration_test.cpp $<TARGET_OBJECTS:astraeoffline_objects>)
    target_include_directories(backend_legacy_save_integration_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/include ${CMAKE_CURRENT_SOURCE_DIR}/compat/include)
    target_link_libraries(backend_legacy_save_integration_test PRIVATE Threads::Threads ${CMAKE_DL_LIBS} ZLIB::ZLIB)
    target_compile_options(backend_legacy_save_integration_test PRIVATE -UNDEBUG)
    add_test(NAME backend_legacy_save_integration COMMAND backend_legacy_save_integration_test)
endif()

add_library(astraeoffline SHARED $<TARGET_OBJECTS:astraeoffline_objects>)
target_include_directories(astraeoffline PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/compat/include)
target_link_libraries(astraeoffline PRIVATE Threads::Threads ${CMAKE_DL_LIBS} ZLIB::ZLIB)
if(ASTRAE_NEEDS_HOOK_ENGINE)
    target_link_libraries(astraeoffline PRIVATE astrae_shadowhook log)
    target_link_options(astraeoffline PRIVATE
        "LINKER:--exclude-libs,ALL"
        # Reject unresolved references in the final single shared library.
        "LINKER:-z,defs"
    )
    # ShadowHook itself is statically linked; no helper SO is required.
endif()
target_link_options(astraeoffline PRIVATE
    "LINKER:--version-script=${CMAKE_CURRENT_SOURCE_DIR}/compat/original_exports.map"
)
set_target_properties(astraeoffline PROPERTIES
    OUTPUT_NAME astraeoffline
    C_VISIBILITY_PRESET hidden
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
)
if(ANDROID)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT ASTRAE_IPO_SUPPORTED OUTPUT ASTRAE_IPO_ERROR LANGUAGES C CXX)
    if(ASTRAE_IPO_SUPPORTED)
        set_property(TARGET astraeoffline_objects PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
        set_property(TARGET astraeoffline PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
        if(ASTRAE_NEEDS_HOOK_ENGINE)
            set_property(TARGET astrae_shadowhook PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
        endif()
        message(STATUS "AstraeOffline Release LTO enabled")
    else()
        message(WARNING "AstraeOffline Release LTO unavailable: ${ASTRAE_IPO_ERROR}")
    endif()
endif()

if(ANDROID)
    # Keep the original Android dependency surface and support 16 KiB page devices.
    target_link_options(astraeoffline PRIVATE "LINKER:--no-as-needed" "LINKER:-z,max-page-size=16384" "LINKER:--gc-sections" "LINKER:--icf=safe")
    target_link_libraries(astraeoffline PRIVATE log)
else()
    set_target_properties(astraeoffline PROPERTIES BUILD_RPATH "$ORIGIN")
endif()

install(TARGETS astraeoffline LIBRARY DESTINATION lib)
