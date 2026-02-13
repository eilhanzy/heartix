cmake_minimum_required(VERSION 3.20)
project(GhostOS LANGUAGES C CXX ASM ASM_NASM)

# Options
option(GHOST_BUILD_LIBAPI "Build libapi" ON)
option(GHOST_BUILD_LIBC   "Build libc" ON)
option(GHOST_BUILD_KERNEL "Build kernel" ON)
option(GHOST_BUILD_APPS   "Build applications via legacy scripts" ON)
option(GHOST_BUILD_PORTS  "Build ports (zlib, pixman, libpng, freetype, cairo)" ON)
option(GHOST_ENABLE_PACK  "Enable pack target to produce ISO via scripts" ON)

# Pack should always build ports; force-enable ports when packing.
if(GHOST_ENABLE_PACK AND NOT GHOST_BUILD_PORTS)
  set(GHOST_BUILD_PORTS ON CACHE BOOL "Build ports (zlib, pixman, libpng, freetype, cairo)" FORCE)
endif()
if(GHOST_ENABLE_PACK AND NOT GHOST_BUILD_KERNEL)
  message(FATAL_ERROR "GHOST_ENABLE_PACK requires GHOST_BUILD_KERNEL=ON")
endif()

find_program(GHOST_PACMAN_EXECUTABLE pacman)

function(ghost_root_tool_install_hint out_var tool)
  if(APPLE)
    set(_hint "brew install ${tool}")
  elseif(GHOST_PACMAN_EXECUTABLE)
    set(_hint "sudo pacman -S ${tool}")
  else()
    set(_hint "install '${tool}' with your package manager")
  endif()
  set(${out_var} "${_hint}" PARENT_SCOPE)
endfunction()

# Defaults similar to ghost.sh
set(TARGET_TRIPLE "${TARGET_TRIPLE}" CACHE STRING "Target triple (e.g. x86_64-ghost)")
if(NOT TARGET_TRIPLE)
  set(TARGET_TRIPLE x86_64-ghost)
endif()
set(TOOLCHAIN_BASE "${TOOLCHAIN_BASE}" CACHE PATH "Ghost toolchain base path")
if(NOT TOOLCHAIN_BASE)
  # Use a local path to avoid writing to '/'
  set(TOOLCHAIN_BASE ${CMAKE_BINARY_DIR}/toolchain)
endif()
set(SYSROOT "${SYSROOT}" CACHE PATH "Ghost sysroot path")
if(NOT SYSROOT)
  # Default sysroot inside build tree (safe, non-root)
  set(SYSROOT ${CMAKE_BINARY_DIR}/sysroot)
endif()
file(MAKE_DIRECTORY ${SYSROOT}/system/lib)
file(MAKE_DIRECTORY ${SYSROOT}/system/include)
if(NOT EXISTS ${SYSROOT}/lib)
  execute_process(COMMAND ${CMAKE_COMMAND} -E create_symlink system/lib ${SYSROOT}/lib)
endif()

add_custom_target(sysroot-base ALL
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/sysroot ${SYSROOT}
  COMMENT "Syncing source sysroot/ into ${SYSROOT}")

set(PORTS_PKG_CONFIG ${CMAKE_BINARY_DIR}/ports-pkg-config.sh)
configure_file(${CMAKE_SOURCE_DIR}/cmake/ports-pkg-config.sh.in ${PORTS_PKG_CONFIG} @ONLY)
file(CHMOD ${PORTS_PKG_CONFIG} PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)

# Common compile settings
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_EXTENSIONS OFF)

# Handy function for header installation into sysroot
function(ghost_install_headers hdr_dir)
  add_custom_target(install-headers-${hdr_dir}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${SYSROOT}/system/include
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${hdr_dir} ${SYSROOT}/system/include
    BYPRODUCTS ${SYSROOT}/system/include
    COMMENT "Installing headers from ${hdr_dir} to ${SYSROOT}/system/include")
endfunction()

# Subdirectories
if(GHOST_BUILD_LIBAPI)
  add_subdirectory(libapi)
endif()
if(GHOST_BUILD_LIBC)
  add_subdirectory(libc)
endif()
if(GHOST_BUILD_KERNEL)
  add_subdirectory(kernel)
endif()

# Applications via CMake (automatic conversion)
if(GHOST_BUILD_APPS)
  add_subdirectory(applications)
endif()

# Ports wrapper (optional, disabled by default). We keep this as a script-driven
# step but fully managed by CMake and safe env paths.
if(GHOST_BUILD_PORTS)
  set(_ports_env
    SYSROOT=${SYSROOT}
    TOOLCHAIN_BASE=${TOOLCHAIN_BASE}
    PKG_CONFIG=${PORTS_PKG_CONFIG}
    PKG_CONFIG_SYSROOT_DIR=${SYSROOT}
    PKG_CONFIG_LIBDIR=${SYSROOT}/system/lib/pkgconfig
    PKG_CONFIG_PATH=${SYSROOT}/system/lib/pkgconfig
    CFLAGS=--sysroot=${SYSROOT}\ -I${SYSROOT}/system/include
    CXXFLAGS=--sysroot=${SYSROOT}\ -I${SYSROOT}/system/include
    CPPFLAGS=-I${SYSROOT}/system/include
    LDFLAGS=--sysroot=${SYSROOT}\ -L${SYSROOT}/system/lib\ -Wl,-rpath-link,${SYSROOT}/system/lib
  )
  set(PORTS_STAMP ${CMAKE_BINARY_DIR}/.ports-built)
  add_custom_command(
    OUTPUT ${PORTS_STAMP}
      COMMAND ${CMAKE_COMMAND} -E env ${_ports_env} bash ${CMAKE_SOURCE_DIR}/patches/ports/build-all.sh
      COMMAND ${CMAKE_COMMAND} -E touch ${PORTS_STAMP}
    USES_TERMINAL
    COMMENT "Building ports using existing scripts (env SYSROOT, TOOLCHAIN_BASE overridden)")
  add_custom_target(ports ALL DEPENDS ${PORTS_STAMP})
  add_custom_target(ports-clean
    COMMAND ${CMAKE_COMMAND} -E rm -f ${PORTS_STAMP}
    COMMENT "Removing ports build stamp to allow rebuilding ports")
  add_dependencies(ports sysroot-base)
  if(TARGET libc-install)
    add_dependencies(ports libc-install)
  endif()
  if(TARGET libapi-install)
    add_dependencies(ports libapi-install)
  endif()
endif()

# Target utilities (limine) are needed for kernel and ISO pack flows.
if(GHOST_BUILD_KERNEL OR GHOST_ENABLE_PACK)
  add_subdirectory(target)
endif()

# Pack/ISO wrapper
if(GHOST_ENABLE_PACK)
  find_program(XORRISO_EXECUTABLE xorriso)
  if(NOT XORRISO_EXECUTABLE)
    ghost_root_tool_install_hint(_xorriso_hint xorriso)
    message(FATAL_ERROR "xorriso is required to build ISO images.\nHint: ${_xorriso_hint}\nTip: configure with -DGHOST_ENABLE_PACK=OFF if you only want binaries/sysroot.")
  endif()
  add_custom_target(pack
    COMMAND ${CMAKE_COMMAND} -E env SH=bash SYSROOT=${SYSROOT} TOOLCHAIN_BASE=${TOOLCHAIN_BASE} KERNEL_BIN=$<TARGET_FILE:kernel> bash ${CMAKE_SOURCE_DIR}/target/build.sh pack
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/target
    USES_TERMINAL
    COMMENT "Packing ISO via target/build.sh (env SYSROOT, TOOLCHAIN_BASE, KERNEL_BIN overridden)")
  # Ensure kernel and libs are built before pack if enabled
  if(TARGET kernel)
    add_dependencies(pack kernel)
  endif()
  if(TARGET libc-install)
    add_dependencies(pack libc-install)
  endif()
  if(TARGET libapi-install)
    add_dependencies(pack libapi-install)
  endif()
  if(TARGET kernel-apstartup-install)
    add_dependencies(pack kernel-apstartup-install)
  endif()
  if(TARGET applications-install)
    add_dependencies(pack applications-install)
  endif()
  if(TARGET ports)
    add_dependencies(pack ports)
  endif()
  add_dependencies(pack sysroot-base)
  add_dependencies(pack limine)
endif()

message(STATUS "Target triple: ${TARGET_TRIPLE}")
message(STATUS "Sysroot: ${SYSROOT}")

if(TARGET_TRIPLE)
  set(_ghost_toolchain_file ${CMAKE_SOURCE_DIR}/cmake/toolchains/ghost-${TARGET_TRIPLE}.cmake)
  if(NOT EXISTS ${_ghost_toolchain_file})
    set(_ghost_toolchain_file "${CMAKE_SOURCE_DIR}/cmake/toolchains/ghost-x86_64.cmake")
  endif()
  set(_ghost_compiler_hint "-DCMAKE_TOOLCHAIN_FILE=${_ghost_toolchain_file} -DTARGET_TRIPLE=${TARGET_TRIPLE} -DTOOLCHAIN_BASE=\\$PWD/build-ghost/toolchain -DSYSROOT=\\$PWD/build-ghost/sysroot")
  if(CMAKE_C_COMPILER AND NOT CMAKE_C_COMPILER MATCHES "${TARGET_TRIPLE}-")
    message(FATAL_ERROR "Ghost must be built with the ${TARGET_TRIPLE} cross toolchain. Re-run CMake with the toolchain file, e.g.\n  cmake -S . -B build ${_ghost_compiler_hint}")
  endif()
  if(CMAKE_CXX_COMPILER AND NOT CMAKE_CXX_COMPILER MATCHES "${TARGET_TRIPLE}-")
    message(FATAL_ERROR "Ghost must be built with the ${TARGET_TRIPLE} cross toolchain. Re-run CMake with the toolchain file, e.g.\n  cmake -S . -B build ${_ghost_compiler_hint}")
  endif()
endif()

if(TARGET applications-libs)
  add_dependencies(applications-libs sysroot-base)
endif()
