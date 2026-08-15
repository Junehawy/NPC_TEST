# 本仓库 AI 编码代理必读（AGENTS）

这是 NPC 智能体框架仓库（C++20）。开始任何编码任务前：

## 必读文档（唯一事实来源）

1. `docs/roadmap-architecture.md` —— 架构定案（条款引用格式 RA-§N）
2. `docs/coding-standards.md` —— 编码规范（CS-§N）
3. `docs/testing-standards.md` —— 测试规范（TS-§N）
4. `docs/review-process.md` —— push 前审查流程与清单

## 红线（违反即失败，无商量）

- **依赖方向**：`npc_agent/` 不 include `game_adapter/` 或任何第三方类型（glm/EnTT/引擎）
- **线程契约**：接口仅驱动线程调用；工作线程只碰值语义 `AgentSnapshot`
- **Blackboard-only**：能力模块不得持有 `IWorld`/`IAgentBody` 指针
- **接口层（RA-§3）变更必须同步 RA 文档 §9 定案表 / §14 变更记录**
- **零警告**：`-Wall -Wextra -Wpedantic -Werror`；禁止提交死代码

## 工作流（强制）

1. 每个阶段性任务完成、push 之前：
   - 跑 `scripts/check-gate.sh`（依赖方向 → 构建 → ctest → 格式）；
   - 按 `docs/review-process.md` §3 清单逐条自查；
   - 写审查报告到 `docs/reviews/<YYYYMMDD>-<阶段>.md`；
   - **审查不通过 = 不 push**。
2. 测试无头运行：不依赖游戏、网络、真实 LLM（用 MockWorld / ScriptedMockProvider）。
3. 性能改动必须有测量依据（CS-§7.8）；热路径零分配（CS-§7.5）。

## 环境

- 构建：`cmake -S . -B build -G Ninja && cmake --build build`（gcc ≥ 13）
- 测试：Catch2 v3 + ctest；格式化：clang-format（.clang-format 随仓）
