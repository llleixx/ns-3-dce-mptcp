#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)

mode="opt"
jobs="${JOBS:-4}"
timeout_seconds="0"
clean_files="0"
target_override=""
source_path=""
program_args=()

usage() {
  cat <<'EOF'
Usage:
  ./run-myscript.sh [options] <myscripts-source.cc> [-- program args...]

Description:
  Resolve a source file under myscripts/, build its target, and run it with
  the correct debug or optimized environment.

Options:
  --mode MODE      Run mode: debug or opt. Default: opt
  --timeout SEC    Kill the program after SEC seconds. Default: 0 (no timeout)
  -j, --jobs N     Parallel jobs for the build step. Default: 4
  --target NAME    Override target resolution when the source maps to multiple targets
  --clean-files    Remove files-* before running
  -h, --help       Show this help

Examples:
  ./run-myscript.sh --mode opt \
    myscripts/120-demo/link-info/wifi-only/link-info-wifi-only.cc \
    -- --numClients=6 --simTime=3.0

  ./run-myscript.sh --mode debug --timeout 120 \
    myscripts/120-demo/120-demo.cc
EOF
}

die() {
  echo "[run-myscript] $*" >&2
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

resolve_target() {
  local source_abs="$1"
  local override="$2"

  python3 - "$repo_root" "$source_abs" "$override" <<'PY'
import ast
import os
import sys

root = os.path.abspath(sys.argv[1])
source_abs = os.path.abspath(sys.argv[2])
override = sys.argv[3]
source_rel = os.path.relpath(source_abs, root)
source_stem = os.path.splitext(os.path.basename(source_rel))[0]


def eval_str(node):
    if isinstance(node, ast.Constant) and isinstance(node.value, str):
        return node.value
    if isinstance(node, ast.Str):
        return node.s
    if isinstance(node, ast.BinOp) and isinstance(node.op, ast.Add):
        left = eval_str(node.left)
        right = eval_str(node.right)
        if left is not None and right is not None:
            return left + right
    return None


def eval_sources(node):
    if isinstance(node, (ast.List, ast.Tuple)):
        values = []
        for item in node.elts:
            text = eval_str(item)
            if text is not None:
                values.append(text)
        return values
    text = eval_str(node)
    if text is not None:
        return [text]
    return []


def should_skip(path):
    parts = path.split(os.sep)
    return any(part in {'.git', 'build', 'build-opt-dce-rel', '.codex-backups'} for part in parts)


candidates = []

for dirpath, dirnames, filenames in os.walk(root):
    dirnames[:] = [name for name in dirnames if not should_skip(os.path.join(dirpath, name))]
    if 'wscript' not in filenames:
        continue

    wscript_path = os.path.join(dirpath, 'wscript')
    try:
        with open(wscript_path, 'r', encoding='utf-8') as handle:
            tree = ast.parse(handle.read(), filename=wscript_path)
    except SyntaxError:
        continue

    wscript_dir_rel = os.path.relpath(dirpath, root)
    for node in ast.walk(tree):
        if not isinstance(node, ast.Call):
            continue

        keywords = {kw.arg: kw.value for kw in node.keywords if kw.arg}
        if 'target' not in keywords or 'source' not in keywords:
            continue

        target = eval_str(keywords['target'])
        if not target:
            continue

        raw_sources = eval_sources(keywords['source'])
        for index, raw_source in enumerate(raw_sources):
            candidate_abs = os.path.abspath(os.path.join(dirpath, raw_source))
            candidate_rel = os.path.normpath(os.path.relpath(candidate_abs, root))
            if candidate_rel != source_rel:
                continue

            score = 100 + index
            target_name = os.path.basename(target)
            if target_name == source_stem:
                score -= 40
            if len(raw_sources) == 1:
                score -= 20

            candidates.append((score, wscript_dir_rel, target))

if not candidates:
    parts = source_rel.split(os.sep)
    if len(parts) == 2 and parts[0] == 'myscripts' and parts[1].endswith('.cc'):
        target = 'bin/' + os.path.splitext(parts[1])[0]
        candidates.append((0, '.', target))

if override:
    filtered = [
        entry for entry in candidates
        if entry[2] == override or os.path.basename(entry[2]) == override
    ]
    if not filtered:
        print(f"override target '{override}' did not match any candidate for {source_rel}", file=sys.stderr)
        sys.exit(2)
    candidates = filtered

if not candidates:
    print(f"could not resolve a waf target for {source_rel}", file=sys.stderr)
    sys.exit(2)

candidates.sort(key=lambda item: (item[0], item[1], item[2]))
best = candidates[0]

if len(candidates) > 1 and candidates[0][0] == candidates[1][0] and not override:
    print(f"multiple targets match {source_rel}; use --target to disambiguate:", file=sys.stderr)
    for _, _, target in candidates:
        print(f"  {target}", file=sys.stderr)
    sys.exit(2)

wscript_dir_rel = best[1]
target = best[2]
binary_rel = target if wscript_dir_rel == '.' else os.path.join(wscript_dir_rel, target)
print(f"{wscript_dir_rel}\t{target}\t{binary_rel}")
PY
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
    --timeout)
      (($# >= 2)) || die "missing value for --timeout"
      timeout_seconds="$2"
      shift 2
      ;;
    --timeout=*)
      timeout_seconds="${1#*=}"
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
    --target)
      (($# >= 2)) || die "missing value for --target"
      target_override="$2"
      shift 2
      ;;
    --target=*)
      target_override="${1#*=}"
      shift
      ;;
    --clean-files)
      clean_files="1"
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    --)
      shift
      program_args=("$@")
      break
      ;;
    -*)
      die "unknown argument: $1"
      ;;
    *)
      if [[ -z "${source_path}" ]]; then
        source_path="$1"
      else
        program_args+=("$1")
      fi
      shift
      ;;
  esac
done

[[ -n "${source_path}" ]] || die "missing source path"

source_abs=$(realpath "${source_path}")
[[ -f "${source_abs}" ]] || die "source file does not exist: ${source_path}"

case "${source_abs}" in
  "${repo_root}"/myscripts/*)
    ;;
  *)
    die "source must be under ${repo_root}/myscripts"
    ;;
esac

case "${mode}" in
  debug)
    out_dir="build"
    ld_library_path="${repo_root}/build/lib:/root/bake/build/lib"
    dce_path="${repo_root}/build/bin:${repo_root}/build/bin_dce:/root/bake/build/bin:/root/bake/build/bin_dce:/root/bake/build/sbin"
    ;;
  opt)
    out_dir="build-opt-dce-rel"
    ld_library_path="${repo_root}/build-opt-dce-rel/lib:/root/bake/build-opt-rel/lib:/root/bake/build-opt-dce-rel/lib"
    dce_path="${repo_root}/build-opt-dce-rel/bin:${repo_root}/build-opt-dce-rel/bin_dce:/root/bake/build-opt-dce-rel/bin:/root/bake/build-opt-dce-rel/bin_dce:/root/bake/build/sbin:/root/bake/build/bin_dce"
    ;;
  *)
    die "unsupported mode: ${mode}"
    ;;
esac

[[ -d "${repo_root}/${out_dir}/c4che" ]] || die "missing configured ${out_dir}; run ./build-all.sh --mode ${mode} first"

resolved=$(resolve_target "${source_abs}" "${target_override}") || exit $?
IFS=$'\t' read -r wscript_dir_rel target binary_rel <<< "${resolved}"
binary_path="${repo_root}/${out_dir}/${binary_rel}"

echo "[run-myscript] source=$(realpath --relative-to="${repo_root}" "${source_abs}")"
echo "[run-myscript] mode=${mode}"
echo "[run-myscript] target=${target}"
echo "[run-myscript] binary=${binary_path}"

cd "${repo_root}"
run_cmd python3 ./waf build --targets="${target}" -j "${jobs}" -o "${out_dir}"

[[ -x "${binary_path}" ]] || die "built binary not found: ${binary_path}"

if [[ -n "${LD_LIBRARY_PATH:-}" ]]; then
  ld_library_path="${ld_library_path}:${LD_LIBRARY_PATH}"
fi
if [[ -n "${DCE_PATH:-}" ]]; then
  dce_path="${dce_path}:${DCE_PATH}"
fi

if [[ "${clean_files}" == "1" ]]; then
  echo "[run-myscript] removing files-*"
  rm -rf files-*
fi

time_cmd=(/usr/bin/time -f 'real=%e user=%U sys=%S maxrss=%M exit=%x')
timeout_cmd=()
if [[ "${timeout_seconds}" != "0" ]]; then
  timeout_cmd=(/usr/bin/timeout "${timeout_seconds}s")
fi

run_cmd "${time_cmd[@]}" \
  "${timeout_cmd[@]}" \
  env "LD_LIBRARY_PATH=${ld_library_path}" "DCE_PATH=${dce_path}" \
  "${binary_path}" "${program_args[@]}"
