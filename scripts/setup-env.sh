#!/usr/bin/env bash
# setup-env.sh —— 一键安装并验证 NPC_TEST 开发环境（幂等，可重复执行）。
#
# 安装内容（已满足的项自动跳过）：
#   1. 构建工具：g++ ≥ 13 / cmake ≥ 3.28 / ninja / git
#   2. clang 工具链：clang-format / clang-tidy（门禁第 4 步格式检查）
#   3. sanitizer 运行库：libasan / libubsan（gcc ASan 构建，经冒烟编译验证）
#
# 用法（需 root；非 root 运行会自动以 sudo 重跑）：
#   sudo ./scripts/setup-env.sh              # 安装 + 验证
#   sudo ./scripts/setup-env.sh --fix-ssh    # 额外修复 /etc/ssh/ssh_config.d 权限
#                                            # （git push 报 "Bad owner or permissions" 时用）
#   sudo ./scripts/setup-env.sh --bootstrap  # 额外预热项目依赖（FetchContent）并构建 + 跑测试
#   sudo ./scripts/setup-env.sh --dry-run    # 只打印将执行的动作，不实际安装
#
# 支持发行版：Fedora/RHEL 系（dnf）、Debian/Ubuntu（apt）、Arch（pacman）、openSUSE（zypper）。
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

BOOTSTRAP=0
FIX_SSH=0
DRY_RUN=0
for arg in "$@"; do
    case "$arg" in
        --bootstrap) BOOTSTRAP=1 ;;
        --fix-ssh)   FIX_SSH=1 ;;
        --dry-run)   DRY_RUN=1 ;;
        *) echo "[setup] 未知参数: $arg（支持 --bootstrap / --fix-ssh / --dry-run）"; exit 2 ;;
    esac
done

info() { echo "[setup] $*"; }
warn() { echo "[setup] 警告: $*" >&2; }
die()  { echo "[setup] 错误: $*" >&2; exit 1; }
have() { command -v "$1" >/dev/null 2>&1; }

# ---------------------------------------------------------------- 提权
if [ "$(id -u)" -ne 0 ] && [ "$DRY_RUN" -eq 0 ]; then
    info "安装系统包需要 root 权限，以 sudo 重新执行..."
    exec sudo bash "$0" "$@"
fi

# ---------------------------------------------------------------- 发行版检测
PKG=""
if [ -f /etc/os-release ]; then
    # shellcheck disable=SC1091
    . /etc/os-release
    case "$ID" in
        fedora|rhel|centos|rocky|almalinux|ol) PKG=dnf ;;
        debian|ubuntu|linuxmint)               PKG=apt ;;
        arch|manjaro)                          PKG=pacman ;;
        opensuse*|suse)                        PKG=zypper ;;
    esac
fi
[ -n "$PKG" ] || die "无法识别的发行版（ID=${ID:-未知}），支持 dnf/apt/pacman/zypper"
info "发行版: ${NAME:-$ID}（$ID），包管理器: $PKG"

install_pkgs() {
    if [ "$DRY_RUN" -eq 1 ]; then
        info "  [dry-run] 将安装: $*"
        return 0
    fi
    info "安装: $*"
    case "$PKG" in
        dnf)    dnf install -y "$@" ;;
        apt)    apt-get update -y && apt-get install -y "$@" ;;
        pacman) pacman -Sy --noconfirm "$@" ;;
        zypper) zypper --non-interactive install "$@" ;;
    esac
}

# ---------------------------------------------------------------- 版本比较
NEED_GCC=13
NEED_CMAKE="3.28"
# ver_ge <current> <required>：current >= required 时返回 0（按整数段比较）
ver_ge() {
    awk -v a="$1" -v b="$2" 'BEGIN {
        na=split(a,A,"."); nb=split(b,B,".");
        for (i=1;i<=3;i++) {
            x=(i<=na)?A[i]+0:0; y=(i<=nb)?B[i]+0:0;
            if (x>y) exit 0; if (x<y) exit 1;
        }
        exit 0;
    }'
}

# ---------------------------------------------------------------- 各发行版包名
case "$PKG" in
    dnf)
        CXX_PKG=(gcc-c++)
        CMAKE_PKG=(cmake)
        NINJA_PKG=(ninja-build)
        GIT_PKG=(git)
        CLANG_PKG=(clang clang-tools-extra)
        ASAN_PKG=(libasan libubsan) ;;
    apt)
        CXX_PKG=(build-essential)
        CMAKE_PKG=(cmake)
        NINJA_PKG=(ninja-build)
        GIT_PKG=(git)
        CLANG_PKG=(clang clang-format clang-tidy)
        ASAN_PKG=() ;;
    pacman)
        CXX_PKG=(gcc)
        CMAKE_PKG=(cmake)
        NINJA_PKG=(ninja)
        GIT_PKG=(git)
        CLANG_PKG=(clang)
        ASAN_PKG=() ;;
    zypper)
        CXX_PKG=(gcc-c++)
        CMAKE_PKG=(cmake)
        NINJA_PKG=(ninja)
        GIT_PKG=(git)
        CLANG_PKG=(clang clang-tools-extra)
        ASAN_PKG=() ;;
esac

# ---------------------------------------------------------------- 1. 构建工具
gcc_ok() { have g++ && ver_ge "$(g++ -dumpfullversion)" "$NEED_GCC"; }
if gcc_ok; then
    info "g++ $(g++ -dumpfullversion) ≥ ${NEED_GCC} ✓"
else
    info "g++ 缺失或版本 < ${NEED_GCC}，安装 ${CXX_PKG[*]}"
    install_pkgs "${CXX_PKG[@]}"
    gcc_ok || die "g++ 安装后仍不满足 ≥ ${NEED_GCC}，请手动处理"
fi

cmake_ok() { have cmake && ver_ge "$(cmake --version | head -1 | awk '{print $3}')" "$NEED_CMAKE"; }
if cmake_ok; then
    info "cmake $(cmake --version | head -1 | awk '{print $3}') ≥ ${NEED_CMAKE} ✓"
else
    info "cmake 缺失或版本 < ${NEED_CMAKE}，安装 ${CMAKE_PKG[*]}"
    install_pkgs "${CMAKE_PKG[@]}"
    cmake_ok || die "cmake 安装后仍不满足 ≥ ${NEED_CMAKE}，请手动处理"
fi

if have ninja; then
    info "ninja ✓"
else
    install_pkgs "${NINJA_PKG[@]}"
    have ninja || die "ninja 安装失败"
fi

if have git; then
    info "git ✓"
else
    install_pkgs "${GIT_PKG[@]}"
    have git || die "git 安装失败"
fi

# ---------------------------------------------------------------- 2. clang 工具链
if have clang-format && have clang-tidy; then
    info "clang-format / clang-tidy ✓"
else
    info "clang-format/clang-tidy 缺失，安装 clang 工具链..."
    install_pkgs "${CLANG_PKG[@]}" || true
    if [ "$DRY_RUN" -eq 1 ]; then
        info "  [dry-run] 实际安装后将自动验证 clang-format/clang-tidy"
    elif have clang-format && have clang-tidy; then
        info "clang-format / clang-tidy 已就绪 ✓"
    else
        warn "clang-format/clang-tidy 仍缺失：门禁第 4 步格式检查将跳过，请按发行版手动安装"
    fi
fi

# ---------------------------------------------------------------- 3. sanitizer 运行库
# 冒烟编译验证 gcc ASan 链接与运行（本机曾缺 /usr/lib64/libasan.so.8）。
asan_ok() {
    local src=/tmp/npc_asan_smoke
    printf 'int main(){volatile int* p=new int[4]; delete[] p; return 0;}\n' > "$src.cpp"
    if g++ -fsanitize=address "$src.cpp" -o "$src" 2>/dev/null &&
        ASAN_OPTIONS=detect_leaks=0 "$src" >/dev/null 2>&1; then
        rm -f "$src" "$src.cpp"
        return 0
    fi
    rm -f "$src" "$src.cpp"
    return 1
}

if asan_ok; then
    info "gcc ASan 运行库可用 ✓"
else
    warn "gcc ASan 运行库缺失（libasan）"
    if [ "${#ASAN_PKG[@]}" -gt 0 ]; then
        install_pkgs "${ASAN_PKG[@]}"
        if [ "$DRY_RUN" -eq 1 ]; then
            info "  [dry-run] 实际安装后将重新冒烟验证 gcc ASan"
        elif asan_ok; then
            info "gcc ASan 运行库已就绪 ✓"
        else
            warn "安装后仍不可用：可用 clang 做 sanitize 构建（clang++ -fsanitize=address）"
        fi
    elif [ "$PKG" = apt ]; then
        # Ubuntu 上 libasan 包名随 gcc 版本变化（libasan8/libasan6...），取最新
        local p
        p=$(apt-cache search --names-only '^libasan[0-9]+$' 2>/dev/null | awk '{print $1}' | sort -V | tail -1)
        if [ -n "$p" ]; then
            install_pkgs "$p"
            if [ "$DRY_RUN" -eq 1 ]; then
                info "  [dry-run] 实际安装后将重新冒烟验证 gcc ASan"
            elif asan_ok; then
                info "gcc ASan 运行库已就绪 ✓"
            else
                warn "安装后仍不可用"
            fi
        else
            warn "未找到 libasan 包，请手动安装"
        fi
    else
        warn "本发行版 libasan 通常随 gcc 提供，请检查 gcc 安装；或改用 clang 做 sanitize 构建"
    fi
fi

# ---------------------------------------------------------------- 4. SSH 配置修复（可选）
fix_ssh() {
    local dir=/etc/ssh/ssh_config.d
    info "检查 SSH 配置目录权限（git push 报 'Bad owner or permissions' 的病灶）..."
    [ -d "$dir" ] || { warn "$dir 不存在，跳过"; return; }
    if [ "$DRY_RUN" -eq 1 ]; then
        info "  [dry-run] 将把 $dir 及其内容恢复为 root:root"
        return
    fi
    local changed=0
    if [ "$(stat -c %U "$dir")" != root ] || [ "$(stat -c %G "$dir")" != root ]; then
        chown root:root "$dir"
        chmod 755 "$dir"
        changed=1
    fi
    for f in "$dir"/*; do
        [ -e "$f" ] || [ -L "$f" ] || continue
        if [ -L "$f" ]; then
            if [ "$(stat -c %U "$f")" != root ]; then
                chown -h root:root "$f"
                changed=1
            fi
        else
            if [ "$(stat -c %U "$f")" != root ] || [ "$(stat -c %G "$f")" != root ]; then
                chown root:root "$f"
                changed=1
            fi
            chmod 644 "$f"
        fi
    done
    [ "$changed" -eq 1 ] && info "已恢复 $dir 权限为 root:root ✓" || info "SSH 配置权限正常 ✓"

    # ---- 以真实用户身份验证 GitHub 连通性（root 无密钥，用 root 验证必然失败）----
    local real_user="${SUDO_USER:-$USER}"
    local user_home
    user_home=$(getent passwd "$real_user" | cut -d: -f6)
    [ -n "$user_home" ] || user_home="$HOME"
    [ "$real_user" = root ] && user_home=/root

    info "以用户 $real_user 预置 GitHub 主机指纹并验证连通性..."
    mkdir -p "$user_home/.ssh"
    # 预置 known_hosts，避免交互式指纹确认卡住（幂等）
    if ! grep -q '^github.com ' "$user_home/.ssh/known_hosts" 2>/dev/null; then
        ssh-keyscan -t ed25519,rsa github.com >> "$user_home/.ssh/known_hosts" 2>/dev/null || true
    fi
    if [ "$real_user" != root ]; then
        chown -R "$real_user":"$(id -gn "$real_user")" "$user_home/.ssh"
    fi

    # 连通性验证：非交互 + 超时，失败给出排查清单
    if [ -d .git ] && git remote -v 2>/dev/null | grep -q origin; then
        local run_as=(env)
        [ "$real_user" != root ] && run_as=(sudo -u "$real_user" env)
        if timeout 25 "${run_as[@]}" \
            GIT_SSH_COMMAND="ssh -o BatchMode=yes -o ConnectTimeout=10" \
            GIT_TERMINAL_PROMPT=0 git ls-remote origin HEAD >/dev/null 2>&1; then
            info "git 远端连通性验证通过 ✓（用户 $real_user）"
        else
            warn "git 远端仍不可达（用户 $real_user）。请按序检查："
            warn "  1) 密钥是否存在: ls $user_home/.ssh/（没有则: ssh-keygen -t ed25519）"
            warn "  2) 公钥是否已添加: https://github.com/settings/keys"
            warn "  3) 网络受限可改 HTTPS: git remote set-url origin https://github.com/Junehawy/NPC_TEST.git"
            warn "  排查命令: ssh -vT git@github.com（看失败在哪一步）"
        fi
    fi
}
[ "$FIX_SSH" -eq 1 ] && fix_ssh

# ---------------------------------------------------------------- 5. 项目预热（可选）
if [ "$BOOTSTRAP" -eq 1 ]; then
    [ -f CMakeLists.txt ] || die "--bootstrap 需在仓库根目录运行"
    if [ "$DRY_RUN" -eq 1 ]; then
        info "[dry-run] 将执行: cmake 配置（FetchContent 预热）→ 构建(-Werror) → ctest"
        info "[dry-run] 项目部分将以真实用户 ${SUDO_USER:-$USER} 身份执行（root 构建会污染 build/ 属主）"
    else
        # 项目部分以真实用户身份执行，避免 build/ 目录属主变成 root
        real_user="${SUDO_USER:-$USER}"
        run_as=(env)
        if [ "$(id -u)" -eq 0 ] && [ "$real_user" != root ]; then
            run_as=(sudo -u "$real_user" env)
        fi
        fetch_base="${NPC_AGENT_FETCH_BASE:-https://github.com}"
        info "预热项目依赖（FetchContent base=$fetch_base，单步超时 900s）..."
        timeout 900 "${run_as[@]}" cmake --preset release \
            -DNPC_AGENT_FETCH_BASE="$fetch_base" || {
            warn "cmake 配置失败或超时。网络受限时用镜像重试："
            warn "  sudo NPC_AGENT_FETCH_BASE=https://ghproxy.net/https://github.com ./scripts/setup-env.sh --bootstrap"
            exit 1
        }
        "${run_as[@]}" cmake --build --preset release || die "构建失败"
        (cd build && "${run_as[@]}" ctest --output-on-failure) || die "测试失败"
        info "项目构建与测试通过 ✓"
    fi
fi

info "环境检查完成。项目验证可随时执行: ./scripts/check-gate.sh"
