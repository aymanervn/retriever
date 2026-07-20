cmake_minimum_required(VERSION 3.16)

file(STRINGS include/retriever.h _rtv_ver_line REGEX "#define RTV_VERSION")
string(REGEX MATCH "[0-9]+\\.[0-9]+\\.[0-9]+" RTV_VER "${_rtv_ver_line}")

project(retriever VERSION ${RTV_VER} LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

option(RTV_WERROR "Treat warnings as errors" OFF)

function(rtv_strict tgt)
  if(MSVC AND NOT CMAKE_C_COMPILER_ID MATCHES "Clang")
    target_compile_options(${tgt} PRIVATE /W4 $<$<BOOL:${RTV_WERROR}>:/WX>)
  else()
    target_compile_options(${tgt} PRIVATE -Wall -Wextra $<$<BOOL:${RTV_WERROR}>:-Werror>)
  endif()
  if(WIN32)
    target_compile_definitions(${tgt} PRIVATE _CRT_SECURE_NO_WARNINGS)
  endif()
endfunction()

add_library(rtv_plat STATIC src/common/plat.c)
rtv_strict(rtv_plat)

add_library(rtv_core STATIC
    src/core/index.c
    src/core/search.c
    src/core/re.c
)
target_link_libraries(rtv_core PUBLIC rtv_plat)
rtv_strict(rtv_core)

set(RTV_SVC_SOURCES
    src/svc/main.c
    src/svc/ipc_server.c
)
if(WIN32)
  list(APPEND RTV_SVC_SOURCES src/svc/vol_win.c src/svc/tray_win.c)
endif()
add_executable(retriever ${RTV_SVC_SOURCES})
target_link_libraries(retriever PRIVATE rtv_core)
if(WIN32)
  set_target_properties(retriever PROPERTIES WIN32_EXECUTABLE TRUE)
  target_link_libraries(retriever PRIVATE user32 gdi32 shell32)
endif()
rtv_strict(retriever)

add_executable(rtv src/cli/main.c src/cli/ipc_client.c)
rtv_strict(rtv)

if(WIN32)
  include(CheckLanguage)
  check_language(RC)
  if(CMAKE_RC_COMPILER)
    enable_language(RC)
    set(RTV_RC_ICON "${CMAKE_CURRENT_SOURCE_DIR}/resources/retriever.ico")
    file(TO_CMAKE_PATH "${RTV_RC_ICON}" RTV_RC_ICON)
    set(RTV_RC_VER "${RTV_VER}")
    string(REPLACE "." "," RTV_RC_VER_COMMA "${RTV_VER}.0")

    set(RTV_RC_NAME "retriever")
    set(RTV_RC_DESC "retriever filename search service")
    configure_file(resources/version.rc.in ${CMAKE_BINARY_DIR}/rc/svc.rc @ONLY)
    target_sources(retriever PRIVATE ${CMAKE_BINARY_DIR}/rc/svc.rc)
    # UAC: raw-volume reads need elevation, so the service manifest demands it
    # up front instead of failing at open time. Linker flag (not a .manifest
    # source) because mt.exe refuses to merge a level= that conflicts with the
    # generator's default asInvoker fragment; the last /MANIFESTUAC wins.
    target_link_options(retriever PRIVATE "LINKER:/MANIFESTUAC:level='requireAdministrator'")

    set(RTV_RC_NAME "rtv")
    set(RTV_RC_DESC "retriever search client")
    configure_file(resources/version.rc.in ${CMAKE_BINARY_DIR}/rc/cli.rc @ONLY)
    target_sources(rtv PRIVATE ${CMAKE_BINARY_DIR}/rc/cli.rc)
  endif()
endif()

install(TARGETS retriever rtv RUNTIME DESTINATION .)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/README.md
        ${CMAKE_CURRENT_SOURCE_DIR}/include/retriever.h DESTINATION .)

enable_testing()
add_executable(re_test tests/re_test.c)
target_include_directories(re_test PRIVATE src/core)
target_link_libraries(re_test PRIVATE rtv_core)
rtv_strict(re_test)
add_test(NAME re COMMAND re_test)

set(CPACK_PACKAGE_NAME retriever)
set(CPACK_PACKAGE_VERSION ${RTV_VER})
set(CPACK_GENERATOR ZIP)
set(CPACK_PACKAGE_FILE_NAME "retriever-v${RTV_VER}-windows-x86_64")
include(CPack)
