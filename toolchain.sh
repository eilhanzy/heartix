#!/bin/bash
if [ -f variables.sh ]; then
	. variables.sh
fi
. ghost.sh


#
# === Ghost Toolchain setup script ===
#
# This script attempts to download, patch and build the Ghost-specific toolchain
# so that your host system is capable of building binaries for Ghost.
# 
# The following pre-requirements must be installed on your system:
#
#    gcc g++ nasm make texinfo flex bison
#    libmpfr-dev libgmp-dev libmpc-dev libisl-dev autoconf pkg-config
#    grub-pc-bin xorriso
#
# The build will fail if any of these requirements are not present.
# To modify parameters, check the "variables.sh.template".
#

echo ""
printf "\e[44mGhost Toolchain setup script\e[0m\n"
echo ""
echo "Target architecture:    $TARGET"
echo "Toolchain base:         $TOOLCHAIN_BASE"
echo ""


# Archive sources & patch versions
GCC_ARCHIVE=https://ftp.gnu.org/gnu/gcc/gcc-12.2.0/gcc-12.2.0.tar.gz
GCC_PATCH=patches/toolchain/gcc-12.2.0-ghost-1.0.patch
GCC_UNPACKED=gcc-12.2.0

BINUTILS_ARCHIVE=https://ftp.gnu.org/gnu/binutils/binutils-2.39.tar.gz
BINUTILS_PATCH=patches/toolchain/binutils-2.39-ghost-1.0.patch
BINUTILS_UNPACKED=binutils-2.39

HOST_UNAME=$(uname -s)
MACOS_NEEDS_GNU_GCC=0
MACOS_MISSING_GCC_LIB_DEPS=""

apply_macos_zlib_fdopen_compat() {
	local zutil_file="$1"
	if [ "$HOST_UNAME" != "Darwin" ]; then
		return
	fi
	if [ ! -f "$zutil_file" ]; then
		return
	fi
	echo "Applying macOS compatibility patch to $(dirname "$zutil_file")"
	perl -0pi -e 's/#if defined\(MACOS\) \|\| defined\(TARGET_OS_MAC\)/#if defined(MACOS) || (defined(TARGET_OS_MAC) \&\& !defined(__APPLE__))/g' "$zutil_file"
}

apply_macos_binutils_compat() {
	apply_macos_zlib_fdopen_compat "temp/$BINUTILS_UNPACKED/zlib/zutil.h"
}

apply_macos_gcc_compat() {
	apply_macos_zlib_fdopen_compat "temp/$GCC_UNPACKED/zlib/zutil.h"
}

setup_macos_gcc_deps() {
	if [ "$HOST_UNAME" != "Darwin" ]; then
		return
	fi
	if ! command -v brew >/dev/null 2>&1; then
		return
	fi

	local brew_prefix
	local gmp_prefix
	local mpfr_prefix
	local mpc_prefix
	local isl_prefix
	local missing_libs=""
	brew_prefix=$(brew --prefix)
	gmp_prefix="$brew_prefix/opt/gmp"
	mpfr_prefix="$brew_prefix/opt/mpfr"
	mpc_prefix="$brew_prefix/opt/libmpc"
	isl_prefix="$brew_prefix/opt/isl"

	if [ ! -d "$gmp_prefix" ]; then
		missing_libs="$missing_libs gmp"
	fi
	if [ ! -d "$mpfr_prefix" ]; then
		missing_libs="$missing_libs mpfr"
	fi
	if [ ! -d "$mpc_prefix" ]; then
		missing_libs="$missing_libs libmpc"
	fi
	if [ ! -d "$isl_prefix" ]; then
		missing_libs="$missing_libs isl"
	fi
	if [ -n "$missing_libs" ]; then
		MACOS_MISSING_GCC_LIB_DEPS="$missing_libs"
		return
	fi

	echo "Using Homebrew GMP/MPFR/MPC/ISL for GCC build"
	BUILD_GCC_ADDITIONAL_FLAGS="$BUILD_GCC_ADDITIONAL_FLAGS --with-gmp=$gmp_prefix --with-mpfr=$mpfr_prefix --with-mpc=$mpc_prefix --with-isl=$isl_prefix"
}

setup_macos_host_compilers() {
	if [ "$HOST_UNAME" != "Darwin" ]; then
		return
	fi
	local major
	for ((major=30; major>=7; major--)); do
		if command -v "gcc-$major" >/dev/null 2>&1 && command -v "g++-$major" >/dev/null 2>&1; then
			HOST_CC="gcc-$major"
			HOST_CXX="g++-$major"
			echo "Using Homebrew GNU toolchain for host build: $HOST_CC / $HOST_CXX"
			return
		fi
	done
	if "$HOST_CXX" --version 2>/dev/null | grep -qi clang; then
		echo "warning: GNU GCC not found on macOS. Current host C++ compiler is clang ($HOST_CXX)." >&2
		echo "warning: Install Homebrew gcc (brew install gcc) for reliable cross-toolchain builds." >&2
		MACOS_NEEDS_GNU_GCC=1
	fi
}

failOnErrorWithLog() {
	local status=$?
	local log_file="${1:-$BUILD_LOG_FILE}"
	if [ "$status" = "0" ]; then
		return
	fi
	printf "\e[31;1mtarget failed\e[0m\n\n"
	if [ -f "$log_file" ]; then
		echo "Build log: $(pwd)/$log_file"
		echo "Last 60 lines:"
		tail -n 60 "$log_file"
		echo ""
	fi
	exit $status
}

applyPatchIfNeeded() {
	local source_dir="$1"
	local patch_file="$2"
	local marker_file="$3"
	local marker_pattern="$4"
	local log_file="${5:-temp/patching.log}"

	if [ -f "$marker_file" ] && grep -Fq "$marker_pattern" "$marker_file"; then
		echo "Patch already applied for $source_dir"
		return
	fi

	patch --batch --forward -d "$source_dir" -p 1 < "$patch_file" >>"$log_file" 2>&1
	failOnErrorWithLog "$log_file"
}


# Add toolchain bin folder to PATH
PATH=$PATH:$TOOLCHAIN_BASE/bin


# Default definitions
with REQUIRED_AUTOCONF "autoconf (GNU Autoconf) 2.69"
with AUTOMAKE	automake
with AUTOCONF	autoconf
with HOST_CC	gcc
with HOST_CXX	g++
with BUILD_GCC_ADDITIONAL_FLAGS ""
with BUILD_LOG_FILE "ghost-build.log"

setup_macos_gcc_deps
setup_macos_host_compilers


# Parse parameters
STEP_DOWNLOAD=1
STEP_CLEAN=0
STEP_UNPACK=1
STEP_PATCH=1

STEP_BUILD_BINUTILS=1
STEP_BUILD_GCC=1

for var in "$@"; do
	if [ $var == "--skip-archives" ]; then
		STEP_DOWNLOAD=0
		STEP_UNPACK=0
		STEP_PATCH=0
	elif [ $var == "--skip-download" ]; then
		STEP_DOWNLOAD=0
	elif [ $var == "--skip-unpack" ]; then
		STEP_UNPACK=0
	elif [ $var == "--skip-patch" ]; then
		STEP_PATCH=0
	elif [ $var == "--skip-build-binutils" ]; then
		STEP_BUILD_BINUTILS=0
	elif [ $var == "--skip-build-gcc" ]; then
		STEP_BUILD_GCC=0
	elif [ $var == "--clean" ]; then
		STEP_CLEAN=1
	elif [ $var == "--help" ]; then
		echo "Run this script to build a cross toolchain for your host system."
		echo "The following flags are available:"
		echo
		echo "	--skip-download			Skips the downloading of binutils/gcc archives"
		echo "	--skip-unpack			Skips the unpacking of said archives"
		echo "	--skip-patch			Skips the patching of the unpacked sources"
		echo "	--skip-build-binutils		Skips building binutils"
		echo "	--skip-build-gcc		Skips building gcc"
		echo "	--clean				Cleans everything"
		echo "	--help				Prints this help screen"
		echo
		echo "To specify a different path for your TOOLCHAIN_BASE/SYSROOT,"
		echo "copy 'variables.sh.template' to 'variables.sh' and use export"
		echo "to set the respective variables."
		echo
		exit
	else
		echo "unknown parameter: $var"
		exit
	fi
done

if [ "$HOST_UNAME" = "Darwin" ] && [ "$MACOS_NEEDS_GNU_GCC" = "1" ] && [ "$STEP_BUILD_GCC" = "1" ]; then
	echo "error: macOS detected with Apple clang as host compiler. GCC cross-toolchain bootstrap requires GNU gcc/g++." >&2
	echo "hint: brew install gcc" >&2
	exit 1
fi
if [ "$HOST_UNAME" = "Darwin" ] && [ -n "$MACOS_MISSING_GCC_LIB_DEPS" ] && [ "$STEP_BUILD_GCC" = "1" ]; then
	echo "error: missing Homebrew GCC build dependencies:$MACOS_MISSING_GCC_LIB_DEPS" >&2
	echo "hint: brew install$MACOS_MISSING_GCC_LIB_DEPS" >&2
	exit 1
fi

pushd() {
    command pushd "$@" > /dev/null
}

popd() {
    command popd "$@" > /dev/null
}


echo "Checking tools"
requireTool patch
requireTool curl
requireTool $AUTOCONF
requireTool $HOST_CC
requireTool $HOST_CXX



# Check version of autoconf
echo "    $REQUIRED_AUTOCONF"
$AUTOCONF --version | grep -q "$REQUIRED_AUTOCONF"
if [[ $? != 0 ]]; then
	echo "    -> wrong autoconf version:"
	echo "       $($AUTOCONF --version | sed -n 1p)"
	exit
fi


# Clean if necessary
if [ $STEP_CLEAN == 1 ]; then
	rm -rf temp
fi


# Create temp
echo "Creating temporary work directory"
mkdir -p temp


# Download archives if necessary
if [ $STEP_DOWNLOAD == 1 ]; then

	echo "Downloading archive files"
	echo "    gcc"
	curl $GCC_ARCHIVE -o temp/gcc.tar.gz -k
	echo "    binutils"
	curl $BINUTILS_ARCHIVE -o temp/binutils.tar.gz -k
	
else
	echo "Skipping file download"
fi


# Unpack archives
if [ $STEP_UNPACK == 1 ]; then

	echo "Unpacking archives"
	rm -rf "temp/$GCC_UNPACKED" "temp/$BINUTILS_UNPACKED"
	tar -xf temp/gcc.tar.gz -C temp
	failOnError
	tar -xf temp/binutils.tar.gz -C temp
	failOnError
	
else
	echo "Skipping unpacking"
fi


# Apply patches
if [ $STEP_PATCH == 1 ]; then

	echo "Patching GCC"
	applyPatchIfNeeded "temp/$GCC_UNPACKED" "$GCC_PATCH" "temp/$GCC_UNPACKED/gcc/config/ghost.h" "TARGET_GHOST 1" "temp/patching.log"
	pushd temp/$GCC_UNPACKED/libstdc++-v3
	echo "Updating autoconf in libstdc++-v3"
	$AUTOCONF
	failOnError
	popd
	
	echo "Patching binutils"
	applyPatchIfNeeded "temp/$BINUTILS_UNPACKED" "$BINUTILS_PATCH" "temp/$BINUTILS_UNPACKED/ld/configure.tgt" "*-*-ghost*)" "temp/patching.log"
	
else
	echo "Skipping patching"
fi

apply_macos_binutils_compat
apply_macos_gcc_compat


# Build tools
	echo "Building 'changes' tool"
pushd tools/changes

	CC=$HOST_CXX LD=$HOST_CXX $SH build.sh all		>>"$BUILD_LOG_FILE" 2>&1
	failOnErrorWithLog "$BUILD_LOG_FILE"

popd


echo "Building 'ramdisk-writer' tool"
pushd tools/ramdisk-writer

	CC=$HOST_CXX LD=$HOST_CXX $SH build.sh all		>>"$BUILD_LOG_FILE" 2>&1
	failOnErrorWithLog "$BUILD_LOG_FILE"

popd


echo "Installing 'pkg-config' wrapper"
pushd tools/pkg-config

	$SH build.sh						>>"$BUILD_LOG_FILE" 2>&1
	failOnErrorWithLog "$BUILD_LOG_FILE"

popd


# Install headers
echo "Installing libc and libapi headers"
pushd libc

	$SH build.sh install-headers	>>"$BUILD_LOG_FILE" 2>&1
	failOnErrorWithLog "$BUILD_LOG_FILE"

popd

pushd libapi

	$SH build.sh install-headers	>>"$BUILD_LOG_FILE" 2>&1
	failOnErrorWithLog "$BUILD_LOG_FILE"

popd


# Build binutils
if [ $STEP_BUILD_BINUTILS == 1 ]; then

	echo "Building binutils"
	rm -rf temp/build-binutils
	mkdir -p temp/build-binutils
	pushd temp/build-binutils

	echo "    Configuring"
	CC=$HOST_CC CXX=$HOST_CXX ../$BINUTILS_UNPACKED/configure --target=$TARGET --prefix=$TOOLCHAIN_BASE --disable-nls --enable-shared --disable-werror --with-sysroot=$SYSROOT >>"$BUILD_LOG_FILE" 2>&1
	failOnErrorWithLog "$BUILD_LOG_FILE"

	echo "    Building"
	make MAKEINFO=true all -j8						>>"$BUILD_LOG_FILE" 2>&1
	failOnErrorWithLog "$BUILD_LOG_FILE"

	echo "    Installing"
	make MAKEINFO=true install						>>"$BUILD_LOG_FILE" 2>&1
	failOnErrorWithLog "$BUILD_LOG_FILE"

	popd

else
	echo "Skipping build of binutils"
fi


# Build gcc
if [ $STEP_BUILD_GCC == 1 ]; then

	echo "Building gcc"
	rm -rf temp/build-gcc
	mkdir -p temp/build-gcc
	pushd temp/build-gcc
	
	echo "    Configuration"
	CC=$HOST_CC CXX=$HOST_CXX ../$GCC_UNPACKED/configure --target=$TARGET --prefix=$TOOLCHAIN_BASE --disable-nls --enable-languages=c,c++ --enable-shared --with-sysroot=$SYSROOT $BUILD_GCC_ADDITIONAL_FLAGS >>"$BUILD_LOG_FILE" 2>&1
	failOnErrorWithLog "$BUILD_LOG_FILE"

	echo "    Building core"
	make all-gcc -j8					>>"$BUILD_LOG_FILE" 2>&1
	failOnErrorWithLog "$BUILD_LOG_FILE"

	echo "    Installing core"
	make install-gcc					>>"$BUILD_LOG_FILE" 2>&1
	failOnErrorWithLog "$BUILD_LOG_FILE"

	popd
else
	echo "Skipping build of GCC"
fi


# Build libc static
echo "Building libc static"

	pushd libc
	$SH build.sh clean static					>>"$BUILD_LOG_FILE" 2>&1
	failOnErrorWithLog "$BUILD_LOG_FILE"

popd


# Build libapi
echo "Building libapi static"

	pushd libapi
	$SH build.sh clean static					>>"$BUILD_LOG_FILE" 2>&1
	failOnErrorWithLog "$BUILD_LOG_FILE"

popd


# Build libgcc
echo "Building target GCC libraries"
pushd temp/build-gcc

	echo "    Building target libgcc"
	make all-target-libgcc -j8				>>"$BUILD_LOG_FILE" 2>&1
	failOnErrorWithLog "$BUILD_LOG_FILE"

	echo "    Installing target libgcc"
	make install-target-libgcc				>>"$BUILD_LOG_FILE" 2>&1
	failOnErrorWithLog "$BUILD_LOG_FILE"

	echo "    Copying artifacts to system/lib"
	cp "$TOOLCHAIN_BASE/$TARGET/lib/libgcc_s.so.1" "$SYSROOT/system/lib/libgcc_s.so.1"
	failOnErrorWithLog "$BUILD_LOG_FILE"

popd


# Build libc shared
echo "Building libc shared"

	pushd libc
	$SH build.sh shared					>>"$BUILD_LOG_FILE" 2>&1
	failOnErrorWithLog "$BUILD_LOG_FILE"

popd


# Build libapi shared
echo "Building libapi shared"

	pushd libapi
	$SH build.sh shared					>>"$BUILD_LOG_FILE" 2>&1
	failOnErrorWithLog "$BUILD_LOG_FILE"

popd


# Build libstdc++-v3
pushd temp/build-gcc

	echo "    Building libstdc++-v3"
	make all-target-libstdc++-v3 -j8		>>"$BUILD_LOG_FILE" 2>&1
	failOnErrorWithLog "$BUILD_LOG_FILE"

	echo "    Installing libstdc++-v3"
	make install-target-libstdc++-v3		>>"$BUILD_LOG_FILE" 2>&1
	failOnErrorWithLog "$BUILD_LOG_FILE"

popd

# TODO: Build shared libstdc++-v3
#  ../gcc-12.2.0/libstdc++-v3/configure --host=i686-ghost --prefix=/system --enable-shared
# Doesn't work yet

# Finished
echo "Toolchain successfully built"
