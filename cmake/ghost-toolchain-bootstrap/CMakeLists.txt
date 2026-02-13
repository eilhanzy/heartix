# Ghost GCC cross toolchain bootstrap project (no compilers required)
# Purpose: builds the Ghost cross toolchain using the repository's toolchain.sh
# Usage:
#   cmake -S cmake/ghost-toolchain-bootstrap -B build-ghost-toolchain \
#     -DTARGET_TRIPLE=x86_64-ghost \
#     -DTOOLCHAIN_BASE=$PWD/build-ghost/toolchain \
#     -DSYSROOT=$PWD/build-ghost/sysroot
#   cmake --build build-ghost-toolchain --target ghost-toolchain

cmake_minimum_required(VERSION 3.20)
project(GhostToolchain NONE)

# User options
set(TARGET_TRIPLE "${TARGET_TRIPLE}" CACHE STRING "Target triple (e.g. x86_64-ghost)")
set(TOOLCHAIN_BASE "${TOOLCHAIN_BASE}" CACHE PATH "Toolchain installation directory")
set(SYSROOT "${SYSROOT}" CACHE PATH "Sysroot output directory")

# Defaults to safe local paths
if(NOT TARGET_TRIPLE)
  set(TARGET_TRIPLE x86_64-ghost)
endif()
if(NOT TOOLCHAIN_BASE)
  set(TOOLCHAIN_BASE ${CMAKE_BINARY_DIR}/toolchain)
endif()
if(NOT SYSROOT)
  set(SYSROOT ${CMAKE_BINARY_DIR}/sysroot)
endif()

find_program(GHOST_BOOTSTRAP_PACMAN_EXECUTABLE pacman)

function(ghost_bootstrap_install_hint out_var package_name)
  if(APPLE)
    set(_hint "brew install ${package_name}")
  elseif(GHOST_BOOTSTRAP_PACMAN_EXECUTABLE)
    set(_hint "sudo pacman -S ${package_name}")
  else()
    set(_hint "install '${package_name}' with your package manager")
  endif()
  set(${out_var} "${_hint}" PARENT_SCOPE)
endfunction()

function(ghost_bootstrap_require_program out_var binary package_name reason)
  find_program(_binary_path NAMES ${binary})
  if(NOT _binary_path)
    ghost_bootstrap_install_hint(_hint "${package_name}")
    message(FATAL_ERROR "${binary} is required to ${reason}.\nHint: ${_hint}")
  endif()
  set(${out_var} "${_binary_path}" PARENT_SCOPE)
endfunction()

function(ghost_bootstrap_require_dev_library prefix header library package_name)
  find_path(${prefix}_INCLUDE_DIR NAMES ${header})
  find_library(${prefix}_LIBRARY NAMES ${library})
  if(NOT ${prefix}_INCLUDE_DIR OR NOT ${prefix}_LIBRARY)
    ghost_bootstrap_install_hint(_hint "${package_name}")
    message(FATAL_ERROR "Missing development library '${library}' (${header}) required for GCC/binutils toolchain.\nHint: ${_hint}")
  endif()
endfunction()

ghost_bootstrap_require_program(BASH_EXECUTABLE bash bash "run toolchain scripts")
ghost_bootstrap_require_program(CURL_EXECUTABLE curl curl "download source archives")
ghost_bootstrap_require_program(TAR_EXECUTABLE tar tar "extract source archives")
ghost_bootstrap_require_program(PATCH_EXECUTABLE patch patch "apply toolchain patches")
ghost_bootstrap_require_program(MAKE_EXECUTABLE make make "build toolchain components")
ghost_bootstrap_require_program(GCC_EXECUTABLE gcc gcc "build host-side helper tools")
ghost_bootstrap_require_program(GXX_EXECUTABLE g++ gcc "build host-side helper tools")
ghost_bootstrap_require_program(BISON_EXECUTABLE bison bison "configure and build GNU toolchain components")
ghost_bootstrap_require_program(FLEX_EXECUTABLE flex flex "configure and build GNU toolchain components")
ghost_bootstrap_require_program(MAKEINFO_EXECUTABLE makeinfo texinfo "build GNU toolchain components")
ghost_bootstrap_require_program(PKGCONFIG_EXECUTABLE pkg-config pkg-config "resolve host package metadata")
ghost_bootstrap_require_program(NASM_EXECUTABLE nasm nasm "assemble project components")

ghost_bootstrap_require_dev_library(GMP gmp.h gmp gmp)
ghost_bootstrap_require_dev_library(MPFR mpfr.h mpfr mpfr)
ghost_bootstrap_require_dev_library(MPC mpc.h mpc libmpc)

# Paths
set(REPO_ROOT ${CMAKE_SOURCE_DIR}/../..)
set(TOOLCHAIN_SH ${REPO_ROOT}/toolchain.sh)

set(AUTOCONF_PREFIX ${REPO_ROOT}/build-deps/autoconf-2.69)
set(AUTOCONF_269_BIN ${AUTOCONF_PREFIX}/bin)
set(AUTOCONF_BIN ${AUTOCONF_269_BIN}/autoconf)

set(AUTOCONF_SCRIPT ${REPO_ROOT}/tools/install-autoconf-269.sh)
if(NOT EXISTS ${AUTOCONF_SCRIPT})
  message(FATAL_ERROR "Expected ${AUTOCONF_SCRIPT} to exist for installing Autoconf 2.69")
endif()

add_custom_command(
  OUTPUT ${AUTOCONF_BIN}
  COMMAND ${CMAKE_COMMAND} -E env AUTOCONF_PREFIX=${AUTOCONF_PREFIX} bash ${AUTOCONF_SCRIPT}
  WORKING_DIRECTORY ${REPO_ROOT}
  COMMENT "Installing Autoconf 2.69 into ${AUTOCONF_PREFIX}"
  VERBATIM
)
add_custom_target(autoconf-269 DEPENDS ${AUTOCONF_BIN})

set(_toolchain_path "${AUTOCONF_269_BIN}:$ENV{PATH}")
string(REPLACE "(" "\\(" _toolchain_path "${_toolchain_path}") # avoid /bin/sh syntax errors from WSL PATH entries
string(REPLACE ")" "\\)" _toolchain_path "${_toolchain_path}")
add_custom_target(ghost-toolchain
  COMMAND ${CMAKE_COMMAND} -E env
          # Quote env values so shells don't choke on characters like parentheses from the host PATH
          "TARGET=${TARGET_TRIPLE}"
          "TOOLCHAIN_BASE=${TOOLCHAIN_BASE}"
          "SYSROOT=${SYSROOT}"
          "PATH=${_toolchain_path}"
          bash ${TOOLCHAIN_SH}
  WORKING_DIRECTORY ${REPO_ROOT}
  USES_TERMINAL
  COMMENT "Building Ghost cross toolchain into ${TOOLCHAIN_BASE} with sysroot ${SYSROOT}")

add_dependencies(ghost-toolchain autoconf-269)
