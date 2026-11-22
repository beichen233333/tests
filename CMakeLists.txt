cmake_minimum_required(VERSION 3.22)
project(AstraeOfflineBackend VERSION 1.0.0 LANGUAGES C CXX)

option(ASTRAE_ENABLE_RUNTIME_AUTOSTART "Start the Android runtime worker and install lobby hooks" ON)
option(ASTRAE_ENABLE_LOCAL_REQUEST_DIRECT_HOOK "Enable the partial offline Hook #5 request replacement" ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)

set(ASTRAE_CORE_SOURCES
    src/api.cpp
    src/backend.cpp
    src/json.cpp
    src/embedded_assets.cpp
)

set(ASTRAE_RUNTIME_SOURCES
    src/runtime_bootstrap.cpp
    src/il2cpp_runtime.cpp
    src/managed_wire.cpp
    src/lobby_restore.cpp
    src/arm64_hook_engine.cpp
    src/hook_runtime.cpp
)

set(ASTRAE_COMPAT_SOURCES
    compat/src/cJSON_compat.cpp
    compat/src/store_core.cpp
    compat/src/original_store_abi.cpp
    compat/src/errno_compat.cpp
)

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

target_compile_definitions(astraeoffline_objects PRIVATE ASTRAE_BACKEND_BUILD=1)
if(ANDROID AND ASTRAE_ENABLE_RUNTIME_AUTOSTART)
    target_compile_definitions(astraeoffline_objects PRIVATE ASTRAE_ENABLE_RUNTIME_AUTOSTART=1)
endif()
if(ANDROID AND ASTRAE_ENABLE_LOCAL_REQUEST_DIRECT_HOOK)
    target_compile_definitions(astraeoffline_objects PRIVATE ASTRAE_ENABLE_LOCAL_REQUEST_DIRECT_HOOK=1)
endif()
set_target_properties(astraeoffline_objects PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(NOT ANDROID)
    target_compile_options(astraeoffline_objects PRIVATE -Wall -Wextra -Wpedantic -Wno-misleading-indentation)
endif()

add_library(astraeoffline SHARED $<TARGET_OBJECTS:astraeoffline_objects>)
target_include_directories(astraeoffline PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/compat/include)
target_link_libraries(astraeoffline PRIVATE Threads::Threads ${CMAKE_DL_LIBS})
target_link_options(astraeoffline PRIVATE
    "LINKER:--version-script=${CMAKE_CURRENT_SOURCE_DIR}/compat/original_exports.map"
)
set_target_properties(astraeoffline PROPERTIES
    OUTPUT_NAME astraeoffline
)

if(ANDROID)
    # Keep the original Android dependency surface and support 16 KiB page devices.
    target_link_options(astraeoffline PRIVATE "LINKER:--no-as-needed" "LINKER:-z,max-page-size=16384")
    target_link_libraries(astraeoffline PRIVATE log)
else()
    set_target_properties(astraeoffline PROPERTIES BUILD_RPATH "$ORIGIN")
endif()

install(TARGETS astraeoffline LIBRARY DESTINATION lib)
