6 Clients BUILD_MODE=debug && FAST_ENV_PROFILE=1:

```txt
root@ubuntu:~/bake/source/ns-3-dce# BUILD_MODE=debug WAIT_SECONDS=900 FAST_ENV_PROFILE=1 KILL_ON_TIMEOUT=1 \
> myscripts/120-demo/debug/run-120-demo-no-tty.sh /tmp/120-demo-debug-6-fast \
>   --numClients=6 --numAps=1 --simTime=3.0 \
>   --sinkStart=0.5 --clientStart=1.0 --clientStartJitter=0.0 \
>   --appSteadyRate=100Mbps --appBurstRate=100Mbps \
>   --wifiEnableOfdma=false --wifiEnableUlOfdma=false --wifiEnableBsrp=false \
>   --checkBackboneDualTraffic=1
submitted at job 30
prefix=/tmp/120-demo-debug-6-fast
job_script=/tmp/120-demo-at-job.ODWGpl.sh
done_status=0
== /tmp/120-demo-debug-6-fast.submit-env ==
submit_time=2026-04-05T03:40:38Z
submit_pid=7408
submit_ppid=2182
submit_tty=pts/1
submit_pwd=/root/bake/source/ns-3-dce
submit_shell=/bin/bash
submit_term=xterm-256color
submit_shlvl=2
submit_session=2182
submit_pgrp=7408
submit_tty_nr=34817
submit_tpgid=7408
== env ==
BROWSER=/root/.vscode-server/cli/servers/Stable-e7fb5e96c0730b9deb70b33781f98e2f35975036/server/bin/helpers/browser.sh
BUILD_MODE=debug
COLORTERM=truecolor
DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/0/bus
DCE_PATH=/root/bake/build/bin:/root/bake/build/bin_dce:/root/bake/build/sbin
FAST_ENV_PROFILE=1
GIT_ASKPASS=/root/.vscode-server/cli/servers/Stable-e7fb5e96c0730b9deb70b33781f98e2f35975036/server/extensions/git/dist/askpass.sh
HOME=/root
KILL_ON_TIMEOUT=1
LANG=en_US.UTF-8
LD_LIBRARY_PATH=/root/bake/build/lib:/root/bake/build/lib
LESSCLOSE=/usr/bin/lesspipe %s %s
LESSOPEN=| /usr/bin/lesspipe %s
LOGNAME=root
LS_COLORS=rs=0:di=01;34:ln=01;36:mh=00:pi=40;33:so=01;35:do=01;35:bd=40;33;01:cd=40;33;01:or=40;31;01:mi=00:su=37;41:sg=30;43:ca=30;41:tw=30;42:ow=34;42:st=37;44:ex=01;32:*.tar=01;31:*.tgz=01;31:*.arc=01;31:*.arj=01;31:*.taz=01;31:*.lha=01;31:*.lz4=01;31:*.lzh=01;31:*.lzma=01;31:*.tlz=01;31:*.txz=01;31:*.tzo=01;31:*.t7z=01;31:*.zip=01;31:*.z=01;31:*.dz=01;31:*.gz=01;31:*.lrz=01;31:*.lz=01;31:*.lzo=01;31:*.xz=01;31:*.zst=01;31:*.tzst=01;31:*.bz2=01;31:*.bz=01;31:*.tbz=01;31:*.tbz2=01;31:*.tz=01;31:*.deb=01;31:*.rpm=01;31:*.jar=01;31:*.war=01;31:*.ear=01;31:*.sar=01;31:*.rar=01;31:*.alz=01;31:*.ace=01;31:*.zoo=01;31:*.cpio=01;31:*.7z=01;31:*.rz=01;31:*.cab=01;31:*.wim=01;31:*.swm=01;31:*.dwm=01;31:*.esd=01;31:*.jpg=01;35:*.jpeg=01;35:*.mjpg=01;35:*.mjpeg=01;35:*.gif=01;35:*.bmp=01;35:*.pbm=01;35:*.pgm=01;35:*.ppm=01;35:*.tga=01;35:*.xbm=01;35:*.xpm=01;35:*.tif=01;35:*.tiff=01;35:*.png=01;35:*.svg=01;35:*.svgz=01;35:*.mng=01;35:*.pcx=01;35:*.mov=01;35:*.mpg=01;35:*.mpeg=01;35:*.m2v=01;35:*.mkv=01;35:*.webm=01;35:*.ogm=01;35:*.mp4=01;35:*.m4v=01;35:*.mp4v=01;35:*.vob=01;35:*.qt=01;35:*.nuv=01;35:*.wmv=01;35:*.asf=01;35:*.rm=01;35:*.rmvb=01;35:*.flc=01;35:*.avi=01;35:*.fli=01;35:*.flv=01;35:*.gl=01;35:*.dl=01;35:*.xcf=01;35:*.xwd=01;35:*.yuv=01;35:*.cgm=01;35:*.emf=01;35:*.ogv=01;35:*.ogx=01;35:*.aac=00;36:*.au=00;36:*.flac=00;36:*.m4a=00;36:*.mid=00;36:*.midi=00;36:*.mka=00;36:*.mp3=00;36:*.mpc=00;36:*.ogg=00;36:*.ra=00;36:*.wav=00;36:*.oga=00;36:*.opus=00;36:*.spx=00;36:*.xspf=00;36:
MOTD_SHOWN=pam
OLDPWD=/root/bake/source
PATH=/root/.vscode-server/data/User/globalStorage/github.copilot-chat/debugCommand:/root/.vscode-server/data/User/globalStorage/github.copilot-chat/copilotCli:/root/.vscode-server/cli/servers/Stable-e7fb5e96c0730b9deb70b33781f98e2f35975036/server/bin/remote-cli:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin:/usr/games:/usr/local/games:/snap/bin:/root/bake/build/bin:/root/bake/build/bin_dce:/root/bake/build/bin:/root/bake/build/bin_dce
PWD=/root/bake/source/ns-3-dce
SHELL=/bin/bash
SHLVL=1
SSH_CLIENT=192.168.237.1 56900 22
SSH_CONNECTION=192.168.237.1 56900 192.168.237.128 22
TERM_PROGRAM_VERSION=1.114.0
TERM_PROGRAM=vscode
TERM=xterm-256color
USER=root
_=/usr/bin/env
VSCODE_GIT_ASKPASS_EXTRA_ARGS=
VSCODE_GIT_ASKPASS_MAIN=/root/.vscode-server/cli/servers/Stable-e7fb5e96c0730b9deb70b33781f98e2f35975036/server/extensions/git/dist/askpass-main.js
VSCODE_GIT_ASKPASS_NODE=/root/.vscode-server/cli/servers/Stable-e7fb5e96c0730b9deb70b33781f98e2f35975036/server/node
VSCODE_GIT_IPC_HANDLE=/run/user/0/vscode-git-49f9efc92d.sock
VSCODE_IPC_HOOK_CLI=/run/user/0/vscode-ipc-3657f9c0-970d-4a39-b67b-c8d54e73645a.sock
VSCODE_NONCE=57294045-4ab3-46c6-b7be-62fbdf4377e5
WAIT_SECONDS=900
XDG_DATA_DIRS=/usr/local/share:/usr/share:/var/lib/snapd/desktop
XDG_RUNTIME_DIR=/run/user/0
XDG_SESSION_CLASS=user
XDG_SESSION_ID=1
XDG_SESSION_TYPE=tty
== /tmp/120-demo-debug-6-fast.job-meta ==
job_time=2026-04-05T03:40:38Z
job_pid=7438
job_ppid=7434
job_tty=?
job_pwd=/root/bake/source/ns-3-dce
job_shell=/bin/bash
job_term=dumb
job_shlvl=2
job_session=1048
job_pgrp=1048
job_tty_nr=0
job_tpgid=-1
== /tmp/120-demo-debug-6-fast.job-ps ==
    PID    PPID TT       STAT PRI  NI PSR %CPU %MEM     ELAPSED CMD
   7438    7434 ?        SN    17   2   5  0.0  0.0       00:00 /bin/bash /tmp/120-demo-at-job.ODWGpl.sh
== /tmp/120-demo-debug-6-fast.job-cgroup ==
12:freezer:/
11:cpuset:/
10:memory:/system.slice/atd.service
9:blkio:/system.slice/atd.service
8:cpu,cpuacct:/system.slice/atd.service
7:devices:/system.slice/atd.service
6:rdma:/
5:hugetlb:/
4:net_cls,net_prio:/
3:pids:/system.slice/atd.service
2:perf_event:/
1:name=systemd:/system.slice/atd.service
0::/system.slice/atd.service
== /tmp/120-demo-debug-6-fast.time ==
real=130.81 user=129.68 sys=0.97 maxrss=162324 exit=0
== /tmp/120-demo-debug-6-fast.out ==
[120-demo] UE RRC summary (t=0.9 @0.9s) connected=6/6 randomAccess=0 idleConnecting=0 other=0
[120-demo] UE RRC summary (pre-clientStart @0.95s) connected=6/6 randomAccess=0 idleConnecting=0 other=0
[120-demo] UE RRC summary (pre-stop @2.95s) connected=6/6 randomAccess=0 idleConnecting=0 other=0
[120-demo] PacketSink totalRxBytes=94162476 acceptedSockets=0 payloadStartSeconds=1.213 activeSeconds=1.787 throughputMbps=421.596
[120-demo] Client connect summary: connected=6/6 inProgress=0 totalConnectFailures=0
[120-demo] Backbone dual-traffic summary: nrActive=6/6 wifiActive=5/6 bothActive=5/6
[120-demo] Backbone missing WiFi payload UEs: 3
== /tmp/120-demo-debug-6-fast.err ==
[120-demo] NR defaults: freq=4.9GHz bw=100MHz numerology=1 pattern="DL|F|UL|UL|UL|" errorModel=NrEesmCcT2(Table2,256QAM) amcModel=ErrorModel fixedMcsUl=1 startingMcsUl=25 fixedMcsDl=0 startingMcsDl=25 ueTxPower=23dBm gnbTxPower=40dBm ueAnt=1x2 gnbAnt=2x2
[120-demo] WiFi defaults: phy=Spectrum 802.11ax_5GHz width=160MHz freq=5250MHz heGi=800ns heMpduBuffer=256 beMaxAmpdu=6500631 txPower=23dBm apAnt=4x4 staAnt=2x2 dlOfdma=off ulOfdma=off bsrp=off
[120-demo] WiFi STA split: ap1=6
```

6 clients BUILD_MODE=debug && FAST_ENV_PROFILE=0:

```txt
root@ubuntu:~/bake/source/ns-3-dce# BUILD_MODE=debug WAIT_SECONDS=300 FAST_ENV_PROFILE=0 KILL_ON_TIMEOUT=1 \
> myscripts/120-demo/debug/run-120-demo-no-tty.sh /tmp/120-demo-debug-6-slow \
>   --numClients=6 --numAps=1 --simTime=3.0 \
>   --sinkStart=0.5 --clientStart=1.0 --clientStartJitter=0.0 \
>   --appSteadyRate=100Mbps --appBurstRate=100Mbps \
>   --wifiEnableOfdma=false --wifiEnableUlOfdma=false --wifiEnableBsrp=false \
>   --checkBackboneDualTraffic=1
submitted at job 31
prefix=/tmp/120-demo-debug-6-slow
job_script=/tmp/120-demo-at-job.AlGWhE.sh
done_status=0
== /tmp/120-demo-debug-6-slow.submit-env ==
submit_time=2026-04-05T03:45:18Z
submit_pid=8084
submit_ppid=2182
submit_tty=pts/1
submit_pwd=/root/bake/source/ns-3-dce
submit_shell=/bin/bash
submit_term=xterm-256color
submit_shlvl=2
submit_session=2182
submit_pgrp=8084
submit_tty_nr=34817
submit_tpgid=8084
== env ==
BROWSER=/root/.vscode-server/cli/servers/Stable-e7fb5e96c0730b9deb70b33781f98e2f35975036/server/bin/helpers/browser.sh
BUILD_MODE=debug
COLORTERM=truecolor
DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/0/bus
DCE_PATH=/root/bake/build/bin:/root/bake/build/bin_dce:/root/bake/build/sbin
FAST_ENV_PROFILE=0
GIT_ASKPASS=/root/.vscode-server/cli/servers/Stable-e7fb5e96c0730b9deb70b33781f98e2f35975036/server/extensions/git/dist/askpass.sh
HOME=/root
KILL_ON_TIMEOUT=1
LANG=en_US.UTF-8
LD_LIBRARY_PATH=/root/bake/build/lib:/root/bake/build/lib
LESSCLOSE=/usr/bin/lesspipe %s %s
LESSOPEN=| /usr/bin/lesspipe %s
LOGNAME=root
LS_COLORS=rs=0:di=01;34:ln=01;36:mh=00:pi=40;33:so=01;35:do=01;35:bd=40;33;01:cd=40;33;01:or=40;31;01:mi=00:su=37;41:sg=30;43:ca=30;41:tw=30;42:ow=34;42:st=37;44:ex=01;32:*.tar=01;31:*.tgz=01;31:*.arc=01;31:*.arj=01;31:*.taz=01;31:*.lha=01;31:*.lz4=01;31:*.lzh=01;31:*.lzma=01;31:*.tlz=01;31:*.txz=01;31:*.tzo=01;31:*.t7z=01;31:*.zip=01;31:*.z=01;31:*.dz=01;31:*.gz=01;31:*.lrz=01;31:*.lz=01;31:*.lzo=01;31:*.xz=01;31:*.zst=01;31:*.tzst=01;31:*.bz2=01;31:*.bz=01;31:*.tbz=01;31:*.tbz2=01;31:*.tz=01;31:*.deb=01;31:*.rpm=01;31:*.jar=01;31:*.war=01;31:*.ear=01;31:*.sar=01;31:*.rar=01;31:*.alz=01;31:*.ace=01;31:*.zoo=01;31:*.cpio=01;31:*.7z=01;31:*.rz=01;31:*.cab=01;31:*.wim=01;31:*.swm=01;31:*.dwm=01;31:*.esd=01;31:*.jpg=01;35:*.jpeg=01;35:*.mjpg=01;35:*.mjpeg=01;35:*.gif=01;35:*.bmp=01;35:*.pbm=01;35:*.pgm=01;35:*.ppm=01;35:*.tga=01;35:*.xbm=01;35:*.xpm=01;35:*.tif=01;35:*.tiff=01;35:*.png=01;35:*.svg=01;35:*.svgz=01;35:*.mng=01;35:*.pcx=01;35:*.mov=01;35:*.mpg=01;35:*.mpeg=01;35:*.m2v=01;35:*.mkv=01;35:*.webm=01;35:*.ogm=01;35:*.mp4=01;35:*.m4v=01;35:*.mp4v=01;35:*.vob=01;35:*.qt=01;35:*.nuv=01;35:*.wmv=01;35:*.asf=01;35:*.rm=01;35:*.rmvb=01;35:*.flc=01;35:*.avi=01;35:*.fli=01;35:*.flv=01;35:*.gl=01;35:*.dl=01;35:*.xcf=01;35:*.xwd=01;35:*.yuv=01;35:*.cgm=01;35:*.emf=01;35:*.ogv=01;35:*.ogx=01;35:*.aac=00;36:*.au=00;36:*.flac=00;36:*.m4a=00;36:*.mid=00;36:*.midi=00;36:*.mka=00;36:*.mp3=00;36:*.mpc=00;36:*.ogg=00;36:*.ra=00;36:*.wav=00;36:*.oga=00;36:*.opus=00;36:*.spx=00;36:*.xspf=00;36:
MOTD_SHOWN=pam
OLDPWD=/root/bake/source
PATH=/root/.vscode-server/data/User/globalStorage/github.copilot-chat/debugCommand:/root/.vscode-server/data/User/globalStorage/github.copilot-chat/copilotCli:/root/.vscode-server/cli/servers/Stable-e7fb5e96c0730b9deb70b33781f98e2f35975036/server/bin/remote-cli:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin:/usr/games:/usr/local/games:/snap/bin:/root/bake/build/bin:/root/bake/build/bin_dce:/root/bake/build/bin:/root/bake/build/bin_dce
PWD=/root/bake/source/ns-3-dce
SHELL=/bin/bash
SHLVL=1
SSH_CLIENT=192.168.237.1 56900 22
SSH_CONNECTION=192.168.237.1 56900 192.168.237.128 22
TERM_PROGRAM_VERSION=1.114.0
TERM_PROGRAM=vscode
TERM=xterm-256color
USER=root
_=/usr/bin/env
VSCODE_GIT_ASKPASS_EXTRA_ARGS=
VSCODE_GIT_ASKPASS_MAIN=/root/.vscode-server/cli/servers/Stable-e7fb5e96c0730b9deb70b33781f98e2f35975036/server/extensions/git/dist/askpass-main.js
VSCODE_GIT_ASKPASS_NODE=/root/.vscode-server/cli/servers/Stable-e7fb5e96c0730b9deb70b33781f98e2f35975036/server/node
VSCODE_GIT_IPC_HANDLE=/run/user/0/vscode-git-49f9efc92d.sock
VSCODE_IPC_HOOK_CLI=/run/user/0/vscode-ipc-3657f9c0-970d-4a39-b67b-c8d54e73645a.sock
VSCODE_NONCE=57294045-4ab3-46c6-b7be-62fbdf4377e5
WAIT_SECONDS=300
XDG_DATA_DIRS=/usr/local/share:/usr/share:/var/lib/snapd/desktop
XDG_RUNTIME_DIR=/run/user/0
XDG_SESSION_CLASS=user
XDG_SESSION_ID=1
XDG_SESSION_TYPE=tty
== /tmp/120-demo-debug-6-slow.job-meta ==
job_time=2026-04-05T03:45:18Z
job_pid=8108
job_ppid=8107
job_tty=?
job_pwd=/root/bake/source/ns-3-dce
job_shell=/bin/bash
job_term=dumb
job_shlvl=2
job_session=1048
job_pgrp=1048
job_tty_nr=0
job_tpgid=-1
== /tmp/120-demo-debug-6-slow.job-ps ==
    PID    PPID TT       STAT PRI  NI PSR %CPU %MEM     ELAPSED CMD
   8108    8107 ?        SN    17   2   0  0.0  0.0       00:00 /bin/bash /tmp/120-demo-at-job.AlGWhE.sh
== /tmp/120-demo-debug-6-slow.job-cgroup ==
12:freezer:/
11:cpuset:/
10:memory:/system.slice/atd.service
9:blkio:/system.slice/atd.service
8:cpu,cpuacct:/system.slice/atd.service
7:devices:/system.slice/atd.service
6:rdma:/
5:hugetlb:/
4:net_cls,net_prio:/
3:pids:/system.slice/atd.service
2:perf_event:/
1:name=systemd:/system.slice/atd.service
0::/system.slice/atd.service
== /tmp/120-demo-debug-6-slow.time ==
real=122.15 user=121.31 sys=0.80 maxrss=150200 exit=0
== /tmp/120-demo-debug-6-slow.out ==
[120-demo] UE RRC summary (t=0.9 @0.9s) connected=6/6 randomAccess=0 idleConnecting=0 other=0
[120-demo] UE RRC summary (pre-clientStart @0.95s) connected=6/6 randomAccess=0 idleConnecting=0 other=0
[120-demo] UE RRC summary (pre-stop @2.95s) connected=6/6 randomAccess=0 idleConnecting=0 other=0
[120-demo] PacketSink totalRxBytes=94903968 acceptedSockets=0 payloadStartSeconds=1.213 activeSeconds=1.787 throughputMbps=424.916
[120-demo] Client connect summary: connected=6/6 inProgress=0 totalConnectFailures=0
[120-demo] Backbone dual-traffic summary: nrActive=6/6 wifiActive=5/6 bothActive=5/6
[120-demo] Backbone missing WiFi payload UEs: 0
== /tmp/120-demo-debug-6-slow.err ==
[120-demo] NR defaults: freq=4.9GHz bw=100MHz numerology=1 pattern="DL|F|UL|UL|UL|" errorModel=NrEesmCcT2(Table2,256QAM) amcModel=ErrorModel fixedMcsUl=1 startingMcsUl=25 fixedMcsDl=0 startingMcsDl=25 ueTxPower=23dBm gnbTxPower=40dBm ueAnt=1x2 gnbAnt=2x2
[120-demo] WiFi defaults: phy=Spectrum 802.11ax_5GHz width=160MHz freq=5250MHz heGi=800ns heMpduBuffer=256 beMaxAmpdu=6500631 txPower=23dBm apAnt=4x4 staAnt=2x2 dlOfdma=off ulOfdma=off bsrp=off
[120-demo] WiFi STA split: ap1=6
```

6 Clients FAST_ENV_PROFILE=0

```txt
root@ubuntu:~/bake/source/ns-3-dce# WAIT_SECONDS=300 FAST_ENV_PROFILE=0 KILL_ON_TIMEOUT=1 \
> myscripts/120-demo/debug/run-120-demo-no-tty.sh /tmp/120-demo-debug-6-slow \
>   --numClients=6 --numAps=1 --simTime=3.0 \
>   --sinkStart=0.5 --clientStart=1.0 --clientStartJitter=0.0 \
>   --appSteadyRate=100Mbps --appBurstRate=100Mbps \
>   --wifiEnableOfdma=false --wifiEnableUlOfdma=false --wifiEnableBsrp=false \
>   --checkBackboneDualTraffic=1
submitted at job 32
prefix=/tmp/120-demo-debug-6-slow
job_script=/tmp/120-demo-at-job.Lxm8KW.sh
done_status=0
== /tmp/120-demo-debug-6-slow.submit-env ==
submit_time=2026-04-05T03:49:52Z
submit_pid=8438
submit_ppid=2182
submit_tty=pts/1
submit_pwd=/root/bake/source/ns-3-dce
submit_shell=/bin/bash
submit_term=xterm-256color
submit_shlvl=2
submit_session=2182
submit_pgrp=8438
submit_tty_nr=34817
submit_tpgid=8438
== env ==
BROWSER=/root/.vscode-server/cli/servers/Stable-e7fb5e96c0730b9deb70b33781f98e2f35975036/server/bin/helpers/browser.sh
COLORTERM=truecolor
DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/0/bus
DCE_PATH=/root/bake/build/bin:/root/bake/build/bin_dce:/root/bake/build/sbin
FAST_ENV_PROFILE=0
GIT_ASKPASS=/root/.vscode-server/cli/servers/Stable-e7fb5e96c0730b9deb70b33781f98e2f35975036/server/extensions/git/dist/askpass.sh
HOME=/root
KILL_ON_TIMEOUT=1
LANG=en_US.UTF-8
LD_LIBRARY_PATH=/root/bake/build/lib:/root/bake/build/lib
LESSCLOSE=/usr/bin/lesspipe %s %s
LESSOPEN=| /usr/bin/lesspipe %s
LOGNAME=root
LS_COLORS=rs=0:di=01;34:ln=01;36:mh=00:pi=40;33:so=01;35:do=01;35:bd=40;33;01:cd=40;33;01:or=40;31;01:mi=00:su=37;41:sg=30;43:ca=30;41:tw=30;42:ow=34;42:st=37;44:ex=01;32:*.tar=01;31:*.tgz=01;31:*.arc=01;31:*.arj=01;31:*.taz=01;31:*.lha=01;31:*.lz4=01;31:*.lzh=01;31:*.lzma=01;31:*.tlz=01;31:*.txz=01;31:*.tzo=01;31:*.t7z=01;31:*.zip=01;31:*.z=01;31:*.dz=01;31:*.gz=01;31:*.lrz=01;31:*.lz=01;31:*.lzo=01;31:*.xz=01;31:*.zst=01;31:*.tzst=01;31:*.bz2=01;31:*.bz=01;31:*.tbz=01;31:*.tbz2=01;31:*.tz=01;31:*.deb=01;31:*.rpm=01;31:*.jar=01;31:*.war=01;31:*.ear=01;31:*.sar=01;31:*.rar=01;31:*.alz=01;31:*.ace=01;31:*.zoo=01;31:*.cpio=01;31:*.7z=01;31:*.rz=01;31:*.cab=01;31:*.wim=01;31:*.swm=01;31:*.dwm=01;31:*.esd=01;31:*.jpg=01;35:*.jpeg=01;35:*.mjpg=01;35:*.mjpeg=01;35:*.gif=01;35:*.bmp=01;35:*.pbm=01;35:*.pgm=01;35:*.ppm=01;35:*.tga=01;35:*.xbm=01;35:*.xpm=01;35:*.tif=01;35:*.tiff=01;35:*.png=01;35:*.svg=01;35:*.svgz=01;35:*.mng=01;35:*.pcx=01;35:*.mov=01;35:*.mpg=01;35:*.mpeg=01;35:*.m2v=01;35:*.mkv=01;35:*.webm=01;35:*.ogm=01;35:*.mp4=01;35:*.m4v=01;35:*.mp4v=01;35:*.vob=01;35:*.qt=01;35:*.nuv=01;35:*.wmv=01;35:*.asf=01;35:*.rm=01;35:*.rmvb=01;35:*.flc=01;35:*.avi=01;35:*.fli=01;35:*.flv=01;35:*.gl=01;35:*.dl=01;35:*.xcf=01;35:*.xwd=01;35:*.yuv=01;35:*.cgm=01;35:*.emf=01;35:*.ogv=01;35:*.ogx=01;35:*.aac=00;36:*.au=00;36:*.flac=00;36:*.m4a=00;36:*.mid=00;36:*.midi=00;36:*.mka=00;36:*.mp3=00;36:*.mpc=00;36:*.ogg=00;36:*.ra=00;36:*.wav=00;36:*.oga=00;36:*.opus=00;36:*.spx=00;36:*.xspf=00;36:
MOTD_SHOWN=pam
OLDPWD=/root/bake/source
PATH=/root/.vscode-server/data/User/globalStorage/github.copilot-chat/debugCommand:/root/.vscode-server/data/User/globalStorage/github.copilot-chat/copilotCli:/root/.vscode-server/cli/servers/Stable-e7fb5e96c0730b9deb70b33781f98e2f35975036/server/bin/remote-cli:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin:/usr/games:/usr/local/games:/snap/bin:/root/bake/build/bin:/root/bake/build/bin_dce:/root/bake/build/bin:/root/bake/build/bin_dce
PWD=/root/bake/source/ns-3-dce
SHELL=/bin/bash
SHLVL=1
SSH_CLIENT=192.168.237.1 56900 22
SSH_CONNECTION=192.168.237.1 56900 192.168.237.128 22
TERM_PROGRAM_VERSION=1.114.0
TERM_PROGRAM=vscode
TERM=xterm-256color
USER=root
_=/usr/bin/env
VSCODE_GIT_ASKPASS_EXTRA_ARGS=
VSCODE_GIT_ASKPASS_MAIN=/root/.vscode-server/cli/servers/Stable-e7fb5e96c0730b9deb70b33781f98e2f35975036/server/extensions/git/dist/askpass-main.js
VSCODE_GIT_ASKPASS_NODE=/root/.vscode-server/cli/servers/Stable-e7fb5e96c0730b9deb70b33781f98e2f35975036/server/node
VSCODE_GIT_IPC_HANDLE=/run/user/0/vscode-git-49f9efc92d.sock
VSCODE_IPC_HOOK_CLI=/run/user/0/vscode-ipc-3657f9c0-970d-4a39-b67b-c8d54e73645a.sock
VSCODE_NONCE=57294045-4ab3-46c6-b7be-62fbdf4377e5
WAIT_SECONDS=300
XDG_DATA_DIRS=/usr/local/share:/usr/share:/var/lib/snapd/desktop
XDG_RUNTIME_DIR=/run/user/0
XDG_SESSION_CLASS=user
XDG_SESSION_ID=1
XDG_SESSION_TYPE=tty
== /tmp/120-demo-debug-6-slow.job-meta ==
job_time=2026-04-05T03:49:52Z
job_pid=8462
job_ppid=8461
job_tty=?
job_pwd=/root/bake/source/ns-3-dce
job_shell=/bin/bash
job_term=dumb
job_shlvl=2
job_session=1048
job_pgrp=1048
job_tty_nr=0
job_tpgid=-1
== /tmp/120-demo-debug-6-slow.job-ps ==
    PID    PPID TT       STAT PRI  NI PSR %CPU %MEM     ELAPSED CMD
   8462    8461 ?        SN    17   2   2  0.0  0.0       00:00 /bin/bash /tmp/120-demo-at-job.Lxm8KW.sh
== /tmp/120-demo-debug-6-slow.job-cgroup ==
12:freezer:/
11:cpuset:/
10:memory:/system.slice/atd.service
9:blkio:/system.slice/atd.service
8:cpu,cpuacct:/system.slice/atd.service
7:devices:/system.slice/atd.service
6:rdma:/
5:hugetlb:/
4:net_cls,net_prio:/
3:pids:/system.slice/atd.service
2:perf_event:/
1:name=systemd:/system.slice/atd.service
0::/system.slice/atd.service
== /tmp/120-demo-debug-6-slow.time ==
real=26.06 user=25.04 sys=0.91 maxrss=114252 exit=0
== /tmp/120-demo-debug-6-slow.out ==
[120-demo] UE RRC summary (t=0.9 @0.9s) connected=6/6 randomAccess=0 idleConnecting=0 other=0
[120-demo] UE RRC summary (pre-clientStart @0.95s) connected=6/6 randomAccess=0 idleConnecting=0 other=0
[120-demo] UE RRC summary (pre-stop @2.95s) connected=6/6 randomAccess=0 idleConnecting=0 other=0
[120-demo] PacketSink totalRxBytes=94181664 acceptedSockets=0 payloadStartSeconds=1.213 activeSeconds=1.787 throughputMbps=421.682
[120-demo] Client connect summary: connected=6/6 inProgress=0 totalConnectFailures=0
[120-demo] Backbone dual-traffic summary: nrActive=6/6 wifiActive=6/6 bothActive=6/6 nrPayloadBytes=41657646 wifiPayloadBytes=53466030 nrThroughputMbps=186.515 wifiThroughputMbps=239.385
```

9 Clients BUILD_MODE=debug FAST_ENV_PROFILE=1

```txt
root@ubuntu:~/bake/source/ns-3-dce# BUILD_MODE=debug WAIT_SECONDS=300 FAST_ENV_PROFILE=1 KILL_ON_TIMEOUT=1 \
> myscripts/120-demo/debug/run-120-demo-no-tty.sh /tmp/120-demo-debug-6-slow \
>   --numClients=9 --numAps=1 --simTime=3.0 \
>   --sinkStart=0.5 --clientStart=1.0 --clientStartJitter=0.0 \
>   --appSteadyRate=100Mbps --appBurstRate=100Mbps \
>   --wifiEnableOfdma=false --wifiEnableUlOfdma=false --wifiEnableBsrp=false \
>   --checkBackboneDualTraffic=1
submitted at job 33
prefix=/tmp/120-demo-debug-6-slow
job_script=/tmp/120-demo-at-job.J5Mfdl.sh
timed out waiting for /tmp/120-demo-debug-6-slow.done after 300s
== /tmp/120-demo-debug-6-slow.submit-env ==
submit_time=2026-04-05T03:51:28Z
submit_pid=8697
submit_ppid=2182
submit_tty=pts/1
submit_pwd=/root/bake/source/ns-3-dce
submit_shell=/bin/bash
submit_term=xterm-256color
submit_shlvl=2
submit_session=2182
submit_pgrp=8697
submit_tty_nr=34817
submit_tpgid=8697
== env ==
BROWSER=/root/.vscode-server/cli/servers/Stable-e7fb5e96c0730b9deb70b33781f98e2f35975036/server/bin/helpers/browser.sh
BUILD_MODE=debug
COLORTERM=truecolor
DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/0/bus
DCE_PATH=/root/bake/build/bin:/root/bake/build/bin_dce:/root/bake/build/sbin
FAST_ENV_PROFILE=1
GIT_ASKPASS=/root/.vscode-server/cli/servers/Stable-e7fb5e96c0730b9deb70b33781f98e2f35975036/server/extensions/git/dist/askpass.sh
HOME=/root
KILL_ON_TIMEOUT=1
LANG=en_US.UTF-8
LD_LIBRARY_PATH=/root/bake/build/lib:/root/bake/build/lib
LESSCLOSE=/usr/bin/lesspipe %s %s
LESSOPEN=| /usr/bin/lesspipe %s
LOGNAME=root
LS_COLORS=rs=0:di=01;34:ln=01;36:mh=00:pi=40;33:so=01;35:do=01;35:bd=40;33;01:cd=40;33;01:or=40;31;01:mi=00:su=37;41:sg=30;43:ca=30;41:tw=30;42:ow=34;42:st=37;44:ex=01;32:*.tar=01;31:*.tgz=01;31:*.arc=01;31:*.arj=01;31:*.taz=01;31:*.lha=01;31:*.lz4=01;31:*.lzh=01;31:*.lzma=01;31:*.tlz=01;31:*.txz=01;31:*.tzo=01;31:*.t7z=01;31:*.zip=01;31:*.z=01;31:*.dz=01;31:*.gz=01;31:*.lrz=01;31:*.lz=01;31:*.lzo=01;31:*.xz=01;31:*.zst=01;31:*.tzst=01;31:*.bz2=01;31:*.bz=01;31:*.tbz=01;31:*.tbz2=01;31:*.tz=01;31:*.deb=01;31:*.rpm=01;31:*.jar=01;31:*.war=01;31:*.ear=01;31:*.sar=01;31:*.rar=01;31:*.alz=01;31:*.ace=01;31:*.zoo=01;31:*.cpio=01;31:*.7z=01;31:*.rz=01;31:*.cab=01;31:*.wim=01;31:*.swm=01;31:*.dwm=01;31:*.esd=01;31:*.jpg=01;35:*.jpeg=01;35:*.mjpg=01;35:*.mjpeg=01;35:*.gif=01;35:*.bmp=01;35:*.pbm=01;35:*.pgm=01;35:*.ppm=01;35:*.tga=01;35:*.xbm=01;35:*.xpm=01;35:*.tif=01;35:*.tiff=01;35:*.png=01;35:*.svg=01;35:*.svgz=01;35:*.mng=01;35:*.pcx=01;35:*.mov=01;35:*.mpg=01;35:*.mpeg=01;35:*.m2v=01;35:*.mkv=01;35:*.webm=01;35:*.ogm=01;35:*.mp4=01;35:*.m4v=01;35:*.mp4v=01;35:*.vob=01;35:*.qt=01;35:*.nuv=01;35:*.wmv=01;35:*.asf=01;35:*.rm=01;35:*.rmvb=01;35:*.flc=01;35:*.avi=01;35:*.fli=01;35:*.flv=01;35:*.gl=01;35:*.dl=01;35:*.xcf=01;35:*.xwd=01;35:*.yuv=01;35:*.cgm=01;35:*.emf=01;35:*.ogv=01;35:*.ogx=01;35:*.aac=00;36:*.au=00;36:*.flac=00;36:*.m4a=00;36:*.mid=00;36:*.midi=00;36:*.mka=00;36:*.mp3=00;36:*.mpc=00;36:*.ogg=00;36:*.ra=00;36:*.wav=00;36:*.oga=00;36:*.opus=00;36:*.spx=00;36:*.xspf=00;36:
MOTD_SHOWN=pam
OLDPWD=/root/bake/source
PATH=/root/.vscode-server/data/User/globalStorage/github.copilot-chat/debugCommand:/root/.vscode-server/data/User/globalStorage/github.copilot-chat/copilotCli:/root/.vscode-server/cli/servers/Stable-e7fb5e96c0730b9deb70b33781f98e2f35975036/server/bin/remote-cli:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin:/usr/games:/usr/local/games:/snap/bin:/root/bake/build/bin:/root/bake/build/bin_dce:/root/bake/build/bin:/root/bake/build/bin_dce
PWD=/root/bake/source/ns-3-dce
SHELL=/bin/bash
SHLVL=1
SSH_CLIENT=192.168.237.1 56900 22
SSH_CONNECTION=192.168.237.1 56900 192.168.237.128 22
TERM_PROGRAM_VERSION=1.114.0
TERM_PROGRAM=vscode
TERM=xterm-256color
USER=root
_=/usr/bin/env
VSCODE_GIT_ASKPASS_EXTRA_ARGS=
VSCODE_GIT_ASKPASS_MAIN=/root/.vscode-server/cli/servers/Stable-e7fb5e96c0730b9deb70b33781f98e2f35975036/server/extensions/git/dist/askpass-main.js
VSCODE_GIT_ASKPASS_NODE=/root/.vscode-server/cli/servers/Stable-e7fb5e96c0730b9deb70b33781f98e2f35975036/server/node
VSCODE_GIT_IPC_HANDLE=/run/user/0/vscode-git-49f9efc92d.sock
VSCODE_IPC_HOOK_CLI=/run/user/0/vscode-ipc-3657f9c0-970d-4a39-b67b-c8d54e73645a.sock
VSCODE_NONCE=57294045-4ab3-46c6-b7be-62fbdf4377e5
WAIT_SECONDS=300
XDG_DATA_DIRS=/usr/local/share:/usr/share:/var/lib/snapd/desktop
XDG_RUNTIME_DIR=/run/user/0
XDG_SESSION_CLASS=user
XDG_SESSION_ID=1
XDG_SESSION_TYPE=tty
== /tmp/120-demo-debug-6-slow.job-meta ==
job_time=2026-04-05T03:51:28Z
job_pid=8721
job_ppid=8720
job_tty=?
job_pwd=/root/bake/source/ns-3-dce
job_shell=/bin/bash
job_term=dumb
job_shlvl=2
job_session=1048
job_pgrp=1048
job_tty_nr=0
job_tpgid=-1
== /tmp/120-demo-debug-6-slow.job-ps ==
    PID    PPID TT       STAT PRI  NI PSR %CPU %MEM     ELAPSED CMD
   8721    8720 ?        SN    17   2   8  0.0  0.0       00:00 /bin/bash /tmp/120-demo-at-job.J5Mfdl.sh
== /tmp/120-demo-debug-6-slow.job-cgroup ==
12:freezer:/
11:cpuset:/
10:memory:/system.slice/atd.service
9:blkio:/system.slice/atd.service
8:cpu,cpuacct:/system.slice/atd.service
7:devices:/system.slice/atd.service
6:rdma:/
5:hugetlb:/
4:net_cls,net_prio:/
3:pids:/system.slice/atd.service
2:perf_event:/
1:name=systemd:/system.slice/atd.service
0::/system.slice/atd.service
== running cmd pid ==
cmd_pid=8735
    PID    PPID TT       STAT PRI  NI PSR %CPU %MEM     ELAPSED CMD
   8735    8721 ?        SN    17   2   6  0.0  0.0       05:00 /usr/bin/time -o /tmp/120-demo-debug-6-slow.time -f real=%e user=%U sys=%S maxrss=%M exit=%x env HOME=/ro
== running children ==
    PID    PPID TT       STAT PRI  NI PSR %CPU %MEM     ELAPSED CMD
   8736    8735 ?        RN    17   2   7  100  4.2       05:00 /root/bake/source/ns-3-dce/build/myscripts/120-demo/bin/120-demo --numClients=9 --numAps=1 --simTime=3.0 
killed_timed_out_process_tree=1
== partial /tmp/120-demo-debug-6-slow.time ==
== partial /tmp/120-demo-debug-6-slow.out ==
[120-demo] UE RRC summary (t=0.9 @0.9s) connected=9/9 randomAccess=0 idleConnecting=0 other=0
[120-demo] UE RRC summary (pre-clientStart @0.95s) connected=9/9 randomAccess=0 idleConnecting=0 other=0
== partial /tmp/120-demo-debug-6-slow.err ==
[120-demo] NR defaults: freq=4.9GHz bw=100MHz numerology=1 pattern="DL|F|UL|UL|UL|" errorModel=NrEesmCcT2(Table2,256QAM) amcModel=ErrorModel fixedMcsUl=1 startingMcsUl=25 fixedMcsDl=0 startingMcsDl=25 ueTxPower=23dBm gnbTxPower=40dBm ueAnt=1x2 gnbAnt=2x2
[120-demo] WiFi defaults: phy=Spectrum 802.11ax_5GHz width=160MHz freq=5250MHz heGi=800ns heMpduBuffer=256 beMaxAmpdu=6500631 txPower=23dBm apAnt=4x4 staAnt=2x2 dlOfdma=off ulOfdma=off bsrp=off
[120-demo] WiFi STA split: ap1=9
```

debug 版本输出结果还不一样，一个是 UE 3 没有 payload，一个是 UE 0 没有 payload。这是为啥？是我之前对 ns-3 或者 dce 的修改引入了 bug 吗？还是说本来代码就有 bug？