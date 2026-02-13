# CMake integration for target utilities (e.g., Limine)

cmake_minimum_required(VERSION 3.20)

set(LIMINE_VERSION "${LIMINE_VERSION}" CACHE STRING "Limine version")
if(NOT LIMINE_VERSION)
  set(LIMINE_VERSION 9.2.0)
endif()
option(LIMINE_USE_BINARY_RELEASE "Use Limine prebuilt binary release instead of building from source" ${CMAKE_HOST_APPLE})
if(LIMINE_USE_BINARY_RELEASE)
  set(LIMINE_SOURCE "https://github.com/limine-bootloader/limine/archive/refs/tags/v${LIMINE_VERSION}-binary.tar.gz")
  set(LIMINE_ARCHIVE_ROOT "limine-${LIMINE_VERSION}-binary")
else()
  set(LIMINE_SOURCE "https://ghostkernel.org/repository/limine/limine-${LIMINE_VERSION}.tar.gz")
  set(LIMINE_ARCHIVE_ROOT "limine-${LIMINE_VERSION}")
endif()

find_program(PACMAN_EXECUTABLE pacman)

function(ghost_tool_install_hint out_var tool)
  if(CMAKE_HOST_APPLE)
    set(_hint "brew install ${tool}")
  elseif(PACMAN_EXECUTABLE)
    set(_hint "sudo pacman -S ${tool}")
  else()
    set(_hint "install '${tool}' with your package manager")
  endif()
  set(${out_var} "${_hint}" PARENT_SCOPE)
endfunction()

function(ghost_require_program out_var program reason)
  string(REGEX REPLACE "[^A-Za-z0-9_]" "_" _program_key "${program}")
  set(_program_cache_var "_ghost_target_prog_${_program_key}")
  find_program(${_program_cache_var} NAMES ${program})
  set(_found_program "${${_program_cache_var}}")
  if(NOT _found_program)
    ghost_tool_install_hint(_hint "${program}")
    message(FATAL_ERROR "${program} is required to ${reason}.\nHint: ${_hint}")
  endif()
  set(${out_var} "${_found_program}" PARENT_SCOPE)
endfunction()

# Host tool checks for Limine preparation.
ghost_require_program(CURL_EXECUTABLE curl "fetch Limine sources")
ghost_require_program(TAR_EXECUTABLE tar "extract Limine sources")
if(NOT LIMINE_USE_BINARY_RELEASE)
  ghost_require_program(MAKE_EXECUTABLE make "build Limine")
  ghost_require_program(NASM_EXECUTABLE nasm "assemble Limine and kernel objects")
  if(CMAKE_HOST_APPLE)
    find_program(LIMINE_OBJCOPY_EXECUTABLE
      NAMES gobjcopy objcopy
      HINTS /opt/homebrew/opt/binutils/bin /usr/local/opt/binutils/bin)
    find_program(LIMINE_OBJDUMP_EXECUTABLE
      NAMES gobjdump objdump
      HINTS /opt/homebrew/opt/binutils/bin /usr/local/opt/binutils/bin)
    find_program(LIMINE_READELF_EXECUTABLE
      NAMES greadelf readelf
      HINTS /opt/homebrew/opt/binutils/bin /usr/local/opt/binutils/bin)
    if(NOT LIMINE_OBJCOPY_EXECUTABLE)
      message(FATAL_ERROR "Limine build requires GNU objcopy on macOS.\nHint: brew install binutils")
    endif()
    if(NOT LIMINE_OBJDUMP_EXECUTABLE)
      message(FATAL_ERROR "Limine build requires GNU objdump on macOS.\nHint: brew install binutils")
    endif()
    if(NOT LIMINE_READELF_EXECUTABLE)
      message(FATAL_ERROR "Limine build requires GNU readelf on macOS.\nHint: brew install binutils")
    endif()
  else()
    ghost_require_program(LIMINE_OBJCOPY_EXECUTABLE objcopy "build Limine BIOS/UEFI artifacts")
    ghost_require_program(LIMINE_OBJDUMP_EXECUTABLE objdump "inspect Limine BIOS/UEFI objects")
    ghost_require_program(LIMINE_READELF_EXECUTABLE readelf "inspect Limine BIOS/UEFI objects")
  endif()
endif()

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
