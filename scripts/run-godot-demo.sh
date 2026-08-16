#!/usr/bin/env bash
# Godot 集成示例启动脚本：优先使用 Intel 核显硬件渲染（本机为 Optimus 双显卡：
# Intel UHD Graphics + NVIDIA MX250；MESA 默认即核显，此处显式指定 iris 防漂移）。
# 无 /dev/dri 的环境（容器/沙箱）自动回退软件渲染并提示。
# 运行：./scripts/run-godot-demo.sh [godot 附加参数...]
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

DEMO_SO="game_adapter/godot_demo/bin/libnpc_agent_godot_demo.linux.release.x86_64.so"
if [ ! -f "$DEMO_SO" ]; then
  echo "[demo] 未找到扩展库 $DEMO_SO"
  echo "[demo] 请先构建：cmake --preset godot && cmake --build --preset godot"
  exit 1
fi

if [ -d /dev/dri ] && compgen -G "/dev/dri/renderD*" >/dev/null; then
  # 核显硬件渲染：启动日志应出现
  #   Using Device: Intel - Mesa Intel(R) UHD Graphics ...
  # 若出现 llvmpipe 则说明仍为软件渲染（检查 /dev/dri 权限）。
  export MESA_LOADER_DRIVER_OVERRIDE=iris
  echo "[demo] 已检测到 /dev/dri，使用 Intel 核显（iris）硬件渲染"
else
  echo "[demo] 警告：当前环境无 /dev/dri（容器/沙箱），回退软件渲染（llvmpipe）"
fi

exec godot --path game_adapter/godot_demo "$@"
