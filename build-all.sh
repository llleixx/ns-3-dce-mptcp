#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
ns3_root=$(cd "${repo_root}/../ns-3.35" && pwd)

mode="both"
jobs="${JOBS:-4}"

usage() {
  cat <<'EOF'
Usage:
  ./build-all.sh [--mode debug|opt|both] [-j N]

Description:
  Rebuild the full ns-3.35 + ns-3-dce toolchain from this workspace.

Options:
  --mode MODE   Build mode: debug, opt, or both. Default: both
  -j, --jobs N  Parallel jobs for the build step. Default: 4
  -h, --help    Show this help

Notes:
  - The debug ns-3.35 build uses --disable-python in this workspace because
    the current generated Wi-Fi bindings do not build cleanly.
  - The opt build follows the optimized/O2 workflow from build.md.
EOF
}

die() {
  echo "[build-all] $*" >&2
  exit 1
}

run_cmd() {
  printf '+'
  for arg in "$@"; do
    printf ' %q' "$arg"
  done
  printf '\n'
  "$@"
}

build_debug() {
  echo "[build-all] building debug toolchain"

  cd "${ns3_root}"
  run_cmd python3 ./waf configure \
    --prefix=/root/bake/build \
    --enable-examples \
    --enable-tests \
    --disable-python
  run_cmd python3 ./waf build install -j "${jobs}"

  cd "${repo_root}"
  run_cmd python3 ./waf configure \
    --prefix=/root/bake/build \
    --with-ns3=/root/bake/build \
    --with-glibc=/root/bake/build/glibc \
    --with-libaspect=/root/bake/build \
    --enable-kernel-stack=/root/bake/source/net-next-nuse-mptcp-0.92/arch
  run_cmd python3 ./waf build install -j "${jobs}"
}

build_opt() {
  echo "[build-all] building optimized toolchain"

  cd "${ns3_root}"
  run_cmd env CCFLAGS='-O2 -g0' CXXFLAGS='-O2 -g0' \
    python3 ./waf configure -d optimized \
      --disable-python \
      --disable-tests \
      --disable-examples \
      --prefix=/root/bake/build-opt-rel \
      -o build-opt-rel
  run_cmd python3 ./waf build install -j "${jobs}" -o build-opt-rel

  cd "${repo_root}"
  run_cmd env CCFLAGS='-O2 -g0' CXXFLAGS='-O2 -g0' \
    python3 ./waf configure \
      --enable-opt \
      --disable-debug \
      --disable-assert \
      --disable-log \
      --disable-python \
      --with-ns3=/root/bake/build-opt-rel \
      --with-glibc=/root/bake/build/glibc \
      --with-libaspect=/root/bake/build \
      --enable-kernel-stack=/root/bake/source/net-next-nuse-mptcp-0.92/arch \
      --prefix=/root/bake/build-opt-dce-rel \
      -o build-opt-dce-rel
  run_cmd python3 ./waf build install -j "${jobs}" -o build-opt-dce-rel
}

while (($#)); do
  case "$1" in
    --mode)
      (($# >= 2)) || die "missing value for --mode"
      mode="$2"
      shift 2
      ;;
    --mode=*)
      mode="${1#*=}"
      shift
      ;;
    -j|--jobs)
      (($# >= 2)) || die "missing value for $1"
      jobs="$2"
      shift 2
      ;;
    -j*)
      jobs="${1#-j}"
      shift
      ;;
    --jobs=*)
      jobs="${1#*=}"
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      die "unknown argument: $1"
      ;;
  esac
done

case "${mode}" in
  debug)
    build_debug
    ;;
  opt)
    build_opt
    ;;
  both)
    build_debug
    build_opt
    ;;
  *)
    die "unsupported mode: ${mode}"
    ;;
esac

echo "[build-all] done"
