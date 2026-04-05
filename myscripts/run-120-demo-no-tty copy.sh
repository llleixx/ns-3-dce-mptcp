#!/bin/bash
set -euo pipefail

workdir=/root/bake/source/ns-3-dce
build_mode=${BUILD_MODE:-opt}

case "$build_mode" in
  opt)
    binary_default=/root/bake/source/ns-3-dce/build-opt-dce-rel/myscripts/120-demo/bin/120-demo
    ld_library_path_default=/root/bake/source/ns-3-dce/build-opt-dce-rel/lib:/root/bake/build-opt-rel/lib:/root/bake/build-opt-dce-rel/lib
    dce_path_default=/root/bake/build-opt-dce-rel/bin:/root/bake/build-opt-dce-rel/bin_dce:/root/bake/build/sbin:/root/bake/build/bin_dce
    ;;
  debug)
    binary_default=/root/bake/source/ns-3-dce/build/myscripts/120-demo/bin/120-demo
    ld_library_path_default=/root/bake/source/ns-3-dce/build/lib:/root/bake/build/lib
    dce_path_default=/root/bake/build/bin:/root/bake/build/bin_dce:/root/bake/build/sbin
    ;;
  *)
    echo "unsupported BUILD_MODE=${build_mode}; use opt or debug" >&2
    exit 1
    ;;
esac

binary=${BINARY_OVERRIDE:-$binary_default}
ld_library_path=${LD_LIBRARY_PATH_OVERRIDE:-$ld_library_path_default}
dce_path=${DCE_PATH_OVERRIDE:-$dce_path_default}

timestamp=$(date -u +%Y%m%dT%H%M%SZ)
prefix_default=/tmp/120-demo-no-tty-${timestamp}
prefix=${1:-$prefix_default}
if [[ $# -gt 0 ]]; then
  shift
fi
wait_seconds=${WAIT_SECONDS:-90}
clean_env=${CLEAN_ENV:-0}
kill_on_timeout=${KILL_ON_TIMEOUT:-1}
fast_env_profile=${FAST_ENV_PROFILE:-1}

submit_env_file=${prefix}.submit-env
job_env_file=${prefix}.job-env
job_ps_file=${prefix}.job-ps
job_meta_file=${prefix}.job-meta
job_cgroup_file=${prefix}.job-cgroup
job_cmd_pid_file=${prefix}.job-cmd-pid

default_args=(
  --numClients=9
  --numAps=1
  --simTime=3.0
  --sinkStart=0.5
  --clientStart=1.0
  --clientStartJitter=0.0
  --appSteadyRate=100Mbps
  --appBurstRate=100Mbps
  --wifiEnableOfdma=false
  --wifiEnableUlOfdma=false
  --wifiEnableBsrp=false
  --checkBackboneDualTraffic=1
)

if [[ $# -gt 0 ]]; then
  args=("$@")
else
  args=("${default_args[@]}")
fi

job_script=$(mktemp /tmp/120-demo-at-job.XXXXXX.sh)
done_file=${prefix}.done
time_file=${prefix}.time
out_file=${prefix}.out
err_file=${prefix}.err

cleanup() {
  rm -f "$job_script"
}
trap cleanup EXIT

{
  echo "submit_time=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "submit_pid=$$"
  echo "submit_ppid=$PPID"
  echo "submit_tty=$(ps -o tty= -p $$ | tr -d ' ')"
  echo "submit_pwd=$PWD"
  echo "submit_shell=$SHELL"
  echo "submit_term=${TERM-}"
  echo "submit_shlvl=${SHLVL-}"
  echo "submit_session=$(awk '{print $6}' /proc/$$/stat)"
  echo "submit_pgrp=$(awk '{print $5}' /proc/$$/stat)"
  echo "submit_tty_nr=$(awk '{print $7}' /proc/$$/stat)"
  echo "submit_tpgid=$(awk '{print $8}' /proc/$$/stat)"
  echo "== env =="
  env | sort
} >"$submit_env_file"

{
  printf '#!/bin/bash\n'
  printf 'set -euo pipefail\n'
  printf 'cd %q\n' "$workdir"
  printf '{\n'
  printf '  echo "job_time=$(date -u +%%Y-%%m-%%dT%%H:%%M:%%SZ)"\n'
  printf '  echo "job_pid=$$"\n'
  printf '  echo "job_ppid=$PPID"\n'
  printf '  echo "job_tty=$(ps -o tty= -p $$ | tr -d %q)"\n' ' '
  printf '  echo "job_pwd=$PWD"\n'
  printf '  echo "job_shell=$SHELL"\n'
  printf '  echo "job_term=${TERM-}"\n'
  printf '  echo "job_shlvl=${SHLVL-}"\n'
  printf '  echo "job_session=$(awk %q /proc/$$/stat)"\n' '{print $6}'
  printf '  echo "job_pgrp=$(awk %q /proc/$$/stat)"\n' '{print $5}'
  printf '  echo "job_tty_nr=$(awk %q /proc/$$/stat)"\n' '{print $7}'
  printf '  echo "job_tpgid=$(awk %q /proc/$$/stat)"\n' '{print $8}'
  printf '} >%q\n' "$job_meta_file"
  printf 'env | sort >%q\n' "$job_env_file"
  printf 'cat /proc/$$/cgroup >%q\n' "$job_cgroup_file"
  printf 'ps -o pid,ppid,tty,stat,pri,ni,psr,pcpu,pmem,etime,cmd -p $$ >%q\n' "$job_ps_file"
  printf 'rm -rf files-*\n'
  printf '(\n'
  printf '  exec /usr/bin/time -o %q -f %q \\\n' \
    "$time_file" 'real=%e user=%U sys=%S maxrss=%M exit=%x'
  if [[ "$clean_env" == "1" ]]; then
    printf '    env -i HOME=%q PATH=%q LANG=%q LC_ALL=%q LD_LIBRARY_PATH=%q DCE_PATH=%q \\\n' \
      "/root" "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin" \
      "C.UTF-8" "C.UTF-8" "$ld_library_path" "$dce_path"
  elif [[ "$fast_env_profile" == "1" ]]; then
    printf '    env HOME=%q PATH=%q LANG=%q LC_ALL=%q LC_CTYPE=%q PAGER=%q GIT_PAGER=%q GH_PAGER=%q NO_COLOR=%q CODEX_CI=%q CODEX_MANAGED_BY_NPM=%q CODEX_THREAD_ID=%q LD_LIBRARY_PATH=%q DCE_PATH=%q \\\n' \
      "/root" \
      "/root/.codex/tmp/arg0/codex-arg09RESQr:/usr/lib/node_modules/@openai/codex/node_modules/@openai/codex-linux-x64/vendor/x86_64-unknown-linux-musl/path:/root/.vscode-server/data/User/globalStorage/github.copilot-chat/debugCommand:/root/.vscode-server/data/User/globalStorage/github.copilot-chat/copilotCli:/root/.vscode-server/cli/servers/Stable-e7fb5e96c0730b9deb70b33781f98e2f35975036/server/bin/remote-cli:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin:/usr/games:/usr/local/games:/snap/bin:/root/bake/build/bin:/root/bake/build/bin_dce:/root/bake/build/bin:/root/bake/build/bin_dce" \
      "en_US.UTF-8" "C.UTF-8" "C.UTF-8" \
      "cat" "cat" "cat" "1" "1" "1" "fast-profile" \
      "$ld_library_path" "$dce_path"
  else
    printf '    env LD_LIBRARY_PATH=%q DCE_PATH=%q \\\n' \
      "$ld_library_path" "$dce_path"
  fi
  printf '    %q' "$binary"
  for arg in "${args[@]}"; do
    printf ' %q' "$arg"
  done
  printf ' \\\n'
  printf '    >%q 2>%q\n' "$out_file" "$err_file"
  printf ') &\n'
  printf 'cmd_pid=$!\n'
  printf 'echo "$cmd_pid" >%q\n' "$job_cmd_pid_file"
  printf 'wait "$cmd_pid"\n'
  printf 'echo "$?" >%q\n' "$done_file"
} >"$job_script"

chmod +x "$job_script"
rm -f "$done_file" "$time_file" "$out_file" "$err_file" \
  "$job_env_file" "$job_ps_file" "$job_meta_file" "$job_cgroup_file" \
  "$job_cmd_pid_file"

job_id=$(
  printf '%s\n' "$job_script" |
    at -M now 2>&1 |
    awk '/job / {print $2}' |
    tail -n1
)

if [[ -z "$job_id" ]]; then
  echo "failed to submit at job" >&2
  exit 1
fi

echo "submitted at job ${job_id}"
echo "prefix=${prefix}"
echo "job_script=${job_script}"

for _ in $(seq 1 "$wait_seconds"); do
  if [[ -f "$done_file" ]]; then
    break
  fi
  sleep 1
done

if [[ ! -f "$done_file" ]]; then
  echo "timed out waiting for ${done_file} after ${wait_seconds}s" >&2
  echo "== ${submit_env_file} =="
  sed -n '1,80p' "$submit_env_file" 2>/dev/null || true
  echo "== ${job_meta_file} =="
  sed -n '1,80p' "$job_meta_file" 2>/dev/null || true
  echo "== ${job_ps_file} =="
  sed -n '1,40p' "$job_ps_file" 2>/dev/null || true
  echo "== ${job_cgroup_file} =="
  sed -n '1,40p' "$job_cgroup_file" 2>/dev/null || true
  if [[ -f "$job_cmd_pid_file" ]]; then
    cmd_pid=$(cat "$job_cmd_pid_file")
    echo "== running cmd pid =="
    echo "cmd_pid=${cmd_pid}"
    ps -o pid,ppid,tty,stat,pri,ni,psr,pcpu,pmem,etime,cmd -p "$cmd_pid" 2>/dev/null || true
    echo "== running children =="
    ps -o pid,ppid,tty,stat,pri,ni,psr,pcpu,pmem,etime,cmd --ppid "$cmd_pid" 2>/dev/null || true
    if [[ "$kill_on_timeout" == "1" ]]; then
      pkill -TERM -P "$cmd_pid" 2>/dev/null || true
      kill -TERM "$cmd_pid" 2>/dev/null || true
      sleep 1
      pkill -KILL -P "$cmd_pid" 2>/dev/null || true
      kill -KILL "$cmd_pid" 2>/dev/null || true
      echo "killed_timed_out_process_tree=1"
    fi
  fi
  echo "== partial ${time_file} =="
  sed -n '1,20p' "$time_file" 2>/dev/null || true
  echo "== partial ${out_file} =="
  sed -n '1,40p' "$out_file" 2>/dev/null || true
  echo "== partial ${err_file} =="
  sed -n '1,40p' "$err_file" 2>/dev/null || true
  exit 2
fi

echo "done_status=$(cat "$done_file")"
echo "== ${submit_env_file} =="
sed -n '1,80p' "$submit_env_file"
echo "== ${job_meta_file} =="
sed -n '1,80p' "$job_meta_file"
echo "== ${job_ps_file} =="
sed -n '1,40p' "$job_ps_file"
echo "== ${job_cgroup_file} =="
sed -n '1,40p' "$job_cgroup_file"
echo "== ${time_file} =="
sed -n '1,20p' "$time_file"
echo "== ${out_file} =="
sed -n '1,40p' "$out_file"
if [[ -s "$err_file" ]]; then
  echo "== ${err_file} =="
  sed -n '1,40p' "$err_file"
fi
