# NPC 智能体：测试规范

> 版本 v1.0 | 测试是门禁的一部分：`ctest` 不绿，不得 push（见 `docs/review-process.md`）。

---

## 1. 框架与运行环境

1. **Catch2 v3**（FetchContent 集成，见 RA 阶段 0）+ CTest 驱动。
2. **全部测试无头运行**：不依赖任何游戏、不依赖真实 LLM、不依赖网络——只依赖 `MockWorld` 与 `ScriptedMockProvider`。
3. 测试必须**确定性**：禁止系统时钟、`std::rand`、环境变量、执行顺序依赖（RA-§3.7 确定性契约）。
4. 测试目标：`npc_agent_tests`（单元/集成）+ `adapter_tests`（MockWorld 场景）。

## 2. 目录与命名

```
npc_agent/tests/
├── core/            # agent / agent_system / event_bus / blackboard
├── interfaces/      # 契约类型（Vec3 等）与序列化往返
├── capabilities/    # 每个能力模块一目录
├── scenarios/       # MockWorld 场景测试（对应 RA 各阶段验收）
└── matrix/          # 三级开关矩阵测试
```

命名格式：`TEST_CASE("[模块] 行为_条件 → 期望")`，如
`TEST_CASE("[fsm] 听到枪声_处于巡逻态 → 转移到警戒态")`。
一个 TEST_CASE 只验证**一个行为焦点**；用 SECTION 表达同一前置下的分支。

## 3. 必测内容（覆盖要求）

| 类型 | 内容 | 谁负责 |
|---|---|---|
| 契约类型 | Vec3/TickContext/快照/句柄的构造、拷贝、序列化往返 | interfaces |
| 核心 | Agent/AgentSystem/事件总线（scope 分层）/黑板/仲裁管线（§RA-3.4 四步，含同分次序、ready=false 兜底） | core |
| 能力模块 | 每个模块的 propose/on_event/序列化往返；FSM 状态转移全表 | capabilities |
| 场景 | RA 各阶段验收项：巡逻→枪声→警戒→呼叫支援→搜寻 等 | scenarios |
| 开关矩阵 | 编译期×运行期×单 NPC 组合（RA-§5.2，范围：llm / memory.vector） | matrix |
| 序列化 | 所有 ISerializable 实现 `to_json → from_json` 往返等价；schema_version 迁移函数 | 各模块 |
| trace 回放 | 录制→回放断言意图序列一致（RA 阶段 2 验收） | scenarios |
| 确定性 | 同种子两次运行输出逐字节一致 | core |

## 4. 编写要求

1. **AAA 结构**：Arrange（构造 MockWorld/配置）→ Act（驱动 tick）→ Assert（断言状态/意图序列/事件序列）。
2. 断言**行为不锁实现**：断言"意图序列、黑板终态、事件序列"，不断言内部实现细节。
3. 测试数据（NPC 配置、刺激脚本、对话图）放 `assets/scenarios/` JSON，测试内不硬编码大段数据。
4. 测试互不依赖：每个 TEST_CASE 自建 AgentSystem，禁止共享可变全局状态。
5. 时间必须用 `TickContext` 注入值推进，禁止真实 sleep。
6. 测试代码同样遵守 `docs/coding-standards.md`（注释、命名、无警告）。

## 5. Bug 修复流程（强制）

1. 先写**复现测试**（红灯）；
2. 修复实现（绿灯）；
3. 关联注释注明 bug 编号与复现测试位置。
修复不带复现测试 = 审查不通过。

## 6. 门禁要求

push 前必须全部通过：

1. `ctest` 全绿（release 构建 + 调试断言构建各一次）；
2. **ASan + UBSan** 构建下关键测试（core/scenarios）全绿；
3. 新增公共 API 必须有对应测试（审查时逐条核对）；
4. 测试覆盖率只增不减（阶段 1 起用 llvm-cov 记录基线，趋势红线）。

## 7. 性能测试

1. 热路径（tick/仲裁/序列化）建立**基准**：`tests/bench/`（每次运行记录耗时）；
2. 基准波动超过 ±10% 必须在审查报告中说明原因；
3. 性能优化 PR 必须附优化前后基准对比（CS-§7.8）。
