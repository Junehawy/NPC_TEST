#!/usr/bin/env bash
# Push 前门禁（docs/review-process.md §2 第一步）：
#   1) 依赖方向检查  2) 构建(-Werror)  3) ctest  4) clang-format  5) Godot 演示冒烟(可选)
# 任一失败即退出非零 = 不得 push。
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

fail() { echo "[GATE] FAIL: $1"; exit 1; }
ok()   { echo "[GATE] ok: $1"; }

echo "== 1/5 依赖方向检查 =="
if grep -rn --include='*.h' --include='*.cpp' \
     -E '#include[[:space:]]*[<"](game_adapter|glm|entt|SFML|godot|UnrealEngine)' \
     npc_agent/ 2>/dev/null; then
  fail "npc_agent 核心出现越界 include（game_adapter 或第三方类型）——违反 CS-§1.3"
fi
ok "依赖方向"

echo "== 2/5 构建（preset: release，-Werror 零警告）=="
cmake --preset release >/dev/null
cmake --build --preset release >/dev/null
ok "构建"

echo "== 3/5 ctest =="
if [ -f build/CTestTestfile.cmake ]; then
  (cd build && ctest --output-on-failure)
else
  echo "[GATE] skip: 尚无测试目标（阶段 0 接入 Catch2 后生效）"
fi
ok "ctest"

echo "== 4/5 clang-format =="
if command -v clang-format >/dev/null 2>&1; then
  # 检查暂存 + 未暂存的改动文件并集（避免 git add 后漏检）。
  changed=$( { git diff --cached --name-only --diff-filter=ACM -- '*.h' '*.cpp';
               git diff --name-only --diff-filter=ACM -- '*.h' '*.cpp'; } | sort -u )
  for f in $changed; do
    clang-format --dry-run --Werror "$f" >/dev/null 2>&1 \
      || fail "格式不合规: $f（运行 clang-format -i $f）"
  done
  ok "格式"
else
  echo "[GATE] skip: clang-format 未安装（阶段 0 待装）"
fi

echo "== 5/5 Godot 演示冒烟（可选：未装 godot 或未构建扩展时跳过）=="
GODOT_BIN="$(command -v godot || true)"
DEMO_SO="game_adapter/godot_demo/bin/libnpc_agent_godot_demo.linux.release.x86_64.so"
if [ -n "$GODOT_BIN" ] && [ -f "$DEMO_SO" ]; then
  # 沙箱环境 HOME 可能不可写：XDG 目录重定向到仓库临时目录。
  mkdir -p .tmp-godotdata .tmp-godotconfig
  # 单 NPC 冒烟 + town 多 NPC 连锁 + 80s 多场景压力冒烟（卡死检测）。
  XDG_DATA_HOME="$ROOT/.tmp-godotdata" XDG_CONFIG_HOME="$ROOT/.tmp-godotconfig" \
    "$GODOT_BIN" --headless --path game_adapter/godot_demo \
    --script res://scripts/smoke_test.gd --fixed-fps 60 || fail "Godot 单 NPC 冒烟失败"
  XDG_DATA_HOME="$ROOT/.tmp-godotdata" XDG_CONFIG_HOME="$ROOT/.tmp-godotconfig" \
    "$GODOT_BIN" --headless --path game_adapter/godot_demo \
    --script res://scripts/smoke_town.gd --fixed-fps 60 \
    -- --config res://assets/npcs/sample_town.json || fail "Godot town 连锁冒烟失败"
  XDG_DATA_HOME="$ROOT/.tmp-godotdata" XDG_CONFIG_HOME="$ROOT/.tmp-godotconfig" \
    "$GODOT_BIN" --headless --path game_adapter/godot_demo \
    --script res://scripts/smoke_stress.gd --fixed-fps 60 \
    -- --config res://assets/npcs/sample_town.json || fail "Godot 压力冒烟失败（NPC 卡死检测）"
  ok "Godot 演示冒烟（单 NPC + town 连锁 + 80s 压力）"
else
  echo "[GATE] skip: Godot 演示冒烟（未装 godot 或未构建扩展：cmake --preset godot && cmake --build --preset godot）"
fi

echo "[GATE] ALL PASS"
