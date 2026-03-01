# ABOUT HEARTIX (heavy-modified Ghost)

Heartix is a heavily modified fork of the Ghost microkernel project. It keeps the
original Ghost architecture and copyright notices by Max Schlüssel, but rebrands
as **Heartix** and tracks its own versioning starting at **0.1.0**.

# This repository will now be kept as an archive, and end-of-life activities will begin. See issue #4 for details.

## Status
* Kernel: Heartix 0.1.0 (heavy-modified Ghost)
* License: GPLv3 (original Ghost licensing retained)
* Credits: Max Schlüssel (upstream Ghost); Heartix modifications by Efe Ilhan Yüce and contributors.

## Documentation
See `documentation/` for design notes and build instructions inherited from Ghost.

Outputs are placed under `target/` (bootable ISO, kernel, sysroot artifacts).

## Build (CMake)
Heartix is built with a cross toolchain (`x86_64-ghost`) and a CMake-based flow.
Third-party libraries (zlib, pixman, libpng, freetype, cairo, etc.) are built
into the Ghost sysroot during the build process; you do not install them as
system libraries for host compilation.

### Arch Linux prerequisites
```bash
sudo pacman -S --needed \
  base-devel cmake nasm xorriso curl git pkgconf \
  autoconf automake bison flex texinfo gmp mpfr libmpc isl
```

### macOS prerequisites
```bash
xcode-select --install
brew install \
  cmake gcc nasm xorriso curl pkg-config \
  autoconf automake bison flex texinfo gmp mpfr libmpc isl
```

### 1) Bootstrap the cross toolchain
```bash
cmake -S cmake/ghost-toolchain-bootstrap -B build-ghost-toolchain \
  -DTARGET_TRIPLE=x86_64-ghost \
  -DTOOLCHAIN_BASE=$PWD/build-ghost/toolchain \
  -DSYSROOT=$PWD/build-ghost/sysroot

cmake --build build-ghost-toolchain --target ghost-toolchain
```

### 2) Ensure the toolchain is visible
```bash
export PATH="$PWD/build-ghost/toolchain/bin:$PATH"
x86_64-ghost-gcc --version
```

If `x86_64-ghost-gcc` is not found, bootstrap the toolchain again (step 1).

### 3) Configure and build Heartix
```bash
rm -rf build-ghost/CMakeCache.txt build-ghost/CMakeFiles

cmake -S . -B build-ghost \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/ghost-x86_64.cmake \
  -DTARGET_TRIPLE=x86_64-ghost \
  -DTOOLCHAIN_BASE=$PWD/build-ghost/toolchain \
  -DSYSROOT=$PWD/build-ghost/sysroot \
  -DGHOST_BUILD_PORTS=ON \
  -DGHOST_ENABLE_PACK=ON

cmake --build build-ghost --target pack
```

macOS note: the build uses Limine's `v9.2.0-binary` release. If a native `limine` host
installer is not available, BIOS post-install is skipped and UEFI boot is recommended.

Output ISO:
```text
target/ghost.iso
```

### Build without ISO (no `xorriso` requirement)
If you only want binaries/sysroot artifacts:

```bash
cmake -S . -B build-ghost \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/ghost-x86_64.cmake \
  -DTARGET_TRIPLE=x86_64-ghost \
  -DTOOLCHAIN_BASE=$PWD/build-ghost/toolchain \
  -DSYSROOT=$PWD/build-ghost/sysroot \
  -DGHOST_BUILD_PORTS=ON \
  -DGHOST_ENABLE_PACK=OFF

cmake --build build-ghost
```

## Running
Test in a VM (VirtualBox/VMware/QEMU) with at least 512 MB RAM. Prefer VMSVGA/VMware SVGA for graphics.

## Features (inherited, evolving)
* x86_64 microkernel with SMP
* ELF userland, shared libs
* libapi + libc (Heartix-branded Ghost libs)
* IPC: messages, pipes, shared memory
* Drivers: VESA/VBE/VMSVGA/Bochs-VGA, PS/2, PCI, AC97, E1000 (where available)
* Limine boot protocol compliance

## Ported/used software
libpng, pixman, zlib, cairo, freetype, musl (libm), duktape, etc. (see `patches/ports`).

## Attribution
This codebase originates from the Ghost project by Max Schlüssel. Heartix retains the GPL and upstream notices while applying its own modifications and branding.

## Contact
Heartix mods: eilhanzy@protonmail.com
Ghost upstream: lokoxe@gmail.com
