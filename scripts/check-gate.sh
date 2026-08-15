#!/usr/bin/env bash
# Push 前门禁（docs/review-process.md §2 第一步）：
#   1) 依赖方向检查  2) 构建(-Werror)  3) ctest  4) clang-format
# 任一失败即退出非零 = 不得 push。
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

fail() { echo "[GATE] FAIL: $1"; exit 1; }
ok()   { echo "[GATE] ok: $1"; }

echo "== 1/4 依赖方向检查 =="
if grep -rn --include='*.h' --include='*.cpp' \
     -E '#include[[:space:]]*[<"](game_adapter|glm|entt|SFML|godot|UnrealEngine)' \
     npc_agent/ 2>/dev/null; then
  fail "npc_agent 核心出现越界 include（game_adapter 或第三方类型）——违反 CS-§1.3"
fi
ok "依赖方向"

echo "== 2/4 构建（-Werror 零警告）=="
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_CXX_FLAGS="-Wall -Wextra -Wpedantic -Werror" >/dev/null
cmake --build build >/dev/null
ok "构建"

echo "== 3/4 ctest =="
if [ -f build/CTestTestfile.cmake ]; then
  (cd build && ctest --output-on-failure)
else
  echo "[GATE] skip: 尚无测试目标（阶段 0 接入 Catch2 后生效）"
fi
ok "ctest"

echo "== 4/4 clang-format =="
if command -v clang-format >/dev/null 2>&1; then
  for f in $(git diff --name-only --diff-filter=ACM -- '*.h' '*.cpp'); do
    clang-format --dry-run --Werror "$f" >/dev/null 2>&1 \
      || fail "格式不合规: $f（运行 clang-format -i $f）"
  done
  ok "格式"
else
  echo "[GATE] skip: clang-format 未安装（阶段 0 待装）"
fi

echo "[GATE] ALL PASS"
