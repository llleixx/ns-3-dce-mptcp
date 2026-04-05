#!/bin/bash
set -euo pipefail

workdir=/root/bake/source/ns-3-dce
build_mode=${BUILD_MODE:-opt}
wait_seconds=${WAIT_SECONDS:-900}
fast_env_profile=${FAST_ENV_PROFILE:-1}

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

if [[ "$fast_env_profile" == "1" ]]; then
  path_value=/root/.codex/tmp/arg0/codex-arg09RESQr:/usr/lib/node_modules/@openai/codex/node_modules/@openai/codex-linux-x64/vendor/x86_64-unknown-linux-musl/path:/root/.vscode-server/data/User/globalStorage/github.copilot-chat/debugCommand:/root/.vscode-server/data/User/globalStorage/github.copilot-chat/copilotCli:/root/.vscode-server/cli/servers/Stable-e7fb5e96c0730b9deb70b33781f98e2f35975036/server/bin/remote-cli:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin:/usr/games:/usr/local/games:/snap/bin:/root/bake/build/bin:/root/bake/build/bin_dce:/root/bake/build/bin:/root/bake/build/bin_dce
  env_cmd=(
    env
    PATH="$path_value"
    LD_LIBRARY_PATH="$ld_library_path"
    DCE_PATH="$dce_path"
  )
  env_label=fast
else
  env_cmd=(
    env
    LD_LIBRARY_PATH="$ld_library_path"
    DCE_PATH="$dce_path"
  )
  env_label=default
fi

cd "$workdir"

echo "[run-120-demo-env-profile] build_mode=${build_mode} env_profile=${env_label} timeout=${wait_seconds}s"
echo "[run-120-demo-env-profile] binary=${binary}"

exec /usr/bin/time -f 'real=%e user=%U sys=%S maxrss=%M exit=%x' \
  /usr/bin/timeout "${wait_seconds}s" \
  "${env_cmd[@]}" \
  "$binary" "${args[@]}"
