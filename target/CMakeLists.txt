# CMake integration for target utilities (e.g., Limine)

cmake_minimum_required(VERSION 3.20)

set(LIMINE_VERSION "${LIMINE_VERSION}" CACHE STRING "Limine version")
if(NOT LIMINE_VERSION)
  set(LIMINE_VERSION 9.2.0)
endif()
set(LIMINE_SOURCE "https://ghostkernel.org/repository/limine/limine-${LIMINE_VERSION}.tar.gz")

find_program(PACMAN_EXECUTABLE pacman)

function(ghost_tool_install_hint out_var tool)
  if(APPLE)
    set(_hint "brew install ${tool}")
  elseif(PACMAN_EXECUTABLE)
    set(_hint "sudo pacman -S ${tool}")
  else()
    set(_hint "install '${tool}' with your package manager")
  endif()
  set(${out_var} "${_hint}" PARENT_SCOPE)
endfunction()

function(ghost_require_program out_var program reason)
  find_program(_found_program NAMES ${program})
  if(NOT _found_program)
    ghost_tool_install_hint(_hint "${program}")
    message(FATAL_ERROR "${program} is required to ${reason}.\nHint: ${_hint}")
  endif()
  set(${out_var} "${_found_program}" PARENT_SCOPE)
endfunction()

# Host tool checks for Limine preparation.
ghost_require_program(CURL_EXECUTABLE curl "fetch Limine sources")
ghost_require_program(TAR_EXECUTABLE tar "extract Limine sources")
ghost_require_program(MAKE_EXECUTABLE make "build Limine")
ghost_require_program(NASM_EXECUTABLE nasm "assemble Limine and kernel objects")

# Where to place limine directory (inside repo's target dir)
set(LIMINE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/limine-${LIMINE_VERSION})
set(LIMINE_TAR ${CMAKE_CURRENT_BINARY_DIR}/limine-${LIMINE_VERSION}.tar.gz)
set(LIMINE_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(DOWNLOAD_LIMINE_SCRIPT ${CMAKE_CURRENT_BINARY_DIR}/download-limine.sh)

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/download-limine.sh.in ${DOWNLOAD_LIMINE_SCRIPT} @ONLY)

# Build limine binaries if missing
add_custom_command(
  OUTPUT ${LIMINE_DIR}/bin/limine-bios.sys
  BYPRODUCTS ${LIMINE_DIR}/bin/limine-bios-cd.bin ${LIMINE_DIR}/bin/limine-uefi-cd.bin ${LIMINE_DIR}/bin/limine
  COMMAND ${CMAKE_COMMAND} -E echo "Preparing Limine ${LIMINE_VERSION}"
  COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}
  COMMAND bash ${DOWNLOAD_LIMINE_SCRIPT}
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  USES_TERMINAL
  COMMENT "Download and build Limine (${LIMINE_VERSION})")

add_custom_target(limine DEPENDS ${LIMINE_DIR}/bin/limine-bios.sys)
