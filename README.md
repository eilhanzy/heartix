# Heartix OS 

Heartix is an advanced, high-performance operating system userland built on top of the **HXNU Micro-Hybrid Kernel**. 

Originally a heavily modified fork of the monolithic Ghost microkernel, Heartix has undergone a complete architectural revolution. The legacy monolithic kernel has been purged and replaced by the deterministic, zero-latency **HXNU Ecosystem**, bridging the gap between bare-metal hardware and a rich, modern POSIX-like userland.

## 🏗️ Architecture

Heartix is designed with a strict Kernel-Userland isolation boundary:
* **Kernel (HXNU):** A lightweight, micro-hybrid traffic director. It handles zero-latency asymmetric IRQ routing, memory mapping (via HFS), and process lifecycle. It does NOT process POSIX logic internally.
* **Compatibility Layer (GCL):** A native `.hxext` kernel extension (`hxnu-drivers`) that provides backwards compatibility for legacy Ghost system calls by translating them to HXNU native LCL (Local Communication Layer) calls.
* **Userland (Heartix):** A rich ecosystem of applications (`.hxapp`), drivers, window managers (`fenster`), and libraries (`libapi`, `libc`) running in isolated Ring 3 environments.

## ⚖️ Licensing

Heartix strictly adheres to a dual-license architecture to satisfy both open-source freedom and patent protection:
* **The HXNU Kernel & Ecosystem:** Licensed under **TCOL (Turkish Conservative Open License) v1.1**, enforcing strict bare-metal integrity, anti-backdoor (TCK compliance), and patent protections for Middle Eastern Calculation Machinary Ltd proprietary technologies.
* **The Heartix Userland:** Licensed under **GNU GPLv3**, guaranteeing freedom to share and modify the user-space applications and standard libraries.

*The boundary between GPLv3 and TCOL is maintained cleanly via the HXNU system call interface (LCL / Ghost Bootstrap ABI), ensuring full legal compliance without static linkage conflicts.*

## 🚀 Build System

Heartix utilizes a modernized, pure **CMake** build flow, abandoning legacy bash scripts.

### Prerequisites
* A standard Linux development environment (Arch, Ubuntu, WSL2, etc.)
* `cmake`, `make`, `git`, `python3`
* The **HXNU GCC Toolchain** (`aarch64-hxnu-gcc` or `x86_64-hxnu-gcc` depending on target architecture).

### Building

The build system is currently undergoing migration to output `.hxapp` binaries.
```bash
mkdir build && cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/hxnu-target.cmake
make
```

## 🖥️ User Interface
Heartix features **fenster**, a lightweight, high-performance windowing system, accompanied by custom terminal emulators and desktop applications built to run seamlessly on the HXNU architecture.

## 🤝 Attribution
* **Heartix Architecture & HXNU Kernel:** Efe İlhan Yüce (Chief Architect, Middle Eastern Calculation Machinary Ltd).
* **Legacy Origins:** This codebase originally descended from the Ghost project by Max Schlüssel. Heartix retains the GPL notices while applying its own massive architectural shifts and branding.

## 📧 Contact
* **Middle Eastern Calculation Machinary Ltd:** efe@berrycomp.com
* **Upstream Ghost (Legacy):** lokoxe@gmail.com
