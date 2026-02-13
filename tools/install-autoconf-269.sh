#!/bin/bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WORK_DIR="${ROOT_DIR}/build-deps"
PREFIX="${AUTOCONF_PREFIX:-${WORK_DIR}/autoconf-2.69}"
ARCHIVE_NAME="autoconf-2.69.tar.gz"
ARCHIVE_PATH="${WORK_DIR}/${ARCHIVE_NAME}"
SOURCE_URL="https://ftp.gnu.org/gnu/autoconf/${ARCHIVE_NAME}"
SRC_DIR="${WORK_DIR}/autoconf-2.69-src"

require_tool() {
  if ! command -v "$1" >/dev/null 2>&1; then
    echo "error: missing required tool '$1'" >&2
    exit 1
  fi
}

pick_make() {
  if [[ "$(uname -s)" == "Darwin" ]] && command -v gmake >/dev/null 2>&1; then
    echo "gmake"
  else
    echo "make"
  fi
}

detect_jobs() {
  local jobs=""
  if command -v nproc >/dev/null 2>&1; then
    nproc
    return
  fi
  if command -v sysctl >/dev/null 2>&1; then
    jobs="$(sysctl -n hw.logicalcpu 2>/dev/null || true)"
    if [ -z "${jobs}" ]; then
      jobs="$(sysctl -n hw.ncpu 2>/dev/null || true)"
    fi
    if [ -n "${jobs}" ]; then
      echo "${jobs}"
      return
    fi
  fi
  if command -v getconf >/dev/null 2>&1; then
    jobs="$(getconf _NPROCESSORS_ONLN 2>/dev/null || true)"
    if [ -n "${jobs}" ]; then
      echo "${jobs}"
      return
    fi
  fi
  echo 1
}

require_tool curl
require_tool tar
MAKE_BIN="$(pick_make)"
require_tool "${MAKE_BIN}"

if [ -x "${PREFIX}/bin/autoconf" ] && "${PREFIX}/bin/autoconf" --version 2>/dev/null | grep -q "GNU Autoconf) 2.69"; then
  echo "[autoconf-2.69] Already installed at ${PREFIX}"
  exit 0
fi

mkdir -p "${WORK_DIR}"
mkdir -p "${PREFIX}"

if [ ! -f "${ARCHIVE_PATH}" ]; then
  echo "[autoconf-2.69] Downloading to ${ARCHIVE_PATH}"
  curl --fail --location --retry 3 --retry-delay 2 "${SOURCE_URL}" -o "${ARCHIVE_PATH}"
else
  echo "[autoconf-2.69] Using cached archive ${ARCHIVE_PATH}"
fi

rm -rf "${SRC_DIR}"
mkdir -p "${SRC_DIR}"
tar -xzf "${ARCHIVE_PATH}" -C "${SRC_DIR}" --strip-components=1

pushd "${SRC_DIR}" >/dev/null
echo "[autoconf-2.69] Configuring with prefix ${PREFIX}"
./configure --prefix="${PREFIX}"
echo "[autoconf-2.69] Building"
JOBS="$(detect_jobs)"
if ! [[ "${JOBS}" =~ ^[0-9]+$ ]] || [ "${JOBS}" -lt 1 ]; then
  JOBS=1
fi
echo "[autoconf-2.69] Using ${MAKE_BIN} with ${JOBS} job(s)"
"${MAKE_BIN}" -j"${JOBS}"
echo "[autoconf-2.69] Installing"
"${MAKE_BIN}" install
popd >/dev/null

cat <<EOF

Autoconf 2.69 installed under: ${PREFIX}

Before running toolchain.sh or bootstrap-toolchain.sh, prepend this to PATH:
  export PATH=${PREFIX}/bin:\$PATH

EOF
