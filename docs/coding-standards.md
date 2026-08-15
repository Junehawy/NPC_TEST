# NPC 智能体：代码编写规范

> 版本 v1.0 | 适用于 `npc_agent/`、`game_adapter/`、`tools/` 全部 C++ 代码。
> 本规范是可执行要求：违反者不得进入 push；检查手段见 §10 与 `docs/review-process.md`。

---

## 1. 总则

1. 本仓库代码存在**唯一目的**：支撑 `docs/roadmap-architecture.md` 定义的框架。任何与架构文档冲突的代码都视为规范违规。
2. 规范优先级：**线程契约 > 耦合方向 > 内存/性能 > 注释 > 命名 > 格式**（§10 门禁按此顺序裁决）。
3. 依赖方向铁律（最高级）：`npc_agent/` 核心**绝不 include `game_adapter/` 或任何游戏代码**；接口层（`interfaces/`）只依赖自研 POD 契约类型 + std，**不含任何第三方类型**（glm/EnTT/引擎类型）。此条由门禁脚本自动检查。

## 2. 语言与编译基线

- C++20，CMake ≥ 3.28，gcc ≥ 13（当前 16.1.1）或 clang ≥ 17。
- 全仓编译必须零警告：`-Wall -Wextra -Wpedantic -Werror`。
- 禁用特性：C 风格宏（除非跨平台配置必须）、`using namespace` 于头文件、`reinterpret_cast`（无评审理由禁止）、可变长参数函数。
- 字符串格式化用 `std::format`（gcc≥13 已支持），不引入 fmt 依赖。

## 3. 格式（clang-format 是唯一裁判）

- 仓库根 `.clang-format` 已提供（LLVM 基、4 空格缩进、100 列、C++20）。
- **禁止手工对齐/排版争论**：提交前 `clang-format -i` 处理；格式不合规由门禁拦截。
- 阶段 0 需安装 clang-format 与 clang-tidy（见 `docs/roadmap-architecture.md` 阶段 0 事项）。

## 4. 命名规范

| 对象 | 规则 | 示例 |
|---|---|---|
| 文件 / 目录 | snake_case；头文件 `.h`，源文件 `.cpp` | `agent_system.h`、`behavior_tree.cpp` |
| 命名空间 | `npc_agent` + snake_case 子空间 | `npc_agent::llm` |
| 类型（类/结构体/枚举） | PascalCase；接口加 `I` 前缀 | `WorldSnapshot`、`IWorld` |
| enum class 值 | PascalCase | `OutputMode::JsonSchema` |
| 函数 / 方法 / 局部变量 | snake_case | `register_capability()`、`rng_seed` |
| 成员变量 | snake_case + 尾缀 `_` | `pending_queue_` |
| 编译期常量 | k + PascalCase | `kMaxAgents` |
| 宏（尽量不用） | `NPC_AGENT_` 前缀全大写 | `NPC_AGENT_ENABLE_LLM` |
| 布尔 | `is_/has_/can_` 前缀 | `can_reach()` |
| 句柄/令牌/快照 | `Handle/Token/Snapshot` 后缀 | `ActionHandle`、`AsyncToken` |
| 接口方法语义 | 查询 `const` + 名词；命令动词开头 | `snapshot()`、`move_to()` |

## 5. 注释规范

**总原则：注释必须"可靠"——代码改了注释不改，比没有注释更糟，按违规处理。**

1. **公共 API 必带文档注释**（头文件内，`//` 行式），固定包含：
   - 用途（做什么）；
   - 前置条件（参数合法性、调用线程）；
   - **线程契约标签**（接口层强制）：`【驱动线程】`（仅游戏主线程可调）/ `【工作线程安全】`（仅可接触值语义快照）/ `【无契约】`；
   - 所有权/生命周期（谁负责释放、句柄何时失效）；
   - 复杂度（非平凡函数标注 O(...)）。
2. 实现注释只写**为什么**（算法选择、坑、与架构条款的对应关系），禁止翻译式注释（`i++; // i 自增`）。
3. 魔数必须命名：`constexpr float kHearingRadius = 15.0f;`。
4. `TODO(name, 日期)` 必须三要素齐全：负责人、日期、待办内容；无主的 TODO 禁止提交。
5. 禁止提交注释掉的死代码——需要留历史就用 git。
6. 架构锚点：引用本文档条款时写 `CS-§n`（如 `CS-§7`），引用架构文档写 `RA-§n.m`，方便审查追溯。

## 6. 头文件与耦合规范

1. **include-what-you-use**：用到什么类型就 include 什么；能前置声明就不 include 头文件。
2. 头文件最小依赖：`interfaces/` 头文件禁止包含任何非 std、非自研 POD 类型的头文件。
3. 无环形依赖：模块间依赖只能是**单向**（依赖闭包见 RA-§5.1）；出现环即设计错误，重构成事件解耦。
4. 每个模块对外**只暴露一个公共头**（facade）；实现细节进 `detail/` 或 `.cpp`（pimpl 按需）。
5. 能力模块遵守 Blackboard-only 契约（RA-§3.5）：**不得持有 `IWorld`/`IAgentBody` 指针**。
6. 头文件里禁止 `using namespace std;`；源文件内允许但仅在 .cpp 内。

## 7. 内存与性能规范（每 tick × 每 NPC 是热路径）

1. **RAII，禁止裸 `new/delete`**；所有权用 `std::unique_ptr` 表达，`shared_ptr` 需在注释中说明共享理由。
2. **传参规则**：
   - 小 POD（≤16 字节，如 `Vec3`、句柄）→ 按值；
   - 大对象只读 → `const&`；
   - 只读字符串 → `std::string_view`；
   - 转移所有权 → 按值 + `std::move` 或 `unique_ptr`；
   - 输出参数禁用（用返回值 + 结构化绑定）。
3. **返回**：依赖 NRVO/移动语义，按值返回容器；**禁止**返回 `const&` 指向局部变量。
4. **循环内零隐式拷贝**：`for (const auto& x : range)`（POD 除外）；`push_back` 前 `reserve`；复用 buffer，循环内禁止任何分配。
5. **热路径（Agent tick / 仲裁 / 感知打包）零分配**：用固定大小缓冲、对象池、或预分配容器；热路径禁止 `std::function` 包装（用模板/函数指针），禁止不必要的虚调用。
6. **避免大循环**：
   - 每个非平凡函数注释标注复杂度；
   - O(n²) 及以上必须有数据量硬上界理由（写在注释 + 审查时复核）；
   - 感知/寻路等真正的大规模计算归宿主侧（RA-§3.2），框架内不做。
7. 数据局部性：连续容器（`vector`）优先于链表；避免指针追逐。
8. **性能"优化"必须有依据**：perf 测试数据或复杂度分析，禁止无测量的"我觉得这样快"。
9. 字符串：循环内拼接用预分配/`std::format_to`，禁止 `s = s + x` 式重复分配。

## 8. 并发规范

1. 严格遵守架构线程契约（RA-§2.1/§2.2）：接口仅驱动线程调用；工作线程只接触 `AgentSnapshot`；结果经待处理队列回投，回调在驱动线程派发。
2. 驱动线程上**禁止任何阻塞等待**（等待 LLM/IO 一律异步）。
3. 无锁数据结构仅在提供正确性依据（测试 + 压力）时允许；默认用互斥/队列。
4. 回调/事件处理器**不得再入**驱动线程的排空循环（禁止在回调里同步调用 tick）。

## 9. 错误处理

1. 接口层**不跨边界抛异常**：错误经返回值、`ActionHandle` 完成/失败事件表达。
2. 框架内部：编程错误（契约违反）用 `assert`；运行期可恢复错误用返回值（`std::optional`）或缺省降级。
3. 配置错误 **fail-fast**：启动即报错并给出**可定位**的信息（文件、键路径、期望值），绝不静默忽略（RA-§5.2 超集约束）。
4. LLM/网络失败：降级规则决策 + 决策日志记录（RA-§6）。

## 10. 工具链与门禁

- `.clang-format`：格式唯一裁判（随仓）。
- `.clang-tidy`：关键检查集（`modernize-*`、`performance-*`、`readability-*` 精选子集），CI 与本地均可跑。
- `scripts/check-gate.sh`：push 前门禁，顺序执行：
  1. **依赖方向检查**（npc_agent 不得 include game_adapter；interfaces 不得 include 第三方）；
  2. 构建（`-Werror` 零警告）；
  3. `ctest` 全绿；
  4. clang-format 差异检查（已安装时）。
- 任一失败 = 不得 push；修复后重跑。
- 具体审查流程与清单见 `docs/review-process.md`。

## 11. 可移植性

1. 核心代码不出现平台 API（Win32/POSIX/引擎 API）；需要时经宿主接口或集中于 `config/`。
2. 条件编译集中于 CMake 选项与 `config/` 单点，禁止散落的 `#ifdef`。
3. 接口层"全值语义、无不透明指针、无回调持有宿主对象"（RA-§13 #6），保持未来进程化可能。
