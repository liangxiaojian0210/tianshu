# 天枢 (TIANSHU) — 开发计划表

> **文档定位**：按"架构层 → 框架 → 功能点"三级拆解到 issue 粒度，每个功能点可直接入 GitHub Issue。
> **维护者**：Pride Leong
> **状态**：v0.2（2026-08） · Phase 0 **完成** · Phase 1 **进行中**
> **关联**：[00-overview.md](./00-overview.md) · [01-roadmap.md](./01-roadmap.md) · [adr/0001](./adr/0001-dsl-form.md) · [adr/0002](./adr/0002-cyber-relation.md)

---

## 阅读指南

### 三级层次

```
架构层 (Layer)     INFRA / L4 / L1 / L2 / L3 / X / T
   ↓
框架 (Framework)   F-前缀，如 L4-TRANS（L4 层 Transport 框架）
   ↓
功能点 (Feature)   ID = 架构层-框架-序号[.子序号]
                    如 L4-TRANS-3.2
```

### 功能点字段

| 字段 | 含义 |
|---|---|
| **ID** | 唯一编号，建议直接作为 GitHub Issue label |
| **描述** | 一句话说清楚做什么 |
| **依赖** | 必须先完成的功能点 ID（无则留 `-`） |
| **优先级** | P0（必做基础） / P1（重要） / P2（增强） |
| **估算** | 点数（1 点 = 1 理想人日，不含调试/回归） |
| **Phase** | 0/1/2/3，对应 [01-roadmap.md](./01-roadmap.md) |
| **验收** | 完成的客观判据 |

### 架构层速览

| 架构层 | 名称 | 性质 | 关键依赖 |
|---|---|---|---|
| **INFRA** | 基础设施 | 工程基座 | 无 |
| **L4** | 运行时（含 transport、协程、core API） | 最底层实现 | INFRA |
| **L1** | DSL + Trace + Compiler | 上层范式 | L4 |
| **L2** | 血缘与容错 | 横切 | L4 |
| **L3** | SLA 调度 | 横切 | L4, L1 |
| **X** | 横切确定性（构建/执行/追溯） | 横切 | L1-L4 |
| **T** | 工具链 | 上层 | L1-L4 |

**自底向上建设顺序**：INFRA → L4 → L1 → L2/L3（并行）→ X → T。

---

## 当前进度

> 更新时间：2026-08-24 · 详见 [CHANGELOG.md](../CHANGELOG.md)

### Phase 完成度

| Phase | 状态 | 完成功能点 | 工作量（点） | 备注 |
|---|---|---|---|---|
| **Phase 0** | ✅ **完成** | 见下方清单 | ~30/30 | 双构建系统 + 测试 + CI + 风格规范 |
| Phase 1 | 🔨 **进行中** | 40/~155 | ~42/155 | PRIM/SCHED/CORE/COMP/MAIN/DSL/LINEAGE/MONITOR 主链路贯通 |
| Phase 2 | ❌ 未开始 | 0/~210 | 0/~210 | - |
| Phase 3 | ❌ 未开始 | 0/~290 | 0/~290 | - |

### Phase 1 已完成功能点（2026-08）

| 架构层 | 已完成 ID | 数量 | 验收 |
|---|---|---|---|
| **L4-PRIM** | 1, 2, 3, 4, 5, 6 | 6 | 单测 + 并发测试全过，100% 函数覆盖 |
| **L4-SCHED** | 1, 2, 3 | 3 | 优先级 + 多 worker 单测通过（回调式，ADR-0019） |
| **L4-TRANS** | 3, 4, 5, 18, 19, 20, 24 | 7 | **跨进程吞吐 4.17M msg/s（验收 ≥1M）、RTT p50=58µs（验收 <1ms）**，fork 集成测试 + talker/listener demo，见 [shm-transport-notes](./development/shm-transport-notes.md) |
| **L4-CORE** | 1, 2, 3, 4, 5, 6, 7, 10 | 8 | MessageConcept + 类型化 Writer/Reader + DataVisitor/Dispatcher/Notifier（AllLatest 融合） |
| **L4-COMP** | 1, 2, 3, 10 | 4 | Component/TwoInput/Timer/TimerSource + 工厂注册宏 |
| **L4-MAIN** | 1 | 1 | **ti + ti-launch DAG 启动器，hello DAG 端到端 15/15**（命名决策见 [adr/0002 术语边界](./adr/0002-cyber-relation.md)） |
| **ti-monitor** | — | 1 | TUI 通道监视：vi 键位 / 暂停锁缓冲 / 帧导航 / fork E2E + 字段解码（[adr/0020](./adr/0020-message-reflection-monitor.md)） |
| **状态即数据 M1** | — | 1 | 状态通道原语 + 恢复协议（checkpoint 血缘定吸收点 + 后缀重放，EXACT MATCH 验证）（[adr/0027](./adr/0027-state-as-data-channel-taxonomy.md)）；含 M0 区间血缘 + 通道历史环 |
| **切片输入 M2** | — | 1 | span_join 触发对齐切片 + 区间血缘 + lidar×IMU 运动补偿 demo（[adr/0026](./adr/0026-slice-input-model.md) Phase A+B） |
| **record 基底** | — | 1 | RecordFile + record_to/replay_from；离线 == 在线 39/39 bit-identical（[adr/0026](./adr/0026-slice-input-model.md) Phase C） |
| **组件血缘接通** | — | 1 | WriterBase 三参 write + provider 1:1 配对 + 泵回派生；环跨组件边界展开（[adr/0025](./adr/0025-from-component-reference.md) 修正） |
| **from() 组件引用** | — | 1 | DSL 引用注册组件（驱动/底盘）：三问三答 + 输出通道注入 + quiesce 静默（[adr/0025](./adr/0025-from-component-reference.md)）；L1-DSL v0.7 子集 |
| **L4-TRANS-33** | — | 1 | 通道 schema 元数据分发：SHM sidecar 段 `/tianshu_schema_<hash>` + typed writer 自动发布 + monitor 免参数自动解码（[adr/0020](./adr/0020-message-reflection-monitor.md) Phase 2） |
| **L4-TRANS-21** | — | 1 | kAuto 自动选路：双发写端 + 读端注册表判定，顺序无关（[adr/0023](./adr/0023-kauto-transport-v0.md)） |
| **L1-DSL v0.6** | — | 1 | 声明式 flow + 解释器 + join + 多消费者 + op 自定义算子（生命周期/自举/tap 断环，[adr/0024](./adr/0024-dsl-op-primitive.md)）；L1-DSL-1..3 的 v0.6 子集 |
| **L2-LIN v0.5** | — | 1 | 消息级血缘自动级联 + 分支模型（join DAG 出处）（[adr/0022](./adr/0022-lineage-v0.md) v0.5 增补）；L2-LIN-1..2 的 v0.5 子集 |
| 合计 | | **40 项**（L4-COMP-6 AllLatest 已随 DataVisitor 落地；kAuto 已随 [adr/0023](./adr/0023-kauto-transport-v0.md) 落地） |

### Phase 0 已完成功能点

| 架构层 | 已完成 ID | 数量 |
|---|---|---|
| **INFRA-BUILD** | 1, 3, 4, 7, 8, 9, 11, 15, 16, 18 | 10 |
| **INFRA-DEPS** | 1, 2 | 2 |
| **INFRA-PROFILE** | 1, 2 | 2 |
| **INFRA-CI** | 1, 2, 12 | 3 |
| **INFRA-TEST** | 1, 2, 3 | 3 |
| **INFRA-BENCH** | 1 | 1 |
| **INFRA-DOC** | 1, 3, 7 | 3 |
| **INFRA-API** | 2, 3, 4 | 3 |
| **INFRA-LOG** | — (设计完成，实现待 Phase 1) | 0 |
| **INFRA-PARAM** | — (设计完成，实现待 Phase 1) | 0 |
| **Library** | version.h/.cc + hello_world + hello_test + version_benchmark | 4 文件 |
| **ADR** | 0001-0018 | 18 份 |
| **Evaluation** | 0001-0003 | 3 份 |
| **合计** | | **~27 功能点** |

> 📌 **轻架构与多端约束**：所有框架设计必须支持 5 个 profile（desktop/server/vehicle/embedded/mcu），详见 [adr/0005](./adr/0005-lightweight-multiplatform.md)。依赖治理（[F-INFRA-DEPS](#f-infra-deps--依赖治理)）、OSAL/HAL 抽象（[F-INFRA-OSAL](#f-infra-osal--os-抽象层) / [F-INFRA-HAL](#f-infra-hal--硬件抽象层)）是横切硬约束。

---

## INFRA · 基础设施

工程基座，所有上层都依赖。

### F-INFRA-DEPS · 依赖治理（详见 [adr/0005 §依赖治理原则](./adr/0005-lightweight-multiplatform.md)）

| ID | 描述 | 依赖 | 优先级 | 估算 | Phase | 验收 |
|---|---|---|---|---|---|---|
| INFRA-DEPS-1 | `ALLOWED_DEPS.txt` 白名单（含每条依赖的档位：🟢🟡🟠🔴 + 批准 ADR 链接） | - | P0 | 1 | 0 | 至少 10 条入库 |
| INFRA-DEPS-2 | 依赖申请模板（ADR 草稿含：用途/体积/替代方案/profile 兼容性） | - | P0 | 0.5 | 0 | 模板可复用 |
| INFRA-DEPS-3 | CI 守护脚本（`bazel query deps` / `ldd` 对比白名单，新增未审批依赖 fail） | INFRA-BUILD-8 | P0 | 2 | 1 | CI 检出新增未审批依赖 |
| INFRA-DEPS-4 | 二进制体积回归（每个 profile 跑 `size` + 阈值告警） | INFRA-CI-1 | P1 | 2 | 2 | vehicle < 50MB / embedded < 10MB / mcu < 1MB 阈值生效 |
| INFRA-DEPS-5 | 依赖体积审计（每个依赖贡献的 .text/.data/.bss 大小） | 3 | P2 | 2 | 2 | 输出 markdown 报告 |
| INFRA-DEPS-6 | 第三方依赖镜像（避免 CI 拉取失败，内部托管） | INFRA-BUILD-11 | P1 | 2 | 2 | 内部镜像同步 |

### F-INFRA-OSAL · OS 抽象层（详见 [adr/0005 §OSAL](./adr/0005-lightweight-multiplatform.md)）

| ID | 描述 | 依赖 | 优先级 | 估算 | Phase | 验收 |
|---|---|---|---|---|---|---|
| INFRA-OSAL-1 | OSAL 接口定义（thread/mutex/condvar/time/shm/socket/file/signal/env） | - | P0 | 2 | 1 | 接口锁定 |
| INFRA-OSAL-2 | POSIX backend（Linux / macOS） | 1 | P0 | 3 | 1 | 全 OSAL 接口实现 + 单测 |
| INFRA-OSAL-3 | QNX backend（POSIX 子集 + QNX 特有） | 1 | P1 | 2 | 2 | ORIN QNX 跑通 |
| INFRA-OSAL-4 | FreeRTOS backend（task / semaphore / stream buffer） | 1 | P1 | 4 | 3 | Cortex-M7 跑通 |
| INFRA-OSAL-5 | Zephyr backend | 1 | P2 | 3 | 3 | 单测覆盖 |
| INFRA-OSAL-6 | bare-metal backend（关中断 / busy-poll / disabled stubs） | 1 | P2 | 2 | 3 | 单测覆盖 |
| INFRA-OSAL-7 | OSAL 兼容性测试套（同测试用例多 backend 跑） | 2,3,4 | P1 | 3 | 2 | ≥ 90% 用例覆盖 |

### F-INFRA-HAL · 硬件抽象层（详见 [adr/0005 §HAL](./adr/0005-lightweight-multiplatform.md)）

| ID | 描述 | 依赖 | 优先级 | 估算 | Phase | 验收 |
|---|---|---|---|---|---|---|
| INFRA-HAL-1 | HAL 接口定义（cpu/cpuset/cache/pmu/gpu/thermal/power） | - | P0 | 1.5 | 1 | 接口锁定 |
| INFRA-HAL-2 | Linux HAL backend（sysfs + /proc + sched_setaffinity） | 1 | P0 | 3 | 1 | x86_64/aarch64 跑通 |
| INFRA-HAL-3 | QNX HAL backend | 1 | P1 | 2 | 2 | 单测覆盖 |
| INFRA-HAL-4 | FreeRTOS HAL backend（MCU 拓扑/缓存简化） | 1 | P2 | 2 | 3 | Cortex-M 跑通 |
| INFRA-HAL-5 | PMU 性能计数器 backend（optional） | 2 | P2 | 3 | 3 | 至少 5 个事件可读 |
| INFRA-HAL-6 | GPU 资源 backend（CUDA / OpenCL / NPU adapter，**一类公民**，详见 [adr/0006](./adr/0006-gpu-acceleration.md)） | 2 | P1 | 6 | 2 | vehicle profile GPU 监控可用 |
| INFRA-HAL-7 | 国产 NPU adapter（华为昇腾 / 地平线 BPU / 寒武纪，按需） | 6 | P2 | 4 | 3 | 至少 1 种 NPU 跑通 |

### F-INFRA-PROFILE · Profile 配置体系

| ID | 描述 | 依赖 | 优先级 | 估算 | Phase | 验收 |
|---|---|---|---|---|---|---|
| INFRA-PROFILE-1 | 5 个 profile 定义（desktop/server/vehicle/embedded/mcu） | INFRA-BUILD-15,16 | P0 | 1 | 0 | 每个 profile 有完整 `.bazelrc` + `CMakePresets.json` 条目 |
| INFRA-PROFILE-2 | Profile 条件编译宏（`TIANSHU_PROFILE_*`） | 1 | P0 | 1 | 0 | 模块可按 profile 启用/禁用 |
| INFRA-PROFILE-3 | Profile 模块矩阵自动生成（从 [adr/0005 §Profile 启用模块矩阵](./adr/0005-lightweight-multiplatform.md) 推导） | 1, 2 | P0 | 2 | 1 | CI 校验矩阵一致 |
| INFRA-PROFILE-4 | Profile 资源预算回归（每个 profile 跑 size + 启动时间） | 3, INFRA-DEPS-4 | P1 | 2 | 2 | 5 profile 阈值生效 |
| INFRA-PROFILE-5 | Profile 文档生成（自动从代码推导 profile 能力清单） | 3 | P2 | 2 | 3 | markdown 输出 |

### F-INFRA-API · API 规范与多语言绑定（详见 [adr/0007](./adr/0007-api-spec-multi-language.md)）

> ⚠️ 多语言 SDK 实现非 P0，归属 Phase 2/3。Phase 0/1 只锁 API 规范与 C ABI 设计。

| ID | 描述 | 依赖 | 优先级 | 估算 | Phase | 验收 |
|---|---|---|---|---|---|---|
| INFRA-API-1 | API 规范文档（命名/ABI/SemVer/头文件组织/错误处理/内存所有权） | - | P0 | 3 | 0 | 文档 review 通过 |
| INFRA-API-2 | C ABI 设计（opaque handle 模式 + create/destroy 配对） | 1 | P0 | 4 | 0 | 至少覆盖 Node/Reader/Writer/Component |
| INFRA-API-3 | 公开/私有头文件分离（`include/tianshu/` vs `src/`） | 1 | P0 | 1.5 | 0 | CI lint 校验 |
| INFRA-API-4 | 错误码枚举 + `tianshu_status_str` 实现 | 1 | P0 | 1 | 0 | ≥ 20 个错误码 |
| INFRA-API-5 | API lint 守护（命名规范 + ABI 兼容性 + 公开头文件无私有依赖） | 1, 2, 3 | P0 | 3 | 1 | CI 检出违规即 fail |
| INFRA-API-6 | ABI 兼容性检查（libabigail / abi-dumper） | 2 | P1 | 2 | 1 | 每个 release 输出 ABI diff 报告 |
| INFRA-API-7 | 消息跨语言契约（Protobuf 序列化 + 类型注册机制） | 2, L4-CORE-1 | P0 | 3 | 1 | 跨语言消息可发收 |
| INFRA-API-8 | Python SDK（pybind11，含 DSL/trace/compile API） | 1-3, 7 | P1 | 8 | 2 | Python 用户可写 flow + 以 ti launch 运行 |
| INFRA-API-9 | Rust SDK（cxx + cbindgen + bindgen） | 1-3, 7 | P2 | 6 | 2-3 | Rust 用户可写 flow + 以 ti launch 运行 |
| INFRA-API-10 | Go SDK（cgo + 手写绑定） | 1-3, 7 | P2 | 5 | 3 | Go 用户可写 flow + 以 ti launch 运行 |
| INFRA-API-11 | Node.js SDK（napi-rs，Rust 写绑定） | 9, 1-3, 7 | P2 | 5 | 3 | Node 用户可写 flow + 以 ti launch 运行 |
| INFRA-API-12 | 跨语言集成测试套（每种 SDK 跑同一组 flow，对比输出） | 8-11 | P1 | 4 | 2-3 | ≥ 90% 用例覆盖 |
| INFRA-API-13 | 多语言 SDK 文档（每种 SDK 独立 quickstart + API 参考） | 8-11 | P1 | 4 | 2-3 | 每种 SDK 有完整文档 |

### F-INFRA-BUILD · 构建系统（CMake + Bazel 双轨，详见 [adr/0003](./adr/0003-build-system.md)）

| ID | 描述 | 依赖 | 优先级 | 估算 | Phase | 验收 |
|---|---|---|---|---|---|---|
| INFRA-BUILD-1 | CMake 项目骨架（顶层 `CMakeLists.txt` + `cmake/` 模块） | - | P0 | 1 | 0 | `cmake -B build && cmake --build build` 跑通 hello world |
| INFRA-BUILD-2 | CMake 多模块组织（`tianshu/` 子目录 tree） | 1 | P0 | 1 | 0 | 每个 `tianshu/<module>/CMakeLists.txt` 可独立开关 |
| INFRA-BUILD-3 | C++20 启用 + 编译器最低版本要求（GCC 11+ / Clang 14+） | 1 | P0 | 0.5 | 0 | CI 矩阵覆盖 GCC 11/13 + Clang 14/16 |
| INFRA-BUILD-4 | `compile_commands.json` 生成（clangd / IDE 友好） | 1 | P1 | 0.5 | 0 | VSCode/clangd 跳转可用 |
| INFRA-BUILD-5 | CMake 交叉编译工具链文件（aarch64 ORIN / QNX） | 1 | P1 | 2 | 1 | ORIN 上能跑 hello world |
| INFRA-BUILD-6 | CMake 安装包/发行物（`cmake --install` 产出 .so + header） | 2 | P1 | 1 | 2 | 第三方项目 `find_package(tianshu)` 可用 |
| INFRA-BUILD-7 | Bazel workspace 骨架（`WORKSPACE` + 顶层 `BUILD` + `.bazelrc`） | - | P0 | 1.5 | 0 | `bazel build //...` 跑通 hello world |
| INFRA-BUILD-8 | Bazel 多模块组织（每个 `tianshu/<module>/BUILD.bazel`） | 7 | P0 | 2 | 0 | 每个子模块可独立 `bazel build //tianshu/<m>:*` |
| INFRA-BUILD-9 | Bazel 工具链 + `.bazelrc` 配置（gcc/clang/aarch64 切换） | 7 | P0 | 2 | 1 | `bazel build //... --config=aarch64` 可用 |
| INFRA-BUILD-10 | Bazel 远程构建/缓存配置（Remote Execution / disk cache） | 9 | P1 | 2 | 1 | CI 命中缓存时间 -50% |
| INFRA-BUILD-11 | Bazel 第三方依赖（`bzl` 体系，含 protobuf/abseil/gtest 等） | 7 | P0 | 2 | 0 | 5+ 主流依赖可拉取 |
| INFRA-BUILD-12 | CMake ↔ Bazel 等价性校验脚本（目标集合 / 编译选项 diff） | 2, 8 | P0 | 2 | 1 | CI 跑等价性，漂移即报错 |
| INFRA-BUILD-13 | Bazel `compile_commands.json` 提取（hedron CCD extractor） | 8 | P1 | 0.5 | 1 | clangd 跨两套系统都可用 |
| INFRA-BUILD-14 | Bazel 安装包（`pkg_tar` / `pkg_deb` rules） | 8 | P2 | 1.5 | 2 | release 页面有 .tar.gz（与 CMake 产物等价） |
| INFRA-BUILD-15 | `.bazelrc` 分层配置（默认 + `--config=cpu/gpu/aarch64/qnx/asan/tsan/ubsan/release/debug`，含 `try-import .bazelrc.local`） | 7 | P0 | 2 | 0 | 5+ `--config` 场景可用，无 wrap 脚本 |
| INFRA-BUILD-16 | `CMakePresets.json` 分层配置（与 bazel config 一一对应：default/gpu/aarch64/qnx/asan/...） | 2 | P0 | 2 | 0 | 同一组场景在 CMake 侧也能跑 |
| INFRA-BUILD-17 | `build.env` 环境变量加载机制（CC/CXX/SYSROOT/BAZEL_REMOTE_CACHE，由 bazelrc/preset 双向注入） | 15, 16 | P1 | 1.5 | 0 | 用户机器级覆盖机制可用 |
| INFRA-BUILD-18 | 构建入口守护（CI 强制扫描仓库内不得出现 `build*.sh` / `build_opt_*` 等 wrap 脚本入口） | 15, 16 | P0 | 1 | 0 | CI lint 检出 wrap 脚本即 fail |

### F-INFRA-CI · 持续集成

| ID | 描述 | 依赖 | 优先级 | 估算 | Phase | 验收 |
|---|---|---|---|---|---|---|
| INFRA-CI-1 | GitHub Actions workflow：push/PR 触发编译 + 测试 | INFRA-BUILD-1 | P0 | 1 | 0 | PR 必须过 CI 才能 merge |
| INFRA-CI-2 | clang-format + clang-tidy + cppcheck 检查 | 1 | P0 | 1 | 0 | 0 lint warning |
| INFRA-CI-3 | sanitizer 矩阵（ASan / UBSan / TSan） | 1 | P0 | 1.5 | 1 | 单测全跑通过 sanitizer |
| INFRA-CI-4 | 多架构 CI（x86_64 + aarch64 via QEMU） | 1 | P1 | 2 | 1 | aarch64 CI 绿 |
| INFRA-CI-5 | 覆盖率报告（lcov/llvm-cov）+ 上传 Codecov | 1 | P1 | 1 | 1 | README 显示覆盖率徽章 |
| INFRA-CI-6 | Release artifact 自动构建（tag 触发，CMake + Bazel 双产物） | INFRA-BUILD-6,14 | P2 | 2 | 2 | release 页面有 .tar.gz |
| INFRA-CI-7 | Bazel 远程缓存配置（GitHub Actions cache + 自建 buildbuddy） | INFRA-BUILD-10 | P1 | 2 | 1 | CI 命中率 > 60% |
| INFRA-CI-8 | 构建系统等价性强制门（CMake vs Bazel 目标集 diff） | INFRA-BUILD-12 | P0 | 1.5 | 1 | 漂移直接 fail CI |
| INFRA-CI-9 | CI 矩阵分摊策略（push 跑子集 / PR 跑全套 / nightly 跑完整矩阵） | 7, 8 | P1 | 1.5 | 1 | push < 10min，PR < 30min |
| INFRA-CI-10 | 文档双语 lint（`.zh.md` 与 `.en.md` 成对 + 切换链接 + frontmatter 一致） | INFRA-DOC-7 | P0 | 2 | 1 | CI 检出缺对/缺链接即 fail |
| INFRA-CI-11 | 注释语言 lint（C++/Python/Rust/Go/Node 禁止中文注释，除允许 block comment 例外，详见 [adr/0009](./adr/0009-doc-code-language.md)） | INFRA-CI-1 | P0 | 2 | 1 | CI 检出中文注释即 fail |
| INFRA-CI-12 | commitlint + PR title lint（Conventional Commits 强制，详见 [adr/0009](./adr/0009-doc-code-language.md)） | INFRA-CI-1 | P0 | 1.5 | 0 | commit 不符合规范 fail |

### F-INFRA-TEST · 测试框架

| ID | 描述 | 依赖 | 优先级 | 估算 | Phase | 验收 |
|---|---|---|---|---|---|---|
| INFRA-TEST-1 | GoogleTest 集成 + `tests/` 目录规范 | INFRA-BUILD-1 | P0 | 1 | 0 | `ctest` 跑通示例 |
| INFRA-TEST-2 | GoogleMock 集成（用于 transport mock） | 1 | P0 | 0.5 | 0 | 有 mock 示例 |
| INFRA-TEST-3 | 测试 fixtures / 公共工具（port、临时目录、计时器） | 1 | P0 | 1 | 0 | 至少 3 个 fixture 类 |
| INFRA-TEST-4 | 集成测试 runner（启动多进程、收集日志） | 1 | P1 | 2 | 1 | 跨进程测试可用 |
| INFRA-TEST-5 | 端到端 replay 测试框架（基于 cyber_recorder 风格） | 4 | P1 | 3 | 2 | 可回放 .record 验证输出 |

### F-INFRA-BENCH · 性能基准

| ID | 描述 | 依赖 | 优先级 | 估算 | Phase | 验收 |
|---|---|---|---|---|---|---|
| INFRA-BENCH-1 | GoogleBenchmark 集成 + `benchmarks/` 规范 | INFRA-BUILD-1 | P0 | 1 | 0 | 至少 1 个 baseline benchmark |
| INFRA-BENCH-2 | 延迟测量原语（P50/P99/P99.9 计算器） | 1 | P0 | 1 | 1 | 单测覆盖 |
| INFRA-BENCH-3 | 5 类典型链路基准（短/中/长/扇入/扇出） | L4-CORE-* | P0 | 3 | 1 | 5 个 benchmark 可重复跑 |
| INFRA-BENCH-4 | H2 验证脚本（codegen vs 手写对比） | L1-CG-*, 3 | P0 | 2 | 1 | 输出 P99 差距报告 |
| INFRA-BENCH-5 | 性能回归 CI（每个 PR 自动跑基准 + 阈值告警） | 4 | P1 | 2 | 2 | 性能回退 > 2% 阻止 merge |

### F-INFRA-DOC · 文档

| ID | 描述 | 依赖 | 优先级 | 估算 | Phase | 验收 |
|---|---|---|---|---|---|---|
| INFRA-DOC-1 | 现有 4 份 docs（README/00/01/02）+ 2 ADR | - | P0 | - | 0 | ✅ 已完成 |
| INFRA-DOC-2 | Doxygen 集成 + API 文档生成（英文） | INFRA-BUILD-1 | P1 | 2 | 1 | `make doc` 输出 HTML |
| INFRA-DOC-3 | ADR 模板 + 流程（每重大决策一份 ADR） | - | P1 | 0.5 | 0 | 至少 5 个 ADR（含 0001/0002） |
| INFRA-DOC-4 | 用户指南（quickstart + tutorial，**双语**） | L1-DSL-* | P1 | 4 | 2 | 5 个 runnable 例子（中英对齐） |
| INFRA-DOC-5 | 迁移指南（cyber → tianshu，**双语**） | L4 全部 | P1 | 3 | 2 | 至少 1 个真实模块迁移示例（中英对齐） |
| INFRA-DOC-6 | 现有文档双语化（README + docs/00-02 + ADR-0001~0008 + evaluation，详见 [adr/0009](./adr/0009-doc-code-language.md)） | - | P1 | 15 | 0-1 | 所有现有文档有 `.zh.md` + `.en.md` 两份，CI 校验成对 |
| INFRA-DOC-7 | 文档模板（双语 README / ADR / 用户指南，含语言切换链接 frontmatter） | - | P0 | 1.5 | 0 | 模板 review 通过 |
| INFRA-DOC-8 | API 文档自动化（Doxygen 英文输出，集成到 CI） | 2, INFRA-CI-1 | P1 | 2 | 1 | CI 自动生成 + 部署到 GitHub Pages |
| INFRA-DOC-9 | 术语表（中英对照：调度器=Scheduler、血缘=Lineage 等） | - | P1 | 1 | 1 | 至少 50 条术语 |

### F-INFRA-LOG · 日志系统（详见 [adr/0011](./adr/0011-logging.md)）

| ID | 描述 | 依赖 | 优先级 | 估算 | Phase | 验收 |
|---|---|---|---|---|---|---|
| INFRA-LOG-1 | Logger API（宏 + LogRecord + 编译期过滤） | - | P0 | 3 | 0-1 | 单测覆盖 + benchmark |
| INFRA-LOG-2 | LogRouter（单例 + 异步队列 + 速率限制） | 1 | P0 | 4 | 1 | 队列满按级别丢弃 |
| INFRA-LOG-3 | ConsoleSink（彩色 stderr） | 2 | P0 | 1 | 1 | 桌面开发可见彩色输出 |
| INFRA-LOG-4 | FileSink（滚动文件，按 size/time） | 2 | P0 | 2 | 1 | 文件滚动正常 |
| INFRA-LOG-5 | ShmRingBufferSink（与 lineage 集成） | 2, L4-TRANS-22 | P1 | 3 | 2 | 日志事件可入 lineage |
| INFRA-LOG-6 | SyslogSink（systemd journal） | 2 | P1 | 2 | 2 | vehicle profile 可用 |
| INFRA-LOG-7 | NetworkSink（UDP/TCP 远端推送） | 2 | P2 | 2 | 3 | 集成 Loki/Promtail 示例 |
| INFRA-LOG-8 | SerialSink（MCU 串口） | 2, INFRA-OSAL-4 | P2 | 2 | 3 | MCU profile 可用 |
| INFRA-LOG-9 | C ABI 暴露 + 多语言 SDK 适配 | 1, INFRA-API-2 | P1 | 3 | 2 | Python/Go SDK 可用 |
| INFRA-LOG-10 | profile 默认配置 + 用户文档 | 1-8 | P0 | 1 | 1 | 5 profile 阈值生效 |
| INFRA-LOG-11 | 日志聚合集成示例（Loki/Promtail） | 7 | P2 | 2 | 3 | 输出 markdown 教程 |

### F-INFRA-PARAM · 参数系统（7 关键点详见 [adr/0012](./adr/0012-parameters.md)）

| ID | 描述 | 依赖 | 优先级 | 估算 | Phase | 验收 |
|---|---|---|---|---|---|---|
| INFRA-PARAM-1 | `TIANSHU_PARAM` 宏 + 类型注册（编译期） | - | P0 | 3 | 0-1 | 宏展开正确 |
| INFRA-PARAM-2 | `tianshu::param<T>` 模板 API + RWLock | 1 | P0 | 3 | 1 | 5+ 类型可读写 |
| INFRA-PARAM-3 | CLI 解析（`--param name=value` 短形式） | INFRA-LOG-1, L4-MAIN-1 | P0 | 2 | 1 | ti launch 接受 CLI 参数 |
| INFRA-PARAM-4 | 环境变量解析（`TIANSHU_<NAME>`） | 2 | P0 | 1 | 1 | 环境变量覆盖默认 |
| INFRA-PARAM-5 | YAML 配置文件解析（自研极简或 header-only yaml-cpp） | INFRA-DEPS-1 | P0 | 4 | 1 | 5 级搜索路径生效 |
| INFRA-PARAM-6 | 文件路径解析（相对/绝对/~/展开，配置文件目录基准） | 5 | P0 | 1 | 1 | 相对路径基准正确 |
| INFRA-PARAM-7 | 类级/实例级 namespace 隔离（`PerceptionComponent.max` vs `instance.max`） | 2 | P1 | 2 | 2 | 类/实例优先级正确 |
| INFRA-PARAM-8 | 配置引用（`${var}` + 算术表达式 + import 多文件合并） | 5 | P1 | 3 | 2 | 循环引用检测 |
| INFRA-PARAM-9 | 导入/导出 YAML（含 source 信息） | 5 | P1 | 2 | 2 | `tianshu-ctl export-params` 可用 |
| INFRA-PARAM-10 | 热加载（文件 watch + API + RPC + SIGHUP） | 2, L4-CONSOLE-2 | P1 | 3 | 2 | 5 触发方式可用 |
| INFRA-PARAM-11 | 回调注册 + 线程安全 | 2 | P1 | 2 | 2 | 回调独立线程执行 |
| INFRA-PARAM-12 | C ABI 暴露 + 多语言 SDK 适配 | 2, INFRA-API-2 | P1 | 3 | 2 | Python/Go SDK 可用 |
| INFRA-PARAM-13 | profile 配置（搜索路径 + 热加载策略） | 5, INFRA-PROFILE-1 | P1 | 1 | 2 | 5 profile 阈值生效 |
| INFRA-PARAM-14 | 文档 + 示例（含迁移指南） | 1-13 | P1 | 2 | 2 | 双语文档齐全 |

**INFRA 工作量汇总**：约 157 点（31 周），P0 部分 ~65 点（13 周）。构建系统双轨 + 入口标准化 + 轻架构 + 多语言 API + 日志 + 参数系统是 INFRA 的核心成本项。

---

## L4 · 运行时层

完全自研的运行时基础设施。**这是工作量最大的一层**，需要 API 兼容 cyber（详见 ADR-0002）。

### F-L4-PRIM · 通用原语

无依赖的基础工具类，对应 cyber 的 `cyber/base/`。

| ID | 描述 | 依赖 | 优先级 | 估算 | Phase | 验收 |
|---|---|---|---|---|---|---|
| L4-PRIM-1 | `ObjectPool<T>`（对象池，无锁/带锁两版） | - | P0 | 2 | 1 | 单测 + sanitizer 通过 |
| L4-PRIM-2 | `CacheBuffer<T>`（环形缓冲，容量可配） | - | P0 | 1.5 | 1 | 单测覆盖满/空/并发 |
| L4-PRIM-3 | `AtomicHashMap<K,V>`（无锁哈希表） | - | P0 | 2 | 1 | 单测 + sanitizer 通过 |
| L4-PRIM-4 | `AtomicRWLock`（读写锁） | - | P0 | 1 | 1 | benchmark vs std::shared_mutex |
| L4-PRIM-5 | `SpinLock` + `TicketLock`（varied backoff） | - | P1 | 1 | 1 | benchmark 对比 |
| L4-PRIM-6 | `BlockingCounter` / `Notification`（同步原语） | - | P0 | 1 | 1 | 单测覆盖 |
| L4-PRIM-7 | `StringUtils` / `PathUtils` / `Time`（杂项工具） | - | P1 | 1 | 1 | 单测覆盖 |
| L4-PRIM-8 | `SignalHandler`（统一信号处理） | - | P2 | 1 | 2 | SIGINT/SIGTERM 优雅退出 |

### F-L4-CORO · 协程

用户态协程，对应 cyber 的 `cyber/croutine/`。

| ID | 描述 | 依赖 | 优先级 | 估算 | Phase | 验收 |
|---|---|---|---|---|---|---|
| L4-CORO-1 | `RoutineContext`（汇编 swap，x86_64 + aarch64） | - | ~~P0~~ | 4 | ~~1~~ | **跳过**（ADR-0019） |
| L4-CORO-2 | `RoutineContext` 起步替代：`ucontext` 版（Phase 1 用） | - | ~~P0~~ | 1 | ~~1~~ | **跳过**（ADR-0019） |
| L4-CORO-3 | `CRoutine`（协程抽象，state + context + fn） | 1 或 2 | ~~P0~~ | 2 | ~~1~~ | **跳过**（ADR-0019） |
| L4-CORO-4 | `RoutineFactory`（包装 component proc 为 CRoutine） | 3 | ~~P0~~ | 1 | ~~1~~ | **跳过**（ADR-0019） |
| L4-CORO-5 | 协程栈管理（固定栈 vs 共享栈，可配） | 1 | P2 | 2 | 2 | Phase 2 C++20 协程按需评估 |
| L4-CORO-6 | 协程级超时（`with_timeout`，强杀） | 3 | P2 | 2 | 3 | Phase 2 按需 |
| L4-CORO-7 | MCU profile 禁用协程（纯回调模式，profile 条件编译） | L4-CORO-3, INFRA-PROFILE-2 | ~~P1~~ | 1 | ~~2~~ | **不需要**（Phase 1 本来无协程，ADR-0019） |
| L4-CORO-8 | 极简协程变体（基于 setjmp/longjmp，embedded profile 可选） | L4-CORO-2 | P2 | 2 | 3 | Phase 3 按需 |

### F-L4-SCHED · 调度器

对应 cyber 的 `cyber/scheduler/`。

| ID | 描述 | 依赖 | 优先级 | 估算 | Phase | 验收 |
|---|---|---|---|---|---|---|
| L4-SCHED-1 | `Scheduler` 抽象基类（接口定义） | L4-CORO-3 | P0 | 1 | 1 | 接口锁定 |
| L4-SCHED-2 | `Processor`（worker 线程，跑 CRoutine 队列） | L4-CORO-3 | P0 | 2 | 1 | N 个 Processor 跑稳定 |
| L4-SCHED-3 | `SchedulerClassic` 策略（优先级队列 + 多对多） | 1, 2 | P0 | 3 | 1 | 单测 + benchmark |
| L4-SCHED-4 | `SchedulerChoreography` 策略（task→processor 固定映射） | 1, 2 | P1 | 3 | 2 | 单测 + cpuset 绑定验证 |
| L4-SCHED-5 | 调度策略配置 proto（`.conf` 格式，cyber 兼容） | 1 | P0 | 1 | 1 | `choreography_conf.pb.txt` 可加载 |
| L4-SCHED-6 | 优先级继承（避免优先级反转） | 3 | P1 | 2 | 2 | 单测覆盖反转场景 |
| L4-SCHED-7 | 协程级 watchdog（hung 检测） | 2 | P2 | 2 | 3 | 5s 未 Yield 触发告警 |
| L4-SCHED-8 | 动态策略切换（运行时改 `.conf` + 热重载） | 5 | P2 | 3 | 3 | 配置文件改动自动生效 |
| L4-SCHED-9 | 单核协作式调度器（embedded/mcu profile，无抢占） | L4-CORO-7, INFRA-PROFILE-2 | P1 | 2 | 2 | embedded profile 跑通 |
| L4-SCHED-10 | MCU 静态调度表（编译期固定，无运行时决策） | 9 | P2 | 2 | 3 | mcu profile 跑通 |

### F-L4-TRANS · Transport

**完全自研，不引用 cyber**。对应 cyber 的 `cyber/transport/`。最大头模块。

| ID | 描述 | 依赖 | 优先级 | 估算 | Phase | 验收 |
|---|---|---|---|---|---|---|
| L4-TRANS-1 | `Transport` 抽象（按 channel 创造 reader/writer 端） | - | P0 | 1 | 1 | 接口锁定 |
| L4-TRANS-2 | INTRA 后端（同进程内直接 callback） | L4-PRIM-6 | P0 | 1 | 1 | 零拷贝，单测覆盖 |
| L4-TRANS-3 | SHM 后端（同机跨进程共享内存） | L4-PRIM-1,2 | P0 | 4 | 1 | 跨进程消息吞吐 ≥ 1M msg/s |
| L4-TRANS-4 | SHM 通知机制（基于 eventfd / futex） | 3 | P0 | 2 | 1 | 单机通知延迟 < 1μs |
| L4-TRANS-5 | `MessageInfo`（每消息元数据：seq, ts, src, lineage ptr） | - | P0 | 1 | 1 | 字段固定，向后兼容 |
| L4-TRANS-6 | RTPS / 跨机后端（自研 UDP+SHM 混合 或 集成 zenoh） | 3 | P1 | 6 | 2 | 跨机消息延迟 < 1ms |
| L4-TRANS-7 | HYBRID 自动选路（同进程 INTRA / 同机 SHM / 跨机 RTPS） | 2,3,6 | P1 | 2 | 2 | 切换逻辑正确，单测覆盖 |
| L4-TRANS-8 | QoS 配置（depth/reliable/history，cyber 兼容） | 5 | P1 | 2 | 2 | `qos_profile.pb.txt` 可加载 |
| L4-TRANS-9 | 大消息分片传输（>1MB 自动分片，参考 cyber ShmSegment） | 3 | P2 | 3 | 3 | 10MB 消息可传输 |
| L4-TRANS-10 | RDMA 后端（NIC offload，可选） | 6 | P2 | 6 | 3 | 性能 vs SHM 对比报告 |
| L4-TRANS-11 | 嵌入式 transport（仅 INTRA，profile=embedded/mcu） | L4-TRANS-2 | P1 | 1 | 2 | embedded profile 跑通 |
| L4-TRANS-12 | MCU stream buffer backend（FreeRTOS stream buffer 适配 SHM 接口） | INFRA-OSAL-4 | P2 | 2 | 3 | Cortex-M 跑通 |
| L4-TRANS-13 | offset_ptr SHM allocator（Boost.Interprocess 风格，地址无关跨进程零拷贝，详见 [evaluation/0002 §6.2](./evaluation/0002-fork-shared-address-space.md) 推荐 B 方案） | L4-TRANS-3 | P1 | 8 | 2 | SHM 内对象跨进程可解引用 |
| L4-TRANS-14 | ForkSHM mode（仅 ADR-0010 决策落地后激活，待 [evaluation/0002](./evaluation/0002-fork-shared-address-space.md) 拍板）：daemon 实现 + ASLR 关闭 + binary hash 校验 | L4-TRANS-3 | P2 | 6 | 3 | vehicle profile 可选启用 |
| L4-TRANS-15 | ForkSHM mode：TIANSHU_MARK_CROSS_PROCESS_SAFE 静态检查 + 降级到 FlatBuffers | 14 | P2 | 4 | 3 | 检查不合规对象即降级 |
| L4-TRANS-16 | ForkSHM mode：安全缓解（seccomp + CFI + 只读 SHM，详见 [evaluation/0002 §4.9](./evaluation/0002-fork-shared-address-space.md)） | 14 | P2 | 4 | 3 | ASLR 关闭的安全补偿 |
| L4-TRANS-17 | ForkSHM mode：集成测试 + 性能 benchmark | 14,15,16 | P2 | 4 | 3 | 性能 vs offset_ptr 对比报告 |
| L4-TRANS-18 | `TransportBackend` 抽象接口 + Registry（详见 [adr/0010](./adr/0010-transport-shm-infra.md)） | - | P0 | 3 | 1 | 接口锁定，5+ backend 可注册 |
| L4-TRANS-19 | `HybridTransport` 自动选择（INTRA/SHM/Zenoh 智能切换） | 18, L4-SD-* | P0 | 3 | 1 | 同进程自动 INTRA，同机自动 SHM |
| L4-TRANS-20 | `IntraBackend` 完整实现（zero-copy 直接指针传递，所有消息格式支持） | 18, L4-CORE-10..13 | P0 | 3 | 1 | 同进程消息延迟 < 100ns |
| L4-TRANS-21 | INTRA 自动检测（service discovery 集成 process_id 比对） | 20, L4-SD-4 | P0 | 2 | 1 | reader/writer 同进程自动切 INTRA |
| L4-TRANS-22 | `tianshu::shm::ShmPool` 通用 allocator（4 池策略 + Stats，详见 [adr/0010](./adr/0010-transport-shm-infra.md)） | L4-PRIM-1 | P0 | 5 | 1 | 4 池命中，碎片率 < 10% |
| L4-TRANS-23 | `tianshu::shm::Allocator<T>` STL 适配（支持 `std::vector<T, ShmAllocator<T>>`） | 22 | P0 | 2 | 1 | STL 容器可在 SHM 内构造 |
| L4-TRANS-24 | `tianshu::shm::offset_ptr<T>` 自研（参考 Boost.Interprocess 精简版，~100 行，详见 [adr/0010](./adr/0010-transport-shm-infra.md) 与 [eval/0002](./evaluation/0002-fork-shared-address-space.md)） | 22 | P0 | 3 | 1 | 跨进程指针 < 1ns 额外开销 |
| L4-TRANS-25 | `tianshu::shm::vector<T>` / `string` SHM 容器（基于 ShmAllocator） | 23, 24 | P1 | 2 | 2 | 用户代码可直接用 |
| L4-TRANS-26 | SHM allocator 可观测性（容量 / 命中率 / 碎片率 / 池分布） | 22 | P1 | 2 | 2 | `tianshu-ctl inspect` 可看 |
| L4-TRANS-27 | SHM allocator profile 配置（vehicle/embedded/mcu 池容量与多池策略） | 22, INFRA-PROFILE-1 | P1 | 1 | 2 | 5 profile 阈值生效 |

### F-L4-CORE · Node / Reader / Writer

对应 cyber 的 `cyber/node/`、`cyber/message/`。

| ID | 描述 | 依赖 | 优先级 | 估算 | Phase | 验收 |
|---|---|---|---|---|---|---|
| L4-CORE-1 | `MessageTraits<T>`（消息类型 traits，序列化/类型名） | - | P0 | 1 | 1 | 内置类型 + Protobuf 适配 |
| L4-CORE-2 | `Writer<T>`（绑 channel + transport + write） | L4-TRANS-1,5 | P0 | 2 | 1 | Write 单测通过 |
| L4-CORE-3 | `Reader<T>`（绑 channel + buffer + observe） | L4-TRANS-1, L4-PRIM-2 | P0 | 2 | 1 | Observe/TryFetch 单测 |
| L4-CORE-4 | `Node`（factory：CreateReader/Writer/Service） | 2, 3 | P0 | 2 | 1 | API 与 cyber 等价 |
| L4-CORE-5 | `DataVisitor<M0..Mn>`（多输入数据访问器） | 3 | P0 | 3 | 1 | 4 输入以内单测通过 |
| L4-CORE-6 | `DataDispatcher`（单例，channel_id → buffers） | 3 | P0 | 2 | 1 | 多 reader 同 channel 单测 |
| L4-CORE-7 | `DataNotifier`（channel_id → notifier callbacks） | 3 | P0 | 1 | 1 | 单测覆盖 |
| L4-CORE-8 | `IntraReader`/`IntraWriter`（同进程内变种） | 2,3 | P1 | 1 | 1 | 性能 benchmark |
| L4-CORE-9 | `Service<Req,Resp>` / `Client<Req,Resp>`（请求-响应） | L4-TRANS-* | P1 | 3 | 2 | 单测覆盖 |
| L4-CORE-10 | `MessageConcept` C++20 concept + POD 自动特化（详见 [adr/0008](./adr/0008-message-format-multi.md)） | L4-CORE-1 | P0 | 2 | 1 | concept 锁定 + POD 单测 |
| L4-CORE-11 | FlatBuffers `MessageTraits` 特化 + `TIANSHU_TRAITS_FLATBUFFER` 注册宏 | 10 | P0 | 2 | 1 | FlatBuffer 消息收发 |
| L4-CORE-12 | Protobuf lite `MessageTraits` 特化 + `TIANSHU_TRAITS_PROTOBUF` 注册宏 | 10 | P0 | 2 | 1 | Protobuf 消息收发 |
| L4-CORE-13 | `MessageFactory`（按 channel_name 反序列化，跨机接收端用） | 11, 12 | P0 | 3 | 1 | 跨语言消息工厂可查表 |
| L4-CORE-14 | Pass 4 消息格式感知传输策略（POD/Flat/Proto → INTRA/SHM/跨机选择，编译期警告 POD 跨机） | L4-CORE-10, L1-PASS-6 | P1 | 3 | 2 | codegen 按格式生成正确传输代码 |
| L4-CORE-15 | 多格式跨语言消息一致性测试套（C++ ↔ Python，FlatBuffers + Protobuf 字段一致） | 11, 12, INFRA-API-8 | P1 | 4 | 2 | 跨语言消息 byte-level 一致 |

### F-L4-COMP · Component

对应 cyber 的 `cyber/component/` + `cyber/init.h`。

| ID | 描述 | 依赖 | 优先级 | 估算 | Phase | 验收 |
|---|---|---|---|---|---|---|
| L4-COMP-1 | `ComponentBase`（Init/Shutdown 抽象） | L4-CORE-4 | P0 | 1 | 1 | 接口锁定 |
| L4-COMP-2 | `Component<>` 5 偏特化（0/1/2/3/4 输入） | 1, L4-CORE-5 | P0 | 2 | 1 | 与 cyber API 等价 |
| L4-COMP-3 | `TimerComponent`（定时触发） | 1 | P0 | 1 | 1 | 单测覆盖 |
| L4-COMP-4 | `ComponentConfig` proto（cyber 兼容 + 扩展字段） | - | P0 | 1 | 1 | `component_conf.pb.txt` 可加载 |
| L4-COMP-5 | `DagConfig` proto（cyber 兼容 + 扩展字段） | 4 | P0 | 1 | 1 | `.dag` 文件可加载 |
| L4-COMP-6 | 数据融合 `AllLatest` 策略 | L4-CORE-5 | P0 | 1 | 1 | 与 cyber 等价 |
| L4-COMP-7 | 数据融合 `AlignNearest`（时间窗口对齐） | L4-CORE-5 | P1 | 2 | 2 | 单测覆盖 |
| L4-COMP-8 | 数据融合 `TimeWindow`（窗口聚合） | L4-CORE-5 | P1 | 3 | 2 | 单测覆盖 |
| L4-COMP-9 | 数据融合 `TriggerOnAny`（任一就绪触发） | L4-CORE-5 | P1 | 1 | 2 | 单测覆盖 |
| L4-COMP-10 | 用户算子注册（`REGISTER_COMPONENT` 宏） | 2 | P0 | 1 | 1 | hello component 可加载 |

### F-L4-MAIN · Mainboard 启动器

对应 cyber 的 `cyber/mainboard/`。

| ID | 描述 | 依赖 | 优先级 | 估算 | Phase | 验收 |
|---|---|---|---|---|---|---|
| L4-MAIN-1 | `ti-launch` CLI（`ti launch xxx.flow`，统一入口 `ti` 按 PATH 分发 `ti-*`，见 [adr/0002 术语边界](./adr/0002-cyber-relation.md)） | L4-COMP-* | P0 | 1.5 | 1 | 启动 hello component DAG |
| L4-MAIN-2 | `ModuleController`（动态加载 .so + 生命周期管理） | 1 | P0 | 3 | 1 | 多 component DAG 可加载 |
| L4-MAIN-3 | `tianshu::Init(argc, argv)`（全局初始化） | - | P0 | 1 | 1 | 与 cyber 等价 |
| L4-MAIN-4 | 信号处理 + 优雅退出 | 2 | P0 | 1 | 1 | SIGINT 后所有 component Shutdown |
| L4-MAIN-5 | `--remap` 全局通道映射（cyber 风格） | 2 | P1 | 2 | 2 | `-r old:/new` 生效 |
| L4-MAIN-6 | `--flow-register`（注册 traceable flow） | 2 | P0 | 1 | 1 | flow 函数可被编译器发现 |
| L4-MAIN-7 | 加载器内嵌编译器（启动时 trace+codegen） | L1-PASS-* | P0 | 3 | 1 | flow → .so 自动链路打通 |

### F-L4-SD · Service Discovery

对应 cyber 的 `cyber/service_discovery/`。

| ID | 描述 | 依赖 | 优先级 | 估算 | Phase | 验收 |
|---|---|---|---|---|---|---|
| L4-SD-1 | `ServiceDiscovery` 抽象（channel/reader/writer 上线通知） | - | P0 | 1 | 1 | 接口锁定 |
| L4-SD-2 | 同机 SD（基于 SHM + 抽屉文件） | 1, L4-TRANS-3 | P0 | 3 | 1 | 多进程 SD 收敛正确 |
| L4-SD-3 | 跨机 SD（基于多播或自研协议） | 1, L4-TRANS-6 | P1 | 3 | 2 | 多机 SD 收敛正确 |
| L4-SD-4 | SD 事件 callback（subscriber 上线触发） | 1 | P0 | 1 | 1 | 单测覆盖 |
| L4-SD-5 | Topology 计算图导出（运行时全局图） | 4 | P2 | 2 | 3 | `tianshu-ctl inspect` 可看图 |
| L4-SD-6 | 静态拓扑配置（embedded/mcu profile，编译期固定，无运行时发现） | L4-SD-1, INFRA-PROFILE-2 | P1 | 2 | 2 | mcu profile 跑通 |

### F-L4-BP · 反压传播

专利核心创新点 3（痛点 3）。

| ID | 描述 | 依赖 | 优先级 | 估算 | Phase | 验收 |
|---|---|---|---|---|---|---|
| L4-BP-1 | `Watermark` 数据结构（high_wm / low_wm） | L4-PRIM-* | P0 | 1 | 2 | 单测覆盖 |
| L4-BP-2 | 反压事件（消费者堆积超 high_wm → 上游回调） | 1 | P0 | 2 | 2 | 单测覆盖 |
| L4-BP-3 | 反压传播链（跨算子级联） | 2 | P1 | 2 | 2 | 多跳链路单测 |
| L4-BP-4 | 上游传感器降频信号（peripheral 层） | 2 | P2 | 3 | 3 | USB 摄像头可降帧 |
| L4-BP-5 | 反压事件入 lineage（可追溯） | 2, L2-LIN-* | P1 | 1 | 2 | lineage 含反压标记 |

### F-L4-CONSOLE · 控制台（Console，独立进程访问任意节点对象，详见 [evaluation/0003](./evaluation/0003-console.md)）

> ⚠️ 所有功能点均**非 P0**，归属 Phase 3（低优先级，详见 [evaluation/0003](./evaluation/0003-console.md)）。Phase 0/1/2 不实现。

| ID | 描述 | 依赖 | 优先级 | 估算 | Phase | 验收 |
|---|---|---|---|---|---|---|
| L4-CONSOLE-1 | ConsoleService Protobuf 定义 + 协议文档（list/inspect/query/set/inject/replay/subscribe） | - | P2 | 2 | 3 | 协议 review 通过 |
| L4-CONSOLE-2 | 加载进程端 ConsoleServer（RPC + Pub/Sub，基于 unix socket） | L4-TRANS-20, 1 | P2 | 6 | 3 | 客户端可连接并发命令 |
| L4-CONSOLE-3 | 加载进程端 Inspect/Query API（暴露 Node/Reader/Writer/State 状态） | L4-CORE-*, L2-LIN-4, 2 | P2 | 5 | 3 | 控制台可查任意对象 |
| L4-CONSOLE-4 | SHM mirror 旁路（按需开启，控制台订阅大数据预览零拷贝） | L4-TRANS-22, 2 | P2 | 4 | 3 | 控制台订阅 channel 零拷贝预览 |
| L4-CONSOLE-5 | Console Client SDK（C ABI + C++ 封装，详见 [adr/0007](./adr/0007-api-spec-multi-language.md)） | 1 | P2 | 4 | 3 | C++/Python 客户端可用 |
| L4-CONSOLE-6 | TUI（ftxui，命令行交互式控制台） | 5 | P2 | 6 | 3 | TUI 可 list/inspect/inject |
| L4-CONSOLE-7 | Web UI（HTTP/WS + React 前端，可选） | 5 | P3 | 10 | 3 | 浏览器可访问 |
| L4-CONSOLE-8 | 安全模型（unix socket peer cred / token，分级权限） | 2 | P2 | 2 | 3 | 只读 / 写 / 危险 三级权限 |
| L4-CONSOLE-9 | Python/Rust/Go/Node SDK 适配（基于 C ABI） | INFRA-API-8..11, 5 | P3 | 4 | 3 | 4 语言客户端可用 |
| L4-CONSOLE-10 | 集成测试 + 示例场景（多加载进程联合调试） | 1-9 | P2 | 3 | 3 | 真实场景跑通 |

**L4-CONSOLE 工作量汇总**：46 点，全部 P2/P3，Phase 3。

### F-L4-GPU · GPU 资源管理（设计就绪，实现按 profile 渐进，详见 [adr/0006](./adr/0006-gpu-acceleration.md)）

> ⚠️ 本框架所有功能点均**非 P0**，归属 Phase 2/3。Phase 1 PoC 不实现 GPU。

| ID | 描述 | 依赖 | 优先级 | 估算 | Phase | 验收 |
|---|---|---|---|---|---|---|
| L4-GPU-1 | `Device` 抽象（CUDA / OpenCL / NPU adapter，HAL backend 多态） | INFRA-HAL-6 | P1 | 3 | 2 | ORIN/J5 GPU 可枚举 |
| L4-GPU-2 | GPU 内存池（pinned memory + device slab pool + free-list） | 1 | P1 | 5 | 2 | 大张量分配 < 1ms；命中率 > 80% |
| L4-GPU-3 | GPU Stream 管理（编译期静态分配 + 运行时 fallback） | 1, L1-PASS-6 | P1 | 3 | 2 | stream 内并发可观测 |
| L4-GPU-4 | CPU-GPU 数据零拷贝（pinned DMA + 可选 unified memory + IPC handle） | 2 | P1 | 4 | 2 | 跨进程 GPU 张量传递 < 10μs |
| L4-GPU-5 | GPU 事件接入 DataNotifier（cudaEventRecord + 完成回调） | L4-CORE-7, 3 | P1 | 3 | 2 | 算子输出依赖 GPU event 而非 wall-clock |
| L4-GPU-6 | GPU 故障检测（OOM / Xid 错误 / 超时 / 热降频） | 1, L2-FT-5 | P1 | 4 | 2 | 5 类故障单测覆盖 |
| L4-GPU-7 | 跨进程 GPU 共享（CUDA MPS daemon 管理 + MIG 硬隔离选项） | 2, L4-SD-2 | P1 | 5 | 3 | ORIN 多加载进程共享 GPU 可跑 |
| L4-GPU-8 | OpenCL backend（嵌入式 GPU / non-CUDA 加速器） | 1 | P2 | 4 | 3 | 至少 1 种 OpenCL 设备跑通 |
| L4-GPU-9 | GPU 利用率/显存监控（runtime 指标，入 lineage + 监控 dashboard） | L4-GPU-2,6 | P1 | 2 | 2 | 指标可读 |
| L4-GPU-10 | GPU 内存池观测 API（容量/命中率/碎片率） | 2 | P2 | 1 | 3 | `tianshu-ctl inspect` 可看 |

**L4 工作量汇总**：约 190 点（38 周），P0 部分 ~80 点（16 周）。**这是 Phase 1 + Phase 2 的工作重点**。嵌入式/MCU 变体（TRANS-11/12、CORO-7/8、SCHED-9/10、SD-6）合计 ~12 点，集中在 Phase 2-3。

---

## L1 · DSL + Trace + Compiler

把命令式 API 升级为声明式 + 加载期编译。**专利核心新颖性所在**。

### F-L1-DSL · Fluent Builder DSL

对外暴露的声明式 API，对应 ADR-0001 选定的方案。

| ID | 描述 | 依赖 | 优先级 | 估算 | Phase | 验收 |
|---|---|---|---|---|---|---|
| L1-DSL-1 | `FlowBuilder` API 骨架（`Node& node` 入参 + register 宏） | L4-CORE-4 | P0 | 2 | 1 | hello flow 可注册 |
| L1-DSL-2 | `node.reader<T>(channel)` 流式构造 | L4-CORE-3 | P0 | 1 | 1 | 与 cyber `CreateReader` 等价语义 |
| L1-DSL-3 | `node.writer<T>(channel)` 流式构造 | L4-CORE-2 | P0 | 1 | 1 | 与 cyber `CreateWriter` 等价语义 |
| L1-DSL-4 | `node.on_input({readers...}, fn)` 注册回调 | 2, L4-CORE-5 | P0 | 2 | 1 | 1-4 输入可写 |
| L1-DSL-5 | `.with_sla(SLA{...})` 链式约束 | L3-SLA-1 | P0 | 1.5 | 1 | SLA 元数据 attach 到 flow |
| L1-DSL-6 | `.with_fallback("flow_lite")` 降级路径声明 | 1 | P1 | 1 | 2 | 单测覆盖降级触发 |
| L1-DSL-7 | `.with_fault_tolerance({strategy, replay_window_ms})` | 1, L2-FT-* | P1 | 2 | 2 | 3 种策略可配 |
| L1-DSL-8 | `.with_backpressure({high_wm, low_wm})` | 1, L4-BP-* | P1 | 1 | 2 | 反压可配 |
| L1-DSL-9 | `.with_state<T>({checkpoint_interval, max_history, recovery})` | 1 | P1 | 3 | 2 | 有状态算子可写 |
| L1-DSL-10 | `node.on_batch<N>({readers}, fn)`（批式触发） | 4 | P2 | 2 | 3 | N=2/4/8 单测 |
| L1-DSL-11 | `node.on_window<N, STEP>({readers}, fn)`（窗口触发） | 4 | P2 | 3 | 3 | 滑动/滚动窗口单测 |
| L1-DSL-12 | `Pipeline` / `Stage` 高级抽象（多个 flow 串联） | 4 | P2 | 3 | 3 | 5 stage pipeline 可跑 |
| L1-DSL-13 | `REGISTER_TRACEABLE_FLOW(name, fn)` 宏 | 1 | P0 | 1 | 1 | 注册表全局可见 |
| L1-DSL-14 | DSL 用户手册 + API 参考 | 1-13 | P1 | 3 | 2 | Doxygen 输出 + 5 例子 |
| L1-DSL-15 | `with_gpu_backend<Backend>` 算子后端声明（CUDA/OpenCL/NPU，详见 [adr/0006](./adr/0006-gpu-acceleration.md)） | 1, L4-GPU-1 | P1 | 2 | 2 | DSL 可声明 GPU 后端 |
| L1-DSL-16 | `with_gpu_memory({op_bytes...})` 显存预算声明 | 15 | P1 | 1.5 | 2 | 编译期可校验 |
| L1-DSL-17 | `with_stream_policy(DEDICATED/SHARED)` stream 分配策略 | 15, L4-GPU-3 | P1 | 1 | 2 | 单测覆盖 |

### F-L1-TRACE · Trace 引擎

对应 ADR-0001 的 trace 路线。**这是 H1 假设的载体**。

| ID | 描述 | 依赖 | 优先级 | 估算 | Phase | 验收 |
|---|---|---|---|---|---|---|
| L1-TRACE-1 | `TraceContext`（trace 会话上下文，含 reader/writer/routine 记录表） | - | P0 | 1.5 | 1 | 单测覆盖 |
| L1-TRACE-2 | `Reader<T>` / `Writer<T>` 的 RAII guard（trace 模式下记录） | L4-CORE-2,3 | P0 | 3 | 1 | trace 时所有 reader/writer 被记录 |
| L1-TRACE-3 | `CreateRoutine` 的 RAII guard（trace 时记录 callback 签名） | L4-CORO-4 | P0 | 2 | 1 | 所有 routine 被记录 |
| L1-TRACE-4 | trace 模式标志（`Node::is_tracing()`，gate 所有副作用） | L4-CORE-4 | P0 | 1 | 1 | trace 时不执行真实 callback |
| L1-TRACE-5 | Trace AST 数据结构（reader 节点 / writer 节点 / routine 节点 / 边） | 1 | P0 | 2 | 1 | 序列化/反序列化通过 |
| L1-TRACE-6 | trace 失败回退（escape hatch：命令式执行） | 4 | P0 | 2 | 1 | 失败时不阻塞运行 |
| L1-TRACE-7 | trace 覆盖率检查器（对比手写 DAG 是否漏记录） | 5 | P0 | 2 | 1 | H1 验证脚本基础 |
| L1-TRACE-8 | 跨线程 trace（thread-local + 合并） | 1 | P1 | 3 | 2 | 多线程 flow 可 trace |
| L1-TRACE-9 | 嵌套 trace（一个 flow 内调用其他 flow） | 8 | P2 | 2 | 3 | 单测覆盖 |
| L1-TRACE-10 | 确定性 trace（去除 `__TIME__`/指针地址/随机数，X1 用） | 1 | P1 | 2 | 3 | 同输入两次 trace AST 一致 |

### F-L1-PASS · 六阶段编译器 Pass 链

对应 00-overview.md §4 Layer 1 的六阶段编译。

| ID | 描述 | 依赖 | 优先级 | 估算 | Phase | 验收 |
|---|---|---|---|---|---|---|
| L1-PASS-1 | Pass 基类 + Pass 管理器（顺序执行 + 中间产物传递） | L1-TRACE-5 | P0 | 2 | 1 | 6 个 pass 可注册执行 |
| L1-PASS-2 | **Pass 0 · Trace**（执行 flow 函数，输出 Trace AST） | L1-TRACE-5 | P0 | 1 | 1 | 与 L1-TRACE-5 联调通过 |
| L1-PASS-3 | **Pass 1 · Analysis**（AST → 全局数据流图，含 SLA 标注） | 2 | P0 | 3 | 1 | 输出有向图 + 元数据 |
| L1-PASS-4 | **Pass 2 · 逻辑优化**（死通道消除、常量传播、冗余 reader 合并） | 3 | P1 | 3 | 2 | 优化前后图对比单测 |
| L1-PASS-5 | **Pass 3 · 算子融合**（多算子合入单 CRoutine） | 3 | P0 | 5 | 2 | 融合规则 ≥ 5 种，benchmark 提速 |
| L1-PASS-6 | **Pass 4 · SLA 物理规划（RTA）** | 3, L3-* | P0 | 4 | 2 | 输出 cpuset/priority/queue_size |
| L1-PASS-7 | **Pass 5 · Codegen**（C++ 源码 + `.dag` + `.conf`） | L1-CG-* | P0 | 2 | 1 | 输出可直接编译 |
| L1-PASS-8 | **Pass 6 · Compile**（编译 .so 或加载预编译） | 7, L4-MAIN-2 | P0 | 2 | 1 | `.so` 可被 ti launch 加载 |
| L1-PASS-9 | Pass 性能 profiling（每 pass 耗时统计） | 1 | P2 | 1 | 2 | 加载期总耗时报告 |
| L1-PASS-10 | Pass 失败诊断（哪个 pass 失败 + 原因 + 建议） | 1 | P1 | 2 | 2 | 错误信息可读 |

### F-L1-CG · C++ Codegen

代码生成器，**H2 假设的载体**。

| ID | 描述 | 依赖 | 优先级 | 估算 | Phase | 验收 |
|---|---|---|---|---|---|---|
| L1-CG-1 | Codegen 框架（模板引擎 + 缩进管理 + 命名空间） | - | P0 | 2 | 1 | 可生成 hello Component |
| L1-CG-2 | `Component<M0>` 子类代码生成（Proc 方法 + Init 方法） | 1 | P0 | 3 | 1 | 与手写二进制对比一致 |
| L1-CG-3 | `.dag` 文件生成（cyber 兼容格式 + 扩展字段） | 1 | P0 | 1 | 1 | ti launch 可加载 |
| L1-CG-4 | `.conf` 文件生成（含 cpuset/priority/queue_size） | 1 | P0 | 1 | 1 | scheduler 可加载 |
| L1-CG-5 | 融合算子 codegen（多算子单 CRoutine 内联） | 2, L1-PASS-5 | P1 | 3 | 2 | 融合后函数体单测 |
| L1-CG-6 | 状态 checkpoint 代码生成（有状态算子） | 2 | P2 | 3 | 3 | checkpoint 文件可读写 |
| L1-CG-7 | 容错包装代码生成（包一层 try/catch + replay） | 2, L2-FT-* | P1 | 3 | 2 | 异常自动触发容错 |
| L1-CG-8 | 反压注入代码生成（writer 端插入水线检查） | 2, L4-BP-* | P1 | 2 | 2 | 反压事件可触发 |
| L1-CG-9 | 血缘注入代码生成（writer 端插入元数据写入） | 2, L2-LIN-* | P1 | 2 | 2 | lineage 字段非空 |
| L1-CG-10 | codegen 输出格式化 + clang-format 自动跑 | 1 | P1 | 0.5 | 1 | 输出代码符合代码规范 |
| L1-CG-11 | codegen 测试 golden 文件（每改一次 pass，对比 golden） | 1-9 | P0 | 2 | 1 | CI 跑 golden diff |
| L1-CG-12 | GPU codegen 注入（stream 绑定 + GPU 内存池分配 + IPC handle 发布，详见 [adr/0006](./adr/0006-gpu-acceleration.md)） | L4-GPU-*, L1-DSL-15..17 | P1 | 4 | 2 | GPU 算子 codegen 可编译运行 |
| L1-CG-13 | codegen 按消息格式生成传输代码（POD: memcpy；FlatBuffers: 零拷贝；Protobuf: serialize，详见 [adr/0008](./adr/0008-message-format-multi.md)） | L4-CORE-10..15, L1-DSL-* | P1 | 2 | 2 | 三种格式 codegen 单测通过 |

**L1 工作量汇总**：约 110 点（22 周），P0 部分 ~50 点（10 周）。

---

## L2 · 血缘与容错

专利核心创新点 8（痛点 8）。**所有功能都横切 L4**。

### F-L2-LIN · 帧级零拷贝血缘

| ID | 描述 | 依赖 | 优先级 | 估算 | Phase | 验收 |
|---|---|---|---|---|---|---|
| L2-LIN-1 | `LineageEntry` 数据结构（src_id, ts, upstream_ptrs[], state_version） | - | P0 | 1.5 | 2 | 字段固定，序列化稳定 |
| L2-LIN-2 | `LineageRecorder`（每消息写入 entry，零拷贝指针） | 1, L4-TRANS-5 | P0 | 3 | 2 | 记录开销 < 100ns/msg（目标 50ns） |
| L2-LIN-3 | `LineageBuffer`（环形，按 channel 维度） | 1, L4-PRIM-2 | P0 | 2 | 2 | 满负载不丢 entry |
| L2-LIN-4 | `LineageQuery`（从输出反查全部上游 + 状态） | 1, 3 | P1 | 3 | 2 | 单测覆盖多跳反查 |
| L2-LIN-5 | 跨进程 lineage 聚合（一个加载进程多 process） | 3, L4-SD-2 | P1 | 3 | 2 | 跨进程反查正确 |
| L2-LIN-6 | lineage 落盘（可选，按时间窗口或大小） | 3 | P2 | 2 | 3 | 离线分析可用 |
| L2-LIN-7 | lineage 导出格式（JSON / SQLite / Parquet） | 4 | P2 | 2 | 3 | 至少 1 种格式可用 |
| L2-LIN-8 | GPU 张量血缘（cudaIpcMemHandle + gpu_device_id + gpu_stream_id + gpu_event_ts，详见 [adr/0006](./adr/0006-gpu-acceleration.md)） | 1, L4-GPU-4 | P1 | 2 | 2 | 跨进程 GPU 张量可追溯 |

### F-L2-RB · Ring Buffer（血缘专用）

| ID | 描述 | 依赖 | 优先级 | 估算 | Phase | 验收 |
|---|---|---|---|---|---|---|
| L2-RB-1 | 多优先级 ring buffer（保留关键帧 + 滚动旧帧） | L4-PRIM-2 | P0 | 2 | 2 | 容量策略可配 |
| L2-RB-2 | 持久化后备（大消息落 SHM 文件，不进内存） | 1 | P2 | 3 | 3 | 100MB ring buffer 可用 |
| L2-RB-3 | ring buffer 压缩（同帧多版本合并） | 1 | P2 | 2 | 3 | 压缩比可观测 |

### F-L2-FT · 分级容错

| ID | 描述 | 依赖 | 优先级 | 估算 | Phase | 验收 |
|---|---|---|---|---|---|---|
| L2-FT-1 | `FaultToleranceStrategy` 抽象（接口 + factory） | L2-LIN-1 | P0 | 1 | 2 | 接口锁定 |
| L2-FT-2 | `SYNC_REPLAY` 策略（同步重放上一帧） | 1, L2-RB-1 | P0 | 2 | 2 | 恢复时间 < 1ms |
| L2-FT-3 | `ASYNC_REPLAY` 策略（后台重放 + 当帧用上次） | 1, L2-RB-1 | P1 | 2 | 2 | 不阻塞关键路径 |
| L2-FT-4 | `DEGRADE` 策略（切到 fallback flow） | 1, L1-DSL-6 | P1 | 2 | 2 | 切换 < 1 帧 |
| L2-FT-5 | 容错决策器（按超时 / 错误 / 资源 触发） | 2,3,4 | P1 | 3 | 2 | 5 类触发条件单测 |
| L2-FT-6 | 容错事件入 lineage（可追溯） | 5, L2-LIN-2 | P1 | 1 | 2 | lineage 含容错标记 |
| L2-FT-7 | 自动恢复（持续 N 帧正常后回到正常 flow） | 5 | P2 | 2 | 3 | 单测覆盖 |

**L2 工作量汇总**：约 50 点（10 周），P0 部分 ~14 点（3 周）。

---

## L3 · SLA 调度

专利核心创新点 2（痛点 2）。**H3 假设的载体**。

### F-L3-SLA · SLA 数据模型

| ID | 描述 | 依赖 | 优先级 | 估算 | Phase | 验收 |
|---|---|---|---|---|---|---|
| L3-SLA-1 | `SLA` 数据结构（deadline_ms, priority, jitter_ms, importance） | - | P0 | 1 | 1 | 字段固定 |
| L3-SLA-2 | `FlowSLA`（顶层 flow 的端到端 SLA） | 1 | P0 | 0.5 | 1 | 可绑定到 flow |
| L3-SLA-3 | `OperatorSLA`（每个算子的局部 SLA，编译期派生） | 1 | P0 | 1 | 1 | 可绑定到算子 |
| L3-SLA-4 | SLA 序列化（写 `.conf` 字段 + 加载） | 1 | P0 | 1 | 1 | 往返一致 |

### F-L3-RTA · Response Time Analysis

| ID | 描述 | 依赖 | 优先级 | 估算 | Phase | 验收 |
|---|---|---|---|---|---|---|
| L3-RTA-1 | WCET 估算器（静态分析 + 历史数据校准） | - | P0 | 3 | 1 | 对 5 类算子给出 WCET |
| L3-RTA-2 | 经典 RTA 算法（单核优先级抢占） | 1 | P0 | 2 | 1 | 与文献公式一致 |
| L3-RTA-3 | 多核 RTA 扩展（含跨核干扰） | 2 | P1 | 3 | 2 | 单测覆盖 |
| L3-RTA-4 | GPU/加速器 RTA（粗粒度估算） | 1 | P2 | 3 | 3 | 单测覆盖 |
| L3-RTA-5 | RTA 校准器（实测 P99.9 数据反馈估算） | 1, L2-LIN-* | P1 | 3 | 2 | 估算精度 [P99.9, P99.9×1.3] |
| L3-RTA-6 | RTA 报告生成（可读的验证文档） | 2,3 | P1 | 2 | 2 | 加载期输出报告 |

### F-L3-DERIVE · 调度参数自动推导

| ID | 描述 | 依赖 | 优先级 | 估算 | Phase | 验收 |
|---|---|---|---|---|---|---|
| L3-DERIVE-1 | cpuset 自动分配（关键路径绑大核 + 非关键绑小核） | L3-RTA-3 | P0 | 3 | 2 | ORIN 12 核分配合理 |
| L3-DERIVE-2 | priority 自动推导（按 SLA 重要性排序） | L3-RTA-2 | P0 | 1 | 2 | 单测覆盖 |
| L3-DERIVE-3 | `pending_queue_size` 自动推导（按 WCET/period） | L3-RTA-1 | P0 | 2 | 2 | 公式与 cyber 实践一致 |
| L3-DERIVE-4 | 反压水线自动推导（按 buffer + SLA） | L3-DERIVE-3 | P1 | 1.5 | 2 | 单测覆盖 |
| L3-DERIVE-5 | 跨加载进程全局资源视图（避免 cpuset 冲突） | L4-SD-2 | P1 | 3 | 2 | 多进程冲突检测准确 |

### F-L3-DETECT · SLA 违反检测

| ID | 描述 | 依赖 | 优先级 | 估算 | Phase | 验收 |
|---|---|---|---|---|---|---|
| L3-DETECT-1 | 加载期可满足性检查（不可满足即 abort） | L3-RTA-2 | P0 | 2 | 2 | 错误信息含 3 条建议 |
| L3-DETECT-2 | 运行时 SLA 监控（实际 WCET > 估算告警） | L2-LIN-2 | P1 | 2 | 2 | 告警入 lineage |
| L3-DETECT-3 | 运行时 SLA 违反触发降级（自动切 fallback） | L2-FT-4 | P2 | 2 | 3 | 单测覆盖 |
| L3-DETECT-4 | SLA violation 报告（含诊断 + 建议改配） | 1,2 | P1 | 2 | 2 | 输出 markdown 报告 |

### F-L3-GPU · GPU SLA 调度（设计就绪，实现按 profile 渐进，详见 [adr/0006](./adr/0006-gpu-acceleration.md)）

> ⚠️ 本框架所有功能点均**非 P0**，归属 Phase 2/3。

| ID | 描述 | 依赖 | 优先级 | 估算 | Phase | 验收 |
|---|---|---|---|---|---|---|
| L3-GPU-1 | GPU WCET 估算（按 batch/分辨率/利用率插值） | L4-GPU-2,9, L3-RTA-1 | P1 | 4 | 2 | 5 类算子 WCET 误差 < 30% |
| L3-GPU-2 | GPU 参与 RTA（CPU WCET + GPU WCET + 传输 = 总 WCET） | 1, L3-RTA-2 | P1 | 3 | 2 | CPU+GPU 联合可调度性可验证 |
| L3-GPU-3 | GPU 任务亲和（多 GPU 绑定 + 单 GPU stream 隔离） | L4-GPU-3,7, L3-DERIVE-1 | P1 | 3 | 3 | 多加载进程 GPU 无干扰 |
| L3-GPU-4 | 显存预算编译期校验（同驻留算子显存 ≤ profile 预算） | L4-GPU-2, L1-DSL-GPU | P1 | 3 | 2 | 超额加载期 abort |
| L3-GPU-5 | GPU OOM 自动降级（切 fallback CPU flow） | L4-GPU-6, L2-FT-4 | P1 | 2 | 2 | OOM 后 < 1 帧切换 |
| L3-GPU-6 | GPU 热降频感知（读 thermal → 切时变 WCET 表） | INFRA-HAL-2, L3-RTA-5 | P2 | 3 | 3 | 高温下 SLA 自动调整 |

**L3 工作量汇总**：约 50 点（10 周），P0 部分 ~15 点（3 周）。

---

## X · 横切确定性

专利核心创新点 9（痛点 9）。**三层确定性叠加，构成 ISO 26262 ASIL-D 追溯链**。

### F-X-BD · 构建确定性（Build Determinism）

| ID | 描述 | 依赖 | 优先级 | 估算 | Phase | 验收 |
|---|---|---|---|---|---|---|
| X-BD-1 | 确定性 trace（同输入两次 trace AST 字节一致） | L1-TRACE-10 | P0 | 2 | 3 | hash 校验通过 |
| X-BD-2 | codegen 输出确定性（同 AST → 同 .cc 字节一致） | L1-CG-1 | P0 | 2 | 3 | hash 校验通过 |
| X-BD-3 | 编译选项固定化（禁 `__TIME__`/`__COUNTER__`/`std::chrono::system_clock`） | - | P0 | 1.5 | 3 | 静态扫描器跑通 |
| X-BD-4 | 构建清单（manifest: 源 hash + 编译选项 + 依赖 hash） | 1,2,3 | P0 | 3 | 3 | `tianshu.build.manifest.json` 产出 |
| X-BD-5 | 二进制可重现构建（同 manifest → 同 .so 字节） | 4, INFRA-CI-* | P0 | 4 | 3 | 跨机两次构建 hash 一致 |
| X-BD-6 | 构建签名（GPG / Sigstore 签 manifest） | 4 | P2 | 2 | 3 | 签名链可验证 |

### F-X-ED · 执行确定性（Execution Determinism）

| ID | 描述 | 依赖 | 优先级 | 估算 | Phase | 验收 |
|---|---|---|---|---|---|---|
| X-ED-1 | 纯函数状态转移约束（编译期 lint） | L1-DSL-9 | P0 | 3 | 3 | 至少 5 类反模式被检出 |
| X-ED-2 | 状态版本绑定 lineage（每条消息带 state_version） | L2-LIN-1, L1-DSL-9 | P0 | 2 | 3 | 输出可追溯状态版本 |
| X-ED-3 | 确定性调度（去除时间戳依赖，按事件序触发） | L4-SCHED-* | P1 | 3 | 3 | 同输入两次执行结果一致 |
| X-ED-4 | 确定性执行测试框架（replay 输入 → diff 输出） | L2-LIN-4 | P1 | 3 | 3 | 5 个真实模块跑通 |
| X-ED-5 | 浮点数确定性（FTZ/DAZ 配置 + SIMD 选项） | - | P2 | 2 | 3 | x86_64 + aarch64 数值一致 |

### F-X-LT · 血缘可追溯（Lineage Traceability）

L2 已覆盖血缘核心；这里只列**跨层集成**功能点。

| ID | 描述 | 依赖 | 优先级 | 估算 | Phase | 验收 |
|---|---|---|---|---|---|---|
| X-LT-1 | 全局 lineage 索引（跨算子/跨进程/跨机） | L2-LIN-5 | P1 | 3 | 3 | 全栈反查 < 100ms |
| X-LT-2 | 回归测试框架（旧数据跑新代码 → diff） | L2-LIN-4 | P1 | 3 | 3 | 至少 3 个模块回归通过 |
| X-LT-3 | 事故复现（输入回放 + 状态回放 → 决策复现） | 1, X-ED-4 | P2 | 4 | 3 | 给定 .record 文件可复现输出 |

**X 工作量汇总**：约 50 点（10 周），全部 P0 ~18 点（4 周），**全部在 Phase 3**。

---

## T · 工具链

上层 CLI 与可视化工具。

### F-T-CTL · tianshu-ctl 命令行

| ID | 描述 | 依赖 | 优先级 | 估算 | Phase | 验收 |
|---|---|---|---|---|---|---|
| T-CTL-1 | CLI 框架（CLI11 / cxxopts，子命令组织） | - | P0 | 1 | 1 | `tianshu-ctl --help` 可用 |
| T-CTL-2 | `trace <flow_name>` 子命令（独立 trace，输出 AST） | 1, L1-TRACE-5 | P0 | 2 | 1 | 输出 JSON 格式 AST |
| T-CTL-3 | `compile <flow_name>` 子命令（独立编译，输出 .so + .dag + .conf） | 2, L1-PASS-* | P0 | 2 | 1 | 输出可直接 ti launch 加载 |
| T-CTL-4 | `inspect <dag>` 子命令（查看运行时拓扑 + SLA 报告） | L4-SD-5, L3-DETECT-4 | P1 | 3 | 2 | 输出 markdown 报告 |
| T-CTL-5 | `replay <record>` 子命令（回放 + 状态重建） | X-LT-3 | P1 | 4 | 3 | 可复现输出 |
| T-CTL-6 | `verify <sla>` 子命令（RTA 报告 + 建议） | L3-RTA-6 | P1 | 2 | 2 | 加载期报告对齐 |
| T-CTL-7 | `bench <flow>` 子命令（性能基准 + 对比手写） | INFRA-BENCH-4 | P2 | 2 | 2 | 输出 P99 差距报告 |
| T-CTL-8 | `doctor` 子命令（环境诊断 + 依赖检查） | - | P2 | 1.5 | 2 | 输出环境健康清单 |

### F-T-VIZ · 可视化（Web UI）

| ID | 描述 | 依赖 | 优先级 | 估算 | Phase | 验收 |
|---|---|---|---|---|---|---|
| T-VIZ-1 | DAG 可视化（trace AST → mermaid/SVG） | L1-TRACE-5 | P1 | 2 | 2 | flow 一键看图 |
| T-VIZ-2 | lineage 可视化（点输出 → 高亮上游） | L2-LIN-4 | P2 | 4 | 3 | Web UI 可交互 |
| T-VIZ-3 | SLA dashboard（运行时 WCET vs deadline 实时图） | L3-DETECT-2 | P2 | 4 | 3 | 实时刷新 < 1s |
| T-VIZ-4 | 反压/容错事件流（timeline 视图） | L4-BP-5, L2-FT-6 | P2 | 3 | 3 | 事件流可筛选 |

**T 工作量汇总**：约 35 点（7 周），P0 ~5 点（1 周），其余多为 P1/P2。

---

## 全局汇总

### 总工作量

| 架构层 | 总点数 | P0 点数 | 重点 Phase |
|---|---|---|---|
| INFRA | 224 | 75 | 0-3 |
| L4 | 327 | 100 | 1-3 |
| L1 | 131 | 53 | 1-2 |
| L2 | 52 | 14 | 2 |
| L3 | 64 | 15 | 2 |
| X | 50 | 18 | 3 |
| T | 35 | 5 | 1-3 |
| **合计** | **883 点** | **280 点** | **约 177 周（一人 3.4 年）** |

按 1 点 = 1 理想人日（不含调试/回归/会议）折算，**实际工期约 1.5-2 倍**，即 1 人 3-4 年，或 3 人 1-1.5 年。

### 优先级矩阵（P0 必做功能点清单）

按依赖顺序排列：

| 序 | ID | 描述 | 估算 | Phase |
|---|---|---|---|---|
| 1 | INFRA-BUILD-1 | CMake 骨架 | 1 | 0 |
| 2 | INFRA-BUILD-7 | Bazel workspace 骨架 | 1.5 | 0 |
| 3 | INFRA-BUILD-2 | CMake 多模块组织 | 1 | 0 |
| 4 | INFRA-BUILD-8 | Bazel 多模块组织 | 2 | 0 |
| 5 | INFRA-BUILD-3 | C++20 + 编译器要求 | 0.5 | 0 |
| 6 | INFRA-BUILD-11 | Bazel bzl 依赖 | 2 | 0 |
| 7 | INFRA-CI-1 | GitHub Actions workflow | 1 | 0 |
| 8 | INFRA-CI-2 | lint 检查 | 1 | 0 |
| 9 | INFRA-TEST-1/2/3 | 测试框架 | 2.5 | 0 |
| 10 | INFRA-BENCH-1 | GoogleBenchmark 集成 | 1 | 0 |
| 11 | INFRA-DOC-3 | ADR 模板 | 0.5 | 0 |
| 12 | L4-PRIM-1..6 | 通用原语 | 9.5 | 1 |
| 13 | L4-CORO-1..4 | 协程（ucontext 起步 + CRoutine） | 8 | 1 |
| 14 | L4-SCHED-1..3,5 | Scheduler + Classic 策略 | 7 | 1 |
| 15 | L4-TRANS-1..5 | Transport（INTRA + SHM + MessageInfo） | 9 | 1 |
| 16 | L4-CORE-1..7 | Node/Reader/Writer + DataVisitor/Dispatcher/Notifier | 13 | 1 |
| 17 | L4-COMP-1..6,10 | Component 基础 + AllLatest | 8 | 1 |
| 18 | L4-MAIN-1..4,6,7 | 加载器（ti launch）+ ModuleController + Init | 10.5 | 1 |
| 19 | L4-SD-1,2,4 | ServiceDiscovery（同机） | 7 | 1 |
| 20 | L1-DSL-1..5,13 | DSL fluent builder 核心 | 8.5 | 1 |
| 21 | L1-TRACE-1..7 | Trace 引擎 + 覆盖率 + escape hatch | 13.5 | 1 |
| 22 | L1-PASS-1,2,3,7,8 | Pass 链 + Trace/Analysis/Codegen/Compile | 10 | 1 |
| 23 | L1-CG-1..4,10,11 | Codegen 基础 + golden 测试 | 10.5 | 1 |
| 24 | T-CTL-1,2,3 | tianshu-ctl CLI + trace/compile 子命令 | 5 | 1 |
| 25 | INFRA-BENCH-2,3,4 | H2 验证基准 | 6 | 1 |
| 26 | INFRA-BUILD-9,12 | Bazel 工具链 + 等价性校验 | 4 | 1 |
| 27 | INFRA-CI-3,7,8 | sanitizer + 远程缓存 + 等价性门 | 5 | 1 |

**P0 总计**：约 240 点（约 48 理想周，1 人 ~11 个月；按 1.5 倍实际系数 → ~17 个月）。

### Phase 映射

| Phase | 主要功能点（粗） | 工作量 |
|---|---|---|
| **Phase 0（2-3 周）** | INFRA-BUILD-1..4,7..11,15,16,18 · INFRA-DEPS-1,2 · INFRA-PROFILE-1,2 · INFRA-API-1..4 · INFRA-DOC-3,7 · INFRA-CI-1,2,12 · INFRA-TEST-1..3 · INFRA-BENCH-1 · INFRA-DOC-1 | ~55 点 |
| **Phase 1 PoC（8-12 周）** | L4-PRIM/SCHED/TRANS/CORE/COMP/MAIN/SD 的 P0 子集（含 CORE-10..13 多消息格式）· INFRA-OSAL-1,2 · INFRA-HAL-1,2 · INFRA-DEPS-3 · INFRA-PROFILE-3 · INFRA-API-5,7 · INFRA-CI-10,11 · INFRA-DOC-6,9（现有文档双语化）· L1 全套 P0 · T-CTL 基础 · H1/H2/H3 验证 | ~155 点 |
| **Phase 2 MVP（3-6 月）** | L4 完善部分（Choreography、HYBRID、跨机、SD 跨机、嵌入式变体 TRANS-11/CORO-7/SCHED-9/SD-6）· L1-PASS-4,5,6（融合 + RTA）· L2 全套 · L3 全套 · L4-COMP-7..9（融合策略）· L4-BP 全套 · T-CTL-4,6,7 · INFRA-DOC-4,5 · INFRA-OSAL-3 · INFRA-DEPS-4 | ~200 点 |
| **Phase 3 认证 + 嵌入式（6-12 月）** | X 全套（构建/执行/追溯确定性）· L1-DSL-10,11,12（批流/窗口/Pipeline）· L4-TRANS-9,10,12 · L4-CORO-5,6,8 · L4-SCHED-7,8,10 · INFRA-OSAL-4,5,6 · INFRA-HAL-4,5,6 · T-CTL-5,8 · T-VIZ 全套 | ~210 点 |

### 并行化建议

| 可并行集合 | 前置条件 | 团队规模建议 |
|---|---|---|
| INFRA + L4-PRIM | Phase 0 完成 | 1 人 |
| L4-CORO + L4-TRANS | L4-PRIM 完成 | 2 人（互不依赖） |
| L4-CORE + L4-COMP | L4-TRANS 完成 | 1 人（强耦合） |
| L4-SD + L4-BP | L4-CORE 完成 | 1-2 人 |
| L1-DSL + L1-TRACE + L1-CG | L4 完成，强相关 | 1 人主导 |
| L1-PASS | L1-DSL + L1-TRACE 完成 | 1 人 |
| L2 + L3 | L4 完成，可并行 L1 | 2 人 |
| X | L1-L4 完成 | 1 人 |
| T | 各层 API 稳定后 | 1 人（持续） |

**最优团队配置**：
- **1 人**：串行推进，约 2 年到 Phase 2，3 年到 Phase 3
- **2 人**：1 人 L4/L2/L3，1 人 L1/X/T，约 1 年到 Phase 2，2 年到 Phase 3
- **3 人**：再加 1 人 INFRA/CI/工具链，约 8 个月到 Phase 2，1.5 年到 Phase 3

### 关键路径（Critical Path）

```
INFRA-BUILD-* → L4-PRIM → L4-TRANS → L4-CORE → L1-DSL → L1-TRACE → L1-PASS → H1 验证
                                                                          ↓
                                                                       L1-CG → H2 验证
                                                                                  ↓
                                                                              L3-RTA → H3 验证
                                                                                          ↓
                                                                                       M1 PoC 通过
```

**Phase 1 关键路径长度**：约 30 点（6 周理想，10 周实际），任何一个环节卡住整个 PoC 延期。

### 风险与缓解

| 风险 | 影响 | 缓解 |
|---|---|---|
| L4-TRANS SHM 实现复杂度超预期 | 阻塞 L4-CORE | Phase 1 起步用 INTRA + 进程内 SHM 替代，跨进程 SHM 延后 |
| 双构建系统维护漂移 | CI 不稳定 | INFRA-BUILD-12 等价性门 + 每周 reviewer 检查 |
| L1-TRACE 覆盖率不足（H1 失败） | 专利新颖性失效 | Phase 1 优先验证，escape hatch 兜底 |
| L1-CG 性能落差 > 1%（H2 失败） | 专利新颖性失效 | Phase 1 优先验证，pass 调优兜底 |
| L3-RTA 估算过保守（H3 失败） | SLA 不可满足 | profile-guided 校准 + 经验库 |
| 一人开发，关键路径不可压缩 | Phase 2 延期 | 严格优先级：M2 只做 perception mainboard |

---

## 后续 ADR 待写队列

> 编号规则（2026-08-27 定）：ADR 编号是**纯时间序流水号**，动笔那一刻才分配，不预占位。
> 主题检索由 [adr/README.md](./adr/README.md) 索引承担（域标签），编号不携带类别语义。
> 历史预占位 0020-0025 已废除；0020-0022 已分配（monitor 反射 / DSL v0 / lineage v0，2026-08-27）。

| 主题 | 触发时机 |
|---|---|
| codegen 输出形式（字符串拼接 vs jinja2 模板 vs LLVM IR） | L1-CG-1 启动前 |
| 状态 checkpoint 格式（自定义 vs FlatBuffers vs SQLite） | L1-DSL-9 启动前 |
| 推理引擎 adapter（TensorRT vs ONNX Runtime vs Triton） | L4-GPU-1 启动前 |
| 国产 NPU adapter（华为昇腾 vs 地平线 BPU vs 寒武纪） | INFRA-HAL-7 启动前（按需） |
| 多语言 SDK 绑定技术 v1（pybind11 / cxx / cgo / napi-rs 具体方案） | INFRA-API-8 启动前 |
| 第三方依赖白名单 v1（首批 10-20 条具体库：Zenoh / Protobuf / FlatBuffers / nlohmann/json / yaml-cpp / toml++ 等） | Phase 0 内定 |
