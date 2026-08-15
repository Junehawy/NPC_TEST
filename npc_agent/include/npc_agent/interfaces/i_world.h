// IWorld —— 环境接口（RA-§3.2）：每场景一个共享门面，不绑定任何 Agent 身份。
// 适配层（游戏侧）实现；查询全部显式携带参数，无隐含 "self"。
// 线程契约：全部方法【驱动线程】调用（RA-§2.1）；实现不得重同步阻塞（RA-§3.2 性能契约）。
#pragma once

#include <string>
#include <vector>

#include "npc_agent/interfaces/types.h"

namespace npc_agent {

// 感知查询（数据驱动，新增感知类型走 extra/扩展而非加虚函数，RA-§3.2）。
struct PerceptionQuery {
    Vec3 origin;                          // 感知源位置
    float radius = 0.0f;                  // 感知半径（0 = 不感知）
    std::string sense_type = "sight";     // 感知类型（sight/hearing/...）
    Vec3 facing;                          // 朝向（零向量 = 全向）
    float facing_half_angle_deg = 180.0f; // 视野半角（180 = 全向）
    json extra = json::object();          // 扩展过滤条件（宿主自解释）
};

// 感知结果中的单个实体。
struct PerceivedEntity {
    std::string id;
    Vec3 position;
    json attributes = json::object();
};

struct PerceptionResult {
    std::vector<PerceivedEntity> entities;
};

// 瞬时刺激（枪声/碰撞/玩家发起对话…），由宿主推送（RA-§3.2）。
struct Stimulus {
    std::string type;
    Vec3 position;
    float magnitude = 1.0f;
    std::string source_id;
    json payload = json::object();
};

struct IWorld {
    virtual ~IWorld() = default;

    // 当前时间上下文（AgentSystem 驱动 tick 的入参来源）。
    virtual TickContext tick_context() const = 0;

    // 状态性信息走查询；空间分区/过滤是宿主职责（性能契约，RA-§3.2）。
    virtual PerceptionResult sense(const PerceptionQuery& q) const = 0;

    // 宿主推送瞬时刺激；由 AgentSystem 转为全局事件广播。
    virtual void inject_stimulus(const Stimulus& s) = 0;

    // 导航查询（寻路实现属于宿主侧）。
    virtual bool can_reach(Vec3 from, Vec3 to) const = 0;
    virtual std::vector<Vec3> find_path(Vec3 from, Vec3 to) const = 0;

    // 环境快照（不含 self；self 见 IAgentBody::body_state）。
    virtual WorldSnapshot snapshot() const = 0;
};

} // namespace npc_agent
