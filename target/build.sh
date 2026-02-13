#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
ISO_ROOT="${ROOT}/iso"
LIMINE_VERSION="${LIMINE_VERSION:-9.2.0}"
LIMINE_DIR="${ROOT}/limine-${LIMINE_VERSION}"

resolve_limine_artifact() {
    local name="$1"
    if [[ -f "${LIMINE_DIR}/bin/${name}" ]]; then
        printf "%s\n" "${LIMINE_DIR}/bin/${name}"
        return 0
    fi
    if [[ -f "${LIMINE_DIR}/${name}" ]]; then
        printf "%s\n" "${LIMINE_DIR}/${name}"
        return 0
    fi
    return 1
}

if [[ $# -lt 1 ]]; then
    echo "usage: $0 pack"
    exit 1
fi

case "$1" in
    pack)
        if [[ -z "${SYSROOT:-}" ]]; then
            echo "error: SYSROOT is required" >&2
            exit 1
        fi
        if [[ -z "${TOOLCHAIN_BASE:-}" ]]; then
            echo "error: TOOLCHAIN_BASE is required" >&2
            exit 1
        fi
        if [[ -z "${KERNEL_BIN:-}" ]]; then
            echo "error: KERNEL_BIN is required" >&2
            exit 1
        fi
        if [[ "${BUILD_PORTS:-1}" != "0" ]]; then
            if [[ ! -f "${SYSROOT}/system/.ports-built" ]]; then
                echo "building ports (missing stamp)"
                if [[ -z "${PKG_CONFIG:-}" ]]; then
                    PKG_CONFIG="${TOOLCHAIN_BASE}/bin/${TARGET:-x86_64-ghost}-pkg-config.sh"
                    export PKG_CONFIG
                fi
                export SYSROOT TOOLCHAIN_BASE
                bash "${ROOT}/../patches/ports/build-all.sh"
            fi
        fi

        echo "building ramdisk:"
        rm -rf "${ISO_ROOT}"
        mkdir -p "${ISO_ROOT}/boot/limine"
        "${TOOLCHAIN_BASE}/bin/ramdisk-writer" "${SYSROOT}" "${ISO_ROOT}/boot/ramdisk"

        echo "copying kernel and Limine artifacts"
        LIMINE_BIOS_SYS="$(resolve_limine_artifact limine-bios.sys)" || { echo "error: limine-bios.sys not found in ${LIMINE_DIR}" >&2; exit 1; }
        LIMINE_BIOS_CD="$(resolve_limine_artifact limine-bios-cd.bin)" || { echo "error: limine-bios-cd.bin not found in ${LIMINE_DIR}" >&2; exit 1; }
        LIMINE_UEFI_CD="$(resolve_limine_artifact limine-uefi-cd.bin)" || { echo "error: limine-uefi-cd.bin not found in ${LIMINE_DIR}" >&2; exit 1; }

        cp "${KERNEL_BIN}" "${ISO_ROOT}/boot/kernel"
        cp "${LIMINE_BIOS_SYS}" "${ISO_ROOT}/limine-bios.sys"
        cp "${LIMINE_BIOS_SYS}" "${ISO_ROOT}/boot/limine/limine-bios.sys"
        cp "${LIMINE_BIOS_CD}" "${ISO_ROOT}/boot/limine/limine-bios-cd.bin"
        cp "${LIMINE_UEFI_CD}" "${ISO_ROOT}/boot/limine/limine-uefi-cd.bin"

        cat <<EOF > "${ISO_ROOT}/limine.conf"
timeout: 5
default_entry: Ghost

/Ghost
    protocol: limine
    path: boot():/boot/kernel
    module_path: boot():/boot/ramdisk
    module_string: ramdisk
    cmdline: root=/dev/ram0
EOF
        cp "${ISO_ROOT}/limine.conf" "${ISO_ROOT}/boot/limine/limine.conf"

        echo "making iso:"
        (
            cd "${ISO_ROOT}"
            xorriso -as mkisofs -R -r -J -V Ghost \
                -b boot/limine/limine-bios-cd.bin \
                -no-emul-boot -boot-load-size 4 -boot-info-table -hfsplus \
                -apm-block-size 2048 \
                --efi-boot boot/limine/limine-uefi-cd.bin \
                -efi-boot-part --efi-boot-image \
                --protective-msdos-label \
                "${ISO_ROOT}" -o "${ROOT}/ghost.iso"
        )

        LIMINE_INSTALLER=""
        if [[ -x "${LIMINE_DIR}/bin/limine" ]]; then
            LIMINE_INSTALLER="${LIMINE_DIR}/bin/limine"
        elif [[ -x "${LIMINE_DIR}/limine" ]]; then
            LIMINE_INSTALLER="${LIMINE_DIR}/limine"
        fi

        if [[ -n "${LIMINE_INSTALLER}" ]]; then
            echo "running limine BIOS installer"
            "${LIMINE_INSTALLER}" bios-install "${ROOT}/ghost.iso"
        else
            echo "warning: limine host installer not found; skipping BIOS install step"
            echo "warning: UEFI boot files were copied. BIOS boot may not work on all VMs."
        fi
        ;;
    *)
        echo "usage: $0 pack"
        exit 1
        ;;
esac
