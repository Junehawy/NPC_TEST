# 审查报告：setup-env.sh SSH 验证与 bootstrap 修复（2025-08-16）

## 1. 变更范围
- `scripts/setup-env.sh`：
  - `--fix-ssh`：连通性验证改为**以真实用户（SUDO_USER）身份**执行（此前以 root 验证，root 无密钥必然失败）；预置 GitHub 主机指纹到真实用户 known_hosts（消除交互式确认）；验证加 `BatchMode + ConnectTimeout + timeout 25`，失败输出三步排查清单。
  - `--bootstrap`：项目部分**降权到真实用户**执行（此前 root 构建污染 build/ 属主，实测污染为 nobody）；cmake 配置加 900s 超时；新增镜像开关 `NPC_AGENT_FETCH_BASE` 透传与失败提示。
- `CMakeLists.txt`：新增 `NPC_AGENT_FETCH_BASE` cache 变量（默认 https://github.com，网络受限时可指向镜像）；撤回 `GIT_PROGRESS`（会改变 FetchContent 克隆命令、触发 stamp 失效重下载）。

## 2. 门禁结果
- [x] check-gate.sh 全绿（依赖方向 / 构建 -Werror / ctest 4/4 / **clang-format 检查已启用**——用户侧已装 clang-tools-extra）
- [x] `bash -n` 语法通过；dry-run 输出正确
- [x] 实测取证：`ssh -F /dev/null -T git@github.com` 返回 `Hi Junehawy!`（密钥正常）；https 浅克隆 2.5s（网络正常）

## 3. 清单核对（review-process §3 A~F）
| 组 | 结果 | 备注 |
|---|---|---|
| A 耦合 | 通过 | 无 C++ 依赖方向变化 |
| B 线程 | 通过 | 不适用 |
| C 内存性能 | 通过 | 不适用（脚本） |
| D 注释命名 | 通过 | 失败分支给出可执行的排查清单 |
| E 冗余 | 通过 | 幂等（known_hosts 已存在则跳过） |
| F 测试 | 通过 | dry-run + 实测取证（本环境无法以 root 全量实测，用户机器已部分验证） |

## 4. 问题与修复
| # | 问题 | 根因 | 修复方式 | 状态 |
|---|---|---|---|---|
| 1 | --fix-ssh 验证"仍不可达" | 脚本以 root 验证，/root/.ssh 不存在（root 无密钥）；且无 accept-new 引发交互式指纹询问 | 以真实用户身份验证 + 预置指纹 + BatchMode + 超时 + 排查清单 | 已修复 |
| 2 | --bootstrap 卡住 / 构建失败 | root 运行把 build/_deps 污染为 nobody 属主；GIT_PROGRESS 改变克隆命令 → stamp 失效 → 重下载 → 无法删除 nobody 目录 | 项目部分降权真实用户；撤回 GIT_PROGRESS；超时 + 镜像开关；旧目录改名挪开恢复本机构建 | 已修复 |
| 3 | 本机 build/_deps 残留 nobody 属主目录 | 同上 | 已改名 `catch2-src.nobody-bak`（构建正常）；用户可用 `sudo rm -rf build/_deps/*.nobody-bak` 清理 | 待用户清理 |

## 5. 结论
☑ 通过，可 push。
用户侧复验命令：`sudo rm -rf build/_deps/*.nobody-bak && sudo ./scripts/setup-env.sh --fix-ssh --bootstrap`
