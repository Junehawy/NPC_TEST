# NPC_TEST

游戏 NPC 智能体框架：**游戏无关、插件化、能力可开关**的 C++20 NPC AI 核心库。

## 文档

| 文档 | 内容 |
|---|---|
| [docs/roadmap-architecture.md](docs/roadmap-architecture.md) | 技术路线与架构设计（定案与变更记录，条款引用 RA-§N） |
| [docs/coding-standards.md](docs/coding-standards.md) | 代码编写规范（CS-§N） |
| [docs/testing-standards.md](docs/testing-standards.md) | 测试规范（TS-§N） |
| [docs/review-process.md](docs/review-process.md) | Push 前代码审查流程与清单 |
| [docs/reviews/](docs/reviews/) | 每次 push 前的审查报告存档 |
| [AGENTS.md](AGENTS.md) | AI 编码代理必读（红线 + 强制工作流） |

## 强制工作流

每个阶段性任务完成、push 之前：

```bash
scripts/check-gate.sh   # 依赖方向 → 构建(-Werror) → ctest → clang-format
```

然后按 `docs/review-process.md` §3 清单自查并写审查报告；**审查不通过 = 不 push**。

## 主题

- NPC 对话系统（对话图 + LLM 驱动，异步化）
- 行为模式（FSM / 行为树 / Utility AI，可切换）
- AI 与路径查找（A* 宿主侧 + 感知系统）
- 角色扮演设计（角色卡 + 记忆系统）
- 任务 NPC 设计（派发 / 交付钩子 + 条件求值）

## 技术栈

C++20 + CMake + Ninja | nlohmann/json | Catch2 | 自研 POD 接口契约 | LLM Provider 抽象（Mock → 云端 API → llama.cpp）
