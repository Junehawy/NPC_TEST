# NPC 智能体：技术路线与架构设计（草案 v0.3）

> 目标：构建一个**游戏无关、插件化、能力可开关**的 NPC 智能体框架，可作为独立库移植到后续任何游戏/引擎中。
> 状态：**接口冻结评审就绪**。v0.3 已并入第三轮评审修订（§14 对照表）、第四轮冻结前修订（§14 R4 小节）与终检修订（§14 R5 小节），不再出独立版本。版本在文档内维护，文件名保持单一来源。

---

## 1. 项目愿景

- **插件化**：智能体核心（框架）与游戏宿主完全解耦，通过接口层通信；换引擎 = 重写薄适配层，核心零改动。
- **全面**：感知、决策、寻路、对话、社交、记忆、任务、LLM 能力齐全，且**每个能力可独立开关**（编译期 / 运行期 / 单 NPC 三级开关）。
- **智能化**：规则决策（快、稳、可调试）与 LLM 决策（深、灵活、拟人）**混合使用**，LLM 异步化不阻塞游戏循环。
- **可测试**：无头模拟世界（`MockWorld`）支撑集成测试与场景回归；决策日志可录制→回放做回归断言（确定性契约见 §3.7）。

---

## 2. 总体架构（依赖倒置）

**核心原则：依赖方向永远从框架指向接口。** 智能体核心绝不 include 游戏代码；游戏侧实现接口。

```
┌─────────────────────────────────────────────────────────┐
│                       游戏宿主 (Game Host)                │
│  引擎循环 / 场景 / 渲染 / 导航网格 / 动画 / 音频           │
│  ┌───────────────────────────────────────────────────┐  │
│  │              游戏适配层 (Game Adapter)              │  │
│  │   实现 IWorld（每场景一个共享门面）                   │  │
│  │   + IAgentBody（每 NPC 一个）                       │  │
│  └───────────────────────────────────────────────────┘  │
└──────────────────────────┬──────────────────────────────┘
                           │ 只依赖 POD 契约类型 + 纯虚接口
┌──────────────────────────▼──────────────────────────────┐
│                  NPC 智能体核心 (npc_agent)               │
│  ┌───────────────────────────────────────────────────┐  │
│  │  AgentSystem（全局：tick 驱动、IWorld 引用注入、     │  │
│  │  事件总线、AgentSnapshot 组装、跨 agent 状态：       │  │
│  │  SocialGraph / 记忆索引 / 任务状态）                 │  │
│  │  Agent（每 NPC：能力模块(ICapability) + 私有事件 +   │  │
│  │  Blackboard + 仲裁器）                              │  │
│  │  interfaces IWorld │ IAgentBody │ ICapability │ 契约 │  │
│  └───────────────────────────────────────────────────┘  │
│  依赖：std + nlohmann/json（无第三方数学库）              │
└─────────────────────────────────────────────────────────┘
```

### 2.1 接口层（框架与游戏的分界线）

- 接口层只出现**框架自己定义的 POD 契约类型**（`Vec3`、`TickContext`、`AgentSnapshot` 等，见 §3）。**不出现任何第三方类型**（glm、EnTT、引擎向量）——适配层实现接口时不需要依赖框架的第三方库版本。
- 三个接口角色：`IWorld`（环境，**每场景一个共享门面**）、`IAgentBody`（自身状态与动作，每 NPC 一个）、`ICapability`（框架内能力模块的统一产出接口）。领域动作（交任务、给物品）不进入接口，走通用 JSON 通道。
- **线程契约（最重要的一条）**：`IWorld` / `IAgentBody` **仅允许驱动线程（游戏主线程）调用**。工作线程（LLM 推理、IO）只接触**值语义的 `AgentSnapshot`**。该快照同时复用于 PromptBuilder / 决策日志 / 存档。
- **IWorld 归属**：`IWorld` 不绑定任何 Agent 身份，所有查询显式携带参数；**Agent 与能力模块均不持有 `IWorld` 指针**，由 `AgentSystem` 持有当前场景引用并在每次 tick 使用（顺序换图/传送时随场景更新）。**并发多场景/分片为 v1 显式排除范围**（§13 #11）。

### 2.2 Runtime 粒度

- **每 NPC 一个轻量 `Agent` 对象**：持有自己的能力模块（均实现 `ICapability`）、自己的 `IAgentBody` 引用（执行层使用）、私有事件队列、自己的 `Blackboard`、仲裁器。
- **全局一个 `AgentSystem`**：统一驱动所有 Agent 的 tick、持有当前场景 `IWorld` 引用（**框架内唯一持有者**，感知/导航查询由它代执行并把结果注入事件/黑板）、跨 agent 状态（SocialGraph、长期记忆索引、任务状态）、管理工作线程池与结果回投队列、组装 `AgentSnapshot`。
- **事件总线按 scope 分层**：
  - *全局事件*（广播）：枪声、天气、任务广播、玩家行为——所有 Agent 可订阅；
  - *Agent 私有事件*（点对点）：自己的感知结果、动作完成/失败、LLM 回复。
- **线程安全契约**：事件总线与 Blackboard **非线程安全**，只在驱动线程访问；工作线程唯一入口是 `AgentSystem` 持有的**线程安全待处理队列**（worker 只入队，驱动线程 tick 时统一排空消费）。

### 2.3 大脑 / 身体分离

决策层只产生**意图（Intent）**，不直接执行动作（契约见 §3.4）。寻路、动画、语音等全部是"执行细节"，可替换实现而不动大脑。

---

## 3. 核心契约（接口层 v0.3 头文件草案）

### 3.1 types.h —— POD 契约类型

```cpp
// 纯数据，无运算。运算逻辑留在框架内部（内部可用任意数学库）。
struct Vec3 { float x = 0, y = 0, z = 0; };

struct TickContext {
    float    dt;           // 本次 tick 的模拟步长（秒）
    double   game_time;    // 全局游戏时间（秒），暂停时不推进；同一运行内单调递增，读档恢复存档值
    uint64_t tick_index;   // 单调递增 tick 序号
    uint64_t rng_seed;     // 每 tick 确定性派生，供决策随机（见 §3.7 确定性契约）
};

// 环境快照（值语义、可序列化）——只含环境，不含 self。
struct WorldSnapshot {
    double  game_time;
    std::vector<MemoryEvent> recent_events;
    // 其余环境只读字段（天气、可见实体列表等）
};

// 自身状态快照 —— 由 IAgentBody 提供。
struct BodyState {
    Vec3        position;
    std::string faction;
    float       stamina;
    // 其余自身只读字段
};

// 完整 Agent 快照 —— AgentSystem 在驱动线程组装（WorldSnapshot + BodyState），
// 供 PromptBuilder / 决策日志 / 存档复用；工作线程只接触它，绝不接触接口对象。
struct AgentSnapshot {
    WorldSnapshot world;
    BodyState     self;
};

// 动作生命周期句柄
struct ActionHandle {
    uint64_t id = 0;
    explicit operator bool() const { return id != 0; }
};

// 轻量异步原语（替代 std::future）
// submit() 返回 AsyncToken；结果回调经事件总线入队，在驱动线程 tick 时统一派发
// —— 回调内可直接读写 Blackboard，无竞争。不提供阻塞 get。
struct AsyncToken { uint64_t id = 0; };
```

> 注：文中引用的其余契约类型（`GameEvent` / `Message` / `DialogueLine` / `Stimulus` / `PerceptionQuery` / `PerceptionResult` / `Blackboard` / `AgentEvent`）均在同一接口层定义，v1 一律保持 POD/JSON 值语义，不含引擎或第三方类型。

### 3.2 i_world.h —— 环境（每场景一个共享门面，不绑定 Agent 身份）

```cpp
struct IWorld {
    virtual ~IWorld() = default;

    // ---- 时间 ----
    virtual TickContext tick_context() const = 0;

    // ---- 感知：状态性信息走查询；瞬时刺激由宿主推送 ----
    // 查询必须携带感知参数；空间分区、过滤是宿主职责（性能契约）。
    virtual PerceptionResult sense(const PerceptionQuery& q) const = 0;

    // 宿主推送瞬时刺激（枪声、碰撞、玩家发起对话…），
    // 由 AgentSystem 转为全局事件广播。
    virtual void inject_stimulus(const Stimulus& s) = 0;

    // ---- 导航查询（寻路实现属于宿主侧；查询显式传参，无 "self"） ----
    virtual bool can_reach(Vec3 from, Vec3 to) const = 0;
    virtual std::vector<Vec3> find_path(Vec3 from, Vec3 to) const = 0;

    // ---- 环境快照（不含 self；self 见 IAgentBody::body_state） ----
    virtual WorldSnapshot snapshot() const = 0;
};
```

> 接口稳定性策略：`IWorld` 是**门面**，内部按角色聚合（`IPerceptionSource` / `INavigationQuery` / `IWorldClock`）。适配层实现一个门面类即可；未来新增感知类型走 `PerceptionQuery` 数据驱动，而不是加虚函数。
> **性能契约澄清**："仅驱动线程调用"是**线程安全契约，不是性能许可**。重计算的 `IWorld` 实现（尤其 `find_path`、大规模 `sense`）建议宿主侧自行异步化/时间片——同步阻塞主线程的宿主实现同样违反红线一的精神（框架不限定实现策略，只明确这不是框架的承诺范围）。

### 3.3 i_agent_body.h —— 智能体的"身体"（每 NPC 一个）

```cpp
struct IAgentBody {
    virtual ~IAgentBody() = default;

    // ---- 只读：自身状态（供 AgentSystem 组装 AgentSnapshot） ----
    virtual BodyState body_state() const = 0;

    // ---- 动作：返回句柄；完成/失败/取消经事件总线回投 ----
    // 移动是持续动作，执行层必须能收到"到达 / 被挡住 / 被打断"。
    virtual ActionHandle move_to(Vec3 target, float speed) = 0;
    virtual ActionHandle play_emote(const std::string& name) = 0;
    virtual ActionHandle say(const DialogueLine& line) = 0;

    // ---- 通用语义通道（领域动作：交任务/给物品/开锁…） ----
    // 返回句柄，生命周期与其余动作对称：宿主可回报完成/失败事件；
    // 若宿主不回报，框架按"立即成功"推进状态——
    // 任务进度的真实依据是条件求值（task 模块轮询），而非派发结果本身。
    virtual ActionHandle dispatch_game_event(const GameEvent& e) = 0;
};
```

### 3.4 intent.h —— 决策输出 + 仲裁管线（v0.3 补全产出方接口）

```cpp
struct MoveIntent       { Vec3 target; float speed; };
struct SayIntent        { std::string text; std::string tone; };
struct EmoteIntent      { std::string name; };
struct GameEventIntent  { GameEvent event; };
using IntentPayload = std::variant<MoveIntent, SayIntent, EmoteIntent, GameEventIntent>;

struct Intent {
    IntentPayload payload;
    float  priority = 0.0f;
    bool   ready = true;                 // false = 决策未完成，正在等异步结果（LLM）
    std::optional<AsyncToken> async_token; // ready=false 时的关联令牌
};
```

**仲裁管线（v1 定案，产出方接口见 §3.5）**：

1. Agent 每 tick 遍历所有**启用的能力模块**，调用 `ICapability::propose()` 收集候选 Intent；
2. `IDecisionMaker` 本身实现 `ICapability`，是**权威意图源**：其 `ready` 意图**直接胜出**（不参与仲裁）；`ready=false` 时跳过，执行层用兜底行为顶替，直到异步结果回投；
3. 其余候选按 `priority` 降序仲裁，**同分按注册次序（Agent 注册时分配，见 §3.5）**；
4. 仲裁结果 → 执行层（经 `IAgentBody` 执行，动作完成/失败经事件回投形成闭环）。

### 3.5 icapability.h + i_decision_maker.h —— 能力模块统一产出接口（v0.3 新增，含冻结前修订 R4）

```cpp
// 所有能产出 Intent 的能力模块（decision / dialogue / task / llm 等）都实现它。
struct ICapability {
    virtual ~ICapability() = default;
    virtual std::string_view id() const = 0;              // 注册名（日志/诊断）
    virtual std::optional<Intent> propose(const Blackboard& bb, const TickContext& tc) = 0;
    virtual void on_event(const AgentEvent& e) {}         // 可选（FSM 等需要事件驱动）
    virtual void to_json(json&) const = 0;                // 可序列化（契约见 §7.3）
    virtual void from_json(const json&) = 0;
    // 仲裁次序不由模块自报（避免撞值）：由 Agent::register_capability()
    // 按注册顺序分配并内部记录（见下"注册与仲裁次序"）。
};

// 决策器：IDecisionMaker 是 ICapability 的纯标记子接口（冻结前修订 R4：
// 删除 tick()，消除与 propose() 的双轨歧义）。其 ready 意图在仲裁中拥有
// 权威地位（§3.4 管线第 2 条），该地位由 Agent 在注册/配置时指定
// （config: decision: "fsm" | "behavior_tree" | "utility_ai"）。
// 只统一输入输出契约，不强制内部机制；单元测试直接调用 propose()。
struct IDecisionMaker : ICapability {};
```

**注册与仲裁次序（冻结前修订 R4）**：仲裁次序由 `Agent::register_capability()` 按注册顺序分配并内部记录，`ICapability` 不声明该值——从根上消除模块自报撞值（同分仲裁仍按该次序，见 §3.4 管线第 3 条）。

**Blackboard-only 契约（线程红线的显式化，硬性规则）**：`IWorld` / `IAgentBody` 仅由 `AgentSystem` / `Agent` 各持有一份（§2.2），**能力模块不得缓存或持有其指针**（构造参数、成员变量、静态存储均禁止）。感知与导航查询由 `AgentSystem` 在驱动线程按配置代执行（`sense` / `find_path`），结果以事件/黑板条目注入；能力模块的 `propose()` 只从 `Blackboard` + `TickContext` 读取，模块私有状态自行维护并经 `to_json/from_json` 序列化。直连接口会绕过线程安全契约且编译期不可见——视为违反红线。

**BT 序列化策略（v1 定案）**：读档后行为树**重置到根节点重新评估**（多数游戏语义上可接受）；FSM 存状态枚举即可；BT 节点级执行状态外置到黑板（供 v2 精细化恢复），接口设计时即预留。

### 3.6 i_llm_provider.h —— LLM 抽象（v0.3 补 cancel）

```cpp
enum class OutputMode { Text, JsonSchema };   // 对话=纯文本；决策=结构化

struct LLMRequest {
    std::string system_prompt;   // 角色卡渲染结果（PromptBuilder 产出）
    std::vector<Message> history;
    OutputMode mode;
    json schema;                 // mode==JsonSchema 时提供（function calling）
};
struct LLMResponse {
    bool ok;
    std::string text;            // Text 模式
    json json;                   // JsonSchema 模式
};

struct ILLMProvider {
    virtual ~ILLMProvider() = default;
    virtual AsyncToken submit(LLMRequest req, std::function<void(LLMResponse)> cb) = 0;
    // 取消语义分层（v0.3 定案）：
    //  - "取消回调"：迟到结果不污染 Blackboard —— 由 AgentSystem 令牌作废负责；
    //  - "取消请求"：尝试中断底层 HTTP/推理本身（省钱/省资源）—— 本方法，
    //    尽力而为，默认空操作；是否真正中断取决于 Provider 实现。
    virtual void cancel(AsyncToken token) {}
};
```

### 3.7 契约补充条款（v0.3 新增）

- **确定性契约（含 RNG 归属，冻结前修订 R4）**：框架内所有随机性必须来自 **Agent 统一持有的可播种根 RNG**；根状态**随存档序列化**、每 tick 确定性推进并派生 `TickContext::rng_seed`，各能力模块由 `rng_seed + 模块 id` 确定性派生子序列——**模块不单独持久化自己的随机源**；**禁止 `std::rand()` / 系统时钟参与决策**。否则决策日志录制→回放回归（§2、阶段 2 验收）会失真。
- **线程契约**：见 §2.1/§2.2——接口仅驱动线程调用；工作线程只碰 `AgentSnapshot`；回调只入队、主线程 tick 统一消费。
- **性能契约澄清**：见 §3.2——"仅驱动线程调用"不等于"宿主实现可以重同步调用"。
- **序列化契约**：所有实现 `ICapability` 的模块必须实现 `to_json/from_json`（阶段 1 只定接口契约，具体 schema 随模块交付）。

---

## 4. 事件 / 黑板 / 时间 定界

| 机制 | 定义 | 参与序列化 |
|---|---|---|
| **事件（EventBus）** | 瞬时通知，fire-and-forget，可回溯到决策日志 | 否 |
| **黑板（Blackboard）** | 持续可读的状态，跨 tick 存活 | 是 |
| **时间（TickContext）** | 每个 tick 函数的入参：`dt / game_time / tick_index / rng_seed`；LLM 节流、超时判定都基于 `game_time` | — |

一句话：**事件 = 发生的事；黑板 = 现在的状态。**

---

## 5. 能力模块与开关体系

### 5.1 能力清单（v0.3 修正依赖）

| 模块 | 职责 | 依赖 | 默认 |
|---|---|---|---|
| `perception` | 订阅全局刺激 + 消费 AgentSystem 注入的感知查询结果，过滤打包为感知事件 | IWorld（经 AgentSystem 代查询） | 开 |
| `decision`（**v1 互斥单选**：fsm \| behavior_tree \| utility_ai） | 行为决策（权威意图源） | core | fsm |
| `memory.short_term` | 环形缓冲（最近 N 条 MemoryEvent） | core | 开 |
| `memory.long_term` | 事件摘要 + 按对象索引 | memory.short_term | 关 |
| `memory.vector` | 语义检索（RAG），**v2 再做** | 外部库 | 关 |
| `social` | 好感度规则、态度状态机 | memory.short_term（读事件）+ SocialGraph | 开 |
| `dialogue` | 对话图 + DialogueSession 状态机 | social、memory | 开 |
| `llm` | Provider + PromptBuilder + LLMGateway | 无（决策器可选调用） | 关 |
| `task` | 任务派发/交付钩子 | **条件求值接口**（不直接依赖 dialogue/social 模块） | 开 |
| `pathfinding` | 路径查询封装（消费 AgentSystem 代执行的导航查询结果） | IWorld（经 AgentSystem 代查询） | 开 |

**依赖闭包规则**：开关不是完全正交的——打开一个模块会**自动拉入其依赖闭包**（如 `dialogue` → `social` + `memory`）。但 `task` 只依赖抽象的"条件求值接口"，因此"只要任务、不要社交好感度"的游戏可以成立。

### 5.2 三级开关与组合语义（v0.3 收窄编译期范围）

1. **编译期**：**只对"重依赖 / 可选第三方库"的能力开放**——`llm`、`memory.vector`、（可选）导航库。其余能力（decision、perception、social、dialogue、task、memory.short_term/long_term）**只做运行期开关**，避免编译期组合爆炸。
2. **运行期全局**：JSON 配置。
3. **单 NPC 级**：JSON 配置覆盖。

**组合规则**：编译期开关是运行期开关的**超集约束**——编译期关闭的能力，若运行期配置中出现 `enabled: true`，**启动即报错（fail-fast，带明确错误信息）**，绝不静默忽略。**开关矩阵测试只覆盖编译期开关能力集合**（llm / memory.vector），组合数受控，CI 时间可控。

---

## 6. LLM 子系统

```
LLMGateway（队列 + 优先级 + 每NPC冷却 + 全局令牌预算 + 失败降级）
   │ submit(LLMRequest)
   ▼
ILLMProvider（ScriptedMock | OpenAICompat | LocalLlamaCpp）
   │ 回调 → 线程安全待处理队列 → 驱动线程 tick 统一派发
   │ cancel(AsyncToken) —— 尽力中断底层请求（默认空操作）
   ▼
PromptBuilder（角色卡渲染 + AgentSnapshot 注入 + 记忆检索拼装）——独立模块，可单测
```

- **输出契约**：对话场景 → `OutputMode::Text`（自由文本）；**决策场景 → `OutputMode::JsonSchema`（function calling）**，LLM 结构化输出直接映射为 Intent，不可解析时降级规则决策。
- **LLMGateway 放阶段 5**：50+ NPC 调云端 API 是**成本问题**（令牌预算）与**正确性问题**（超时、降级、每 NPC 冷却），不是优化问题。失败时自动降级到规则决策。
- **异步铁律**：LLM 调用永不阻塞游戏循环；等待期间决策器产出 `ready=false` 的 Intent，执行层用兜底行为顶替。

---

## 7. 记忆、社交与序列化

### 7.1 MemoryEvent schema（阶段 1 即冻结）

```json
{
  "subject": "npc_guard_3",      // 谁
  "object": "player",            // 对谁/什么事
  "type": "heard_gunshot",       // 事件类型（枚举，注册表管理）
  "timestamp": 12345.6,          // game_time
  "importance": 0.8,             // 0~1，供摘要/检索排序
  "payload": { }                 // 类型相关的附加字段
}
```

带 `subject/object` 使记忆可**按主体/对象分区索引**——`social` 模块据此查"与玩家相关的好感度事件"，`dialogue` 据此引用"上次见面的事"。

### 7.2 SocialGraph 是世界状态，不属于单个 NPC（定案）

- 关系是有向边：`A→B` 与 `B→A` 可不对称。
- 数据归 **AgentSystem 侧的 SocialGraph 模块**（或宿主侧），**不随单个 NPC 卸载/死亡而丢失**。
- 随世界存档整体序列化。
- **待确认的只是存储形态**（内存索引结构 / 是否分区 / 序列化格式），不是归属——归属已定案。

### 7.3 序列化契约时机

- **阶段 1 只定义接口契约**：`ISerializable { to_json / from_json }` + 每个模块自报 `schema_version` 并注册迁移函数。（`ICapability` 已内嵌 `to_json/from_json`；根 RNG、黑板、SocialGraph 等非能力状态同样实现 `ISerializable`。）
- **具体字段设计随各能力模块交付时补充**，不提前设计（避免阶段 1 定的 schema 到阶段 4/5 推翻）。
- 模块独立版本号 + 迁移函数，比全局单一版本号更能扛模块独立演进。
- 存档单元 = `AgentSnapshot` + 黑板 + **Agent 根 RNG 状态** + 各能力模块状态（均经 `ICapability::to_json`）。

---

## 8. 技术栈

```
核心：C++20 + CMake + Ninja
数据：nlohmann/json（配置 / 角色卡 / 存档）
测试：Catch2（单元 + 场景集成 + 开关矩阵 + trace 回放回归）
接口契约：自研 POD（Vec3 / TickContext / AgentSnapshot …）——无第三方数学库
决策：自研 FSM / BT / Utility（同一 IDecisionMaker 接口，v1 互斥单选）
LLM ：ILLMProvider：ScriptedMock → OpenAICompat（cpp-httplib）→ LocalLlamaCpp（最后）
寻路：自研 A*（宿主侧）/ 进阶 recastnavigation + RVO2（宿主侧）
向量：faiss / HNSW（v2，可选）
ECS ：~~EnTT~~ 移除（见 §8.1 论证）
```

### 8.1 EnTT 为什么移除

- 本设计中 NPC = `Agent` 对象 + 成员能力模块，是**异构**个体（每 NPC 不同开关、不同决策器），ECS 的收益在于**同构批量**处理——这里用不上。
- 数量级预期 10~100，无性能压力；引入 EnTT 只增加学习与耦合成本。
- **重新引入条件**：出现人群模拟需求（同构、数百+、需要 cache 友好的批量更新）时，把"世界实体"放进 ECS，**Agent 仍是普通对象**，接口层不受影响（这就是接口不暴露 EnTT 的回报）。

### 8.2 目录结构

```
NPC_TEST/
├── CMakeLists.txt
├── docs/
│   └── roadmap-architecture.md
├── npc_agent/                 # 智能体框架（游戏无关，纯库）
│   ├── include/npc_agent/
│   │   ├── core/              # agent / agent_system / event / blackboard
│   │   ├── capabilities/      # 各能力模块
│   │   ├── interfaces/        # i_world / i_agent_body / icapability / intent / ...
│   │   ├── llm/               # provider / prompt_builder / gateway
│   │   ├── testing/           # MockWorld/MockBody/玩具能力（无头测试与示例共用，R6-1）
│   │   └── config/            # 配置结构 + 三级开关解析 + 矩阵测试数据
│   ├── src/
│   └── tests/
├── game_adapter/
│   └── sample_adapter/        # 最小宿主示例（无头演示）
├── tools/
│   ├── trace_replay/          # 决策日志录制→回放回归
│   └── npc_editor/            # （远期）NPC 配置编辑器
└── assets/
    ├── npcs/                  # 角色卡 / NPC 配置 JSON
    ├── dialogues/             # 对话图 JSON
    └── scenarios/             # 测试场景定义 JSON
```

---

## 9. 关键技术决策汇总

| # | 决策 | 状态 |
|---|---|---|
| 1 | 依赖方向永远从框架指向接口 | v0.1 定案 |
| 2 | 接口层只出现自研 POD 契约类型，无第三方类型 | v0.2 定案 |
| 3 | 两个外部接口：`IWorld`（每场景共享门面）+ `IAgentBody`（每 NPC） | **v0.3 定案** |
| 4 | Intent = variant 负载 + 仲裁管线 + `ready/async_token` 分离 | **v0.3 定案** |
| 5 | 动作有生命周期：ActionHandle + 完成/失败事件回投 | v0.2 定案 |
| 6 | 领域动作走 `dispatch_game_event`，返回 ActionHandle；宿主不回报按立即成功，任务进度以条件求值为准 | **v0.3 定案** |
| 7 | 接口仅驱动线程调用；工作线程只碰 AgentSnapshot；回调入队、主线程消费 | v0.2 定案（最重要） |
| 8 | per-agent Agent + 全局 AgentSystem；事件总线按 scope 分层 | v0.2 定案 |
| 9 | 时间模型 TickContext 作为所有 tick 入参（含 rng_seed） | **v0.3 定案** |
| 10 | 感知：状态走查询、刺激走推送；感知过滤是宿主职责 | v0.2 定案 |
| 11 | 事件=瞬时通知（不序列化）；黑板=持续状态（序列化） | v0.2 定案 |
| 12 | LLM 异步用自研 AsyncToken+回调，不用 std::future | v0.2 定案 |
| 13 | LLM 输出分级：对话=文本，决策=JSON schema | v0.2 定案 |
| 14 | LLMGateway（限流/预算/降级）放阶段 5 | v0.2 定案 |
| 15 | SocialGraph 是世界状态，不归单个 NPC（归属定案；存储形态待定） | **v0.3 定案** |
| 16 | 编译期开关是运行期超集约束，冲突即报错 | v0.2 定案 |
| 17 | 开关存在依赖闭包，task 只依赖条件求值接口 | v0.2 定案 |
| 18 | decision v1 互斥单选，组合留 v2 | v0.2 定案 |
| 19 | BT 读档重置到根节点重评（v1） | v0.2 定案 |
| 20 | 阶段 1 只定 ISerializable 接口契约，schema 随模块交付 | v0.2 修正 |
| 21 | 移除 EnTT，写明重新引入条件 | v0.2 定案 |
| 22 | Decision Trace = JSON lines，录制→回放做回归测试 | v0.2 定案 |
| 23 | 能力模块统一 `ICapability` 接口产出候选 Intent + 仲裁管线四步定案 | **v0.3 定案** |
| 24 | 编译期开关收窄：仅 llm / memory.vector / 可选导航库；矩阵测试范围受控 | **v0.3 定案** |
| 25 | 确定性契约：随机性必须经可播种 RNG（TickContext::rng_seed），禁系统随机源 | **v0.3 定案** |
| 26 | LLM 取消语义分层：取消回调（AgentSystem） vs 取消请求（Provider::cancel） | **v0.3 定案** |
| 27 | 性能契约澄清："仅驱动线程调用"≠"宿主实现可重同步"，重计算宿主自行异步化 | **v0.3 定案** |
| 28 | IDecisionMaker 删除 tick()，降为纯标记子接口；权威地位由注册/配置指定 | **v0.3 冻结前修订** |
| 29 | Blackboard-only 契约：能力模块不得持有 IWorld/IAgentBody 指针 | **v0.3 冻结前修订** |
| 30 | registration_order 由 Agent 注册时分配，模块不自报 | **v0.3 冻结前修订** |
| 31 | RNG 归属 Agent 统一持有并随存档序列化，模块派生子序列 | **v0.3 冻结前修订** |
| 32 | 单场景假设显式化：多场景/分片为 v1 排除范围（开放问题 #11） | **v0.3 冻结前修订** |
| 33 | IWorld 由 AgentSystem 唯一持有，感知/导航查询代执行并注入；模块 propose() 只读 Blackboard | **v0.3 终检修订** |
| 34 | IAgentBody 由 Agent 持有（执行层），模块不得持有 | **v0.3 终检修订** |

---

## 10. 分阶段路线图（v0.3 修订）

### 阶段 0：工程底座（1 周）✅ 完成
- [x] C++20 + CMake + Ninja（已有）+ Catch2 v3.8.1 + nlohmann/json v3.11.3（FetchContent）
- [x] **开关矩阵测试骨架**（覆盖编译期开关能力集合：llm / memory.vector；四二进制 × 2×2 组合全覆盖）
- **验收**：`ctest` 通过，含至少 1 个开关组合测试。
- **验收记录（2025-08-15）**：Release（-Werror 零警告）+ Debug/ASan+UBSan（clang-22，环境无 gcc libasan）双构建 ctest 4/4 全绿，25 用例 / 69 断言；门禁 `scripts/check-gate.sh` 全绿；审查报告 `docs/reviews/20250815-阶段0.md`。

### 阶段 1：核心架构（2~3 周）✅ 完成
- [x] **接口层 v0.3 冻结评审**（§3 契约落为真实头文件：types / i_world / i_agent_body / intent / icapability / i_llm_provider）
- [x] Agent + AgentSystem（IWorld 引用注入）+ 事件总线（scope 分层）+ Blackboard + TickContext
- [x] **ICapability + 仲裁管线**（§3.4/§3.5 四步 + CapabilityFactory 存档恢复）
- [x] JSON 配置加载 + 三级开关 + 超集约束报错（阶段 0 完成，per-NPC 配置本阶段补齐）
- [x] MockWorld/MockBody（`npc_agent/testing/`，R6-1）+ 序列化接口 `ISerializable` + MemoryEvent schema + AgentSnapshot 组装
- [ ] 线程安全待处理队列（worker→driver）——顺延至阶段 5（此前无工作线程，R6-2）
- **验收**：无头程序中，一个 NPC 读配置、每 tick 收事件、经仲裁产出意图给 MockWorld；读档后黑板+能力模块状态可恢复。
- **验收记录（2025-08-16）**：Release（-Werror 零警告）+ Debug/ASan+UBSan（gcc，libasan 已装）双构建 ctest 5/5 全绿，36 用例 / 171 断言（新增 22 用例覆盖仲裁四步、事件路由、感知注入、读档恢复、确定性）；冒烟示例 `npc_test` 演示"巡逻 → 枪声 → alarm 兜底问候"全链路；门禁含 clang-format 检查全绿；审查报告 `docs/reviews/20250816-阶段1.md`。

### 阶段 2：行为系统（3~4 周）
- [ ] FSM 实现（闲置/巡逻/警戒/对话/战斗）+ BT / Utility AI（同接口，v1 互斥）
- [ ] 感知模块（订阅刺激 + 消费 AgentSystem 注入的查询结果并打包）+ 动作生命周期回投
- [ ] 决策日志 v1 + **trace 录制/回放回归工具**（确定性 RNG 契约生效）
- **验收**：MockWorld 场景"听到枪声 → 警戒 → 呼叫支援 → 搜寻"跑通；trace 回放断言序列一致。

### 阶段 3：寻路与感知增强（2~3 周）
- [ ] A* 实现（宿主侧示例）+ `can_reach` / `find_path` 消费
- [ ] 视野/听觉判定封装（宿主侧实现细节）；评估宿主侧异步化策略（§3.2 性能契约）
- [ ] （可选）recastnavigation 接入评估
- **验收**：NPC 沿 A* 路径移动，途中障碍触发重新规划；动作完成事件正确推进 FSM。

### 阶段 4：对话与社交（3~4 周）
- [ ] 对话图（节点 + 条件分支 + 变量求值）
- [ ] **DialogueSession 状态机**：发起 / 轮次（NPC↔玩家）/ 打断 / 超时 / 走远终止 / 读档恢复（v1：中断恢复到最后稳定节点，否则重置）
- [ ] SocialGraph 模块（归属定案；存储形态在此阶段确认）+ 好感度规则（A→B ≠ B→A）
- **验收**：脚本化对话流程 + 好感度影响分支 + 对话中玩家走远自动终止的测试通过。

### 阶段 5：LLM 子系统（4~6 周）
- [ ] AsyncToken 异步机制 + ScriptedMockProvider
- [ ] **PromptBuilder**（角色卡 + AgentSnapshot 注入 + 记忆检索拼装，独立单测）
- [ ] **LLMGateway**（队列 + 优先级 + 每NPC冷却 + 全局令牌预算 + 失败降级规则决策）
- [ ] Provider::cancel 评估（HTTP 层能否真正断开 / 本地推理能否中断，见开放问题 §13）
- [ ] 记忆 v1：短期环形缓冲 + 事件摘要 + 按对象索引
- [ ] 决策场景 JSON schema（function calling）→ Intent 映射
- [ ] OpenAICompatProvider（cpp-httplib）
- **验收**：NPC 对话能引用"之前发生的事"和"当前世界状态"；LLM 调用期间游戏不卡帧；超时/限流自动降级。

### 阶段 5.5：本地推理（独立、可随时暂停）
- [ ] LocalLlamaCppProvider + 模型管理（GGUF、上下文窗口预算）
- **验收**：同 OpenAICompat 行为一致（同一 Provider 接口回归）。

### 阶段 6：集成与打磨（持续）
- [ ] 任务系统联动（派发 / 交付 / 进度条件，经 `dispatch_game_event` + 条件求值接口）
- [ ] 多 NPC 并发对话（线程池规模、请求合并、连接复用）
- [ ] 性能：对象池、结果缓存、内存分析；ASan/UBSan + 压力测试
- [ ] 存档/读档端到端测试（含对话中断、BT 重置、SocialGraph 保留、RNG 状态恢复）
- **验收**：同场景 50+ NPC 混用 FSM/LLM 无阻塞、无崩溃、决策日志完整。

---

## 11. MVP 里程碑（3 个月）

1. **第 1 个月**：框架骨架（Agent/AgentSystem + 事件总线 + 黑板 + TickContext）+ 接口层 v0.3 冻结 + MockWorld + FSM + 三级开关 → "巡逻 → 听到声音 → 警戒"无头测试跑通。
2. **第 2 个月**：BT/Utility 可切换 + 动作生命周期 + 对话图 + SocialGraph + 存档序列化。
3. **第 3 个月**：ScriptedMockProvider 接入对话流（异步 + pending 兜底）→ **OpenAI 兼容 API 打通**（不含 llama.cpp）。

> 红线一：LLM 调用永不进入同步游戏循环。
> 红线二：接口层（§3）一旦冻结，改动必须走评审——它是整个可移植性的根基。

---

## 12. 风险与对策

| 风险 | 对策 |
|---|---|
| LLM 阻塞游戏循环 | 异步化 + ready=false 兜底（阶段 5 验收项） |
| 工作线程触碰非线程安全对象 | 接口仅主线程调用 + AgentSnapshot 值语义 + 回调入队主线程消费 |
| 宿主 IWorld 重计算阻塞主线程 | 性能契约澄清（§3.2）：宿主自行异步化/时间片，框架不承诺重同步实现可行 |
| 取消 LLM 后仍在计费 | Provider::cancel 尽力中断 + 令牌预算/冷却兜底（阶段 5 评估真实能力） |
| llama.cpp 集成耗时/显存不足 | 独立阶段 5.5，Mock/云端先行 |
| 接口设计失误导致返工 | 阶段 1 接口冻结评审 + MockWorld 先行验证完备性 |
| 开关体系组合爆炸 | 编译期开关收窄（仅 llm/vector/可选库）+ 超集约束 fail-fast + 受控矩阵测试 |
| trace 回放失真（随机性） | 确定性 RNG 契约（TickContext::rng_seed），禁系统随机源 |
| AI 行为不可调试 | Decision Trace + 录制/回放回归（阶段 2 起） |
| 存档后"失忆" | ISerializable 契约 + MemoryEvent schema 阶段 1 冻结 + SocialGraph 世界级存储 + RNG 状态随存档 |
| 过度设计 | 三级开关默认最小集；EnTT 已移除；BT 读档重置到根（v1） |
| 多 NPC LLM 成本失控 | LLMGateway 令牌预算 + 每 NPC 冷却（阶段 5，非 6） |

---

## 13. 开放问题（v0.3 更新）

1. **decision 组合**（FSM 外套 BT 的分层仲裁）：v2 用"元决策器持有子决策器"实现，v1 不做。
2. **dispatch_game_event 的宿主侧契约**：v1 用 JSON 文档式约定（宿主自解释）；v2 引入 schema 校验。
3. **LLMGateway 调度细节**（优先级权重、预算分配策略）：阶段 5 开工时定。
4. **SocialGraph 存储形态**（内存索引结构 / 是否分区 / 序列化格式）：**归属已定案（世界级）**，存储形态阶段 4 确认。
5. **记忆 v2 向量库选型**（faiss vs 手写 HNSW）：有真实检索需求再定。
6. **接口进程化保险**：接口层保持"全值语义、无不透明指针、无回调持有宿主对象"，未来若要进程化，接口即天然 IPC 边界（零成本预留，不主动做）。
7. **Provider::cancel 的真实中断能力 + 冷却联动**：HTTP 层能否真正断开 / 本地推理能否中断，阶段 5 实现时评估，不影响接口形态（默认空操作已预留）。**LLMGateway 须把"取消是否成功"纳入冷却逻辑**：若未能真正中断旧请求，冷却不得立即重置，须等旧请求结束（或超时）才放行新请求——否则同一 NPC 会同时有两个请求在飞，双倍消耗令牌预算（并入本条的评审点 R4-6）。
8. **trace 回放确定性验收标准**（哪些随机源纳入种子、回归阈值）：阶段 2 开工前定。
9. **编译期开关候选清单最终确认**（llm / memory.vector / 可选导航库）：阶段 0 定。
10. **IWorld 宿主侧重计算异步化策略**（宿主职责，框架只给契约不规定实现）：阶段 3 评估。
11. **多场景 / 分片（v1 显式排除范围，冻结前修订 R4）**：v1 假设**单一活跃场景**（AgentSystem 持有单份当前场景 IWorld 引用，Agent 不持有 IWorld 指针）。多场景并发（副本实例、多人房间分片、多地图并行）是**显式排除**的 v1 范围，v2 再评估（届时可能需要 AgentSystem 按场景分片、或 Agent 归属场景）。宿主若打破此假设，属框架范围变更，须走接口评审。

---

## 14. 变更对照表（v0.2 → v0.3）

| 评审条目 | v0.3 处理 |
|---|---|
| R3-1 IWorld 单例 vs per-agent 自相矛盾 | 采纳方案 (b)：IWorld = 每场景共享门面，不绑定 Agent 身份，查询显式传参；self 状态只来自 IAgentBody::body_state()；AgentSystem 组装 AgentSnapshot（WorldSnapshot + BodyState）；Agent 不持有 IWorld 指针，由 AgentSystem 每 tick 注入 |
| R3-2 能力模块产出 Intent 无统一接口 | 采纳：新增 ICapability（id / registration_order / propose / on_event / to_json / from_json）+ 仲裁管线四步定案（决策器为权威意图源）；（注：`registration_order` 已于 R4-3 改由 Agent 注册时分配） |
| R3-3 dispatch_game_event 缺生命周期 | 采纳（修正）：返回 ActionHandle 保持对称；宿主不回报则按立即成功推进，任务进度真实依据是条件求值而非派发结果 |
| R3-4 SocialGraph 归属表述矛盾 | 采纳：归属定案（世界级），开放问题 #4 缩窄为存储形态待确认 |
| R3-5 取消语义模糊 | 采纳：取消回调（AgentSystem 令牌作废）与取消请求（Provider::cancel，尽力而为，默认空操作）分层 |
| R3-6 trace 回放确定性缺失 | 采纳：确定性契约——随机性必须经 TickContext::rng_seed 派生的可播种 RNG，禁 std::rand/系统时钟 |
| R3-7 重计算 IWorld 阻塞主线程 | 采纳：性能契约澄清——"仅驱动线程调用"是线程安全契约非性能许可，宿主自行异步化/时间片 |
| R3-8 编译期开关粒度无边界 | 采纳：编译期开关仅限 llm / memory.vector / 可选导航库，其余只做运行期开关；矩阵测试范围受控 |

**冻结前修订（并入 v0.3，第四轮评审 R4，不另出版本）**：

| 评审条目 | 处理 |
|---|---|
| R4-1 IDecisionMaker::tick 与 propose 双轨歧义 | 采纳：删除 tick()，IDecisionMaker 降为纯标记子接口；权威地位由 Agent 注册/配置指定（§3.5） |
| R4-2 能力模块直持接口指针绕过线程契约 | 采纳：Blackboard-only 契约显式化为硬性规则——模块不得缓存/持有 IWorld/IAgentBody 指针（§3.5） |
| R4-3 registration_order 自报撞值 | 采纳：改由 Agent::register_capability() 按注册顺序分配，ICapability 不再声明该值（§3.5） |
| R4-4 RNG 归属与序列化未写清 | 采纳：Agent 统一持有根 RNG 并随存档序列化，模块经 rng_seed+模块id 派生子序列，不单独持久化（§3.7、§7.3） |
| R4-5 单一活跃场景隐含假设 | 采纳：v1 显式排除多场景/分片，记为开放问题 #11（§13） |
| R4-6 cancel 失败与冷却重置的计费窗口 | 采纳：并入开放问题 #7——取消未成功时冷却不得立即重置（§13） |

**终检修订（R5，一致性自检，并入 v0.3，不另出版本）**：

| 条目 | 处理 |
|---|---|
| R5-1 §2.1"多场景/传送天然支持"与 §13 #11 的排除范围表述冲突 | 修正：顺序换图/传送支持；并发多场景/分片显式排除（§2.1） |
| R5-2 §3.4 管线第 3 条仍引用已从接口删除的 `registration_order` | 修正：改为"注册次序（Agent 注册时分配）" |
| R5-3 模块不得持有 IWorld 后，感知/导航查询由谁执行未交代 | 补全：AgentSystem 为唯一持有者，代执行查询并注入结果（§2.2、§3.5、§5.1） |
| R5-4 IAgentBody 由谁持有未写明 | 补全：Agent 持有（执行层），模块不得持有（§2.2） |
| R5-5 game_time 注释"读档后仍单调递增"不准确 | 修正：暂停不推进、同运行内单调、读档恢复存档值（§3.1） |
| R5-6 引用但未定义的契约类型清单 | 补注：GameEvent/Message/DialogueLine/Stimulus/PerceptionQuery/PerceptionResult/Blackboard/AgentEvent 同批定义，v1 保持 POD/JSON 值语义（§3.1） |
| R5-7 ISerializable 与 ICapability::to_json 关系未说明 | 补注：ICapability 内嵌契约；根 RNG/黑板/SocialGraph 等非能力状态实现 ISerializable（§7.3） |
| R5-8 §14 R3-2 行残留 registration_order 表述 | 加注 R4-3 更新指向 |
| R5-9 目录树 assets 行排版 | 修正：三目录分行并补注释 |

**阶段 1 落码修订（R6，实现澄清，并入 v0.3，不另出版本）**：

| 条目 | 处理 |
|---|---|
| R6-1 MockWorld/MockBody 位置 | 依赖方向约束（npc_agent/tests 不得 include game_adapter）→ 无头测试设施移至 `npc_agent/testing/`，game_adapter 仅保留 sample_adapter；§8.2 目录已更新 |
| R6-2 线程安全待处理队列 | 顺延至阶段 5（阶段 1 无工作线程；随 AsyncToken/LLM 一并实现）；阶段 1 清单已标注 |
| R6-3 TickContext::rng_seed 落实 | Agent 根 RNG 每 tick 推进派生并注入本地 TickContext；根状态随存档 hex 序列化（§3.7 契约不变） |
| R6-4 感知注入落实（R5-3） | AgentSystem 按 AgentConfig.perception 代执行 IWorld::sense，结果写入黑板键 `perceived_entities` |
| R6-5 存档恢复机制 | CapabilityFactory 注册表按能力 id 重建实例；Agent::restore 身体后挂（attach_body）；失败 fail-fast 定位缺失能力 |
| R6-6 热路径缓冲 | 仲裁候选与全局事件使用复用缓冲（CS-§7.5）；Intent 负载字符串分配列为阶段 6 池化目标 |

**历史对照（v0.1 → v0.2，摘要）**：接口层重构为 IWorld+IAgentBody 双接口、全 POD 契约类型、Intent variant+仲裁、ActionHandle 生命周期、领域动作走 dispatch_game_event、线程契约（主线程接口 + WorldSnapshot/AgentSnapshot 值语义 + 回调入队）、per-agent Agent + 全局 AgentSystem + scope 事件总线、TickContext、感知查询/推送分流、事件/黑板定界、DialogueSession、LLM 输出分级（Text/JsonSchema）、PromptBuilder/LLMGateway 列为模块且网关移入阶段 5、SocialGraph 世界级、EnTT 移除论证、BT 读档重置、Trace 录制回放、开关依赖闭包与超集约束、decision v1 互斥、序列化契约时机修正、阶段 5 拆出 5.5、MVP 减负。完整明细见 git 历史中 v0.2 版本文档的 §14。
