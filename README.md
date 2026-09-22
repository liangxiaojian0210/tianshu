# 天枢 · TIANSHU

<p align="center">
  <img src="branding/assets/tianshu-geometric-color.svg" alt="TIANSHU" width="160"/>
</p>

> 声明式实时数据流框架：写 flow 声明数据依赖与算子语义，
> 加载期完成 SLA 验证与编译——**编译产物对标手写代码：额外延迟是加性常数（每跳 2ns 级，不随算子工作量与消息大小放大）；算子工作量足够时 P99 差异 <1%（适用域显式，[ADR-0034](docs/adr/0034-gate-semantics-v3.md)），逐消息可溯源**。

[![status](https://img.shields.io/badge/status-Phase%201%20PoC-yellow)](docs/01-roadmap.md)
[![language](https://img.shields.io/badge/language-C%2B%2B20-blue)]()
[![build](https://img.shields.io/badge/build-CMake%20%2B%20Bazel-blue)](docs/adr/0003-build-system.md)
[![profiles](https://img.shields.io/badge/profiles-5%20%28desktop%20%7C%20server%20%7C%20vehicle%20%7C%20embedded%20%7C%20mcu%29-green)](docs/adr/0005-lightweight-multiplatform.md)
[![license](https://img.shields.io/badge/license-Apache--2.0-blue)](docs/adr/0017-license.md)

---

## 这是什么

**天枢 (TIANSHU)** 是一个实时数据流框架，目标域是自动驾驶车端 ECU。

它要解决的矛盾：现有中间件（Cyber RT / ROS 2 / DDS）性能足够，但开发范式是命令式的——开发者要手写每个 Component、手填 `.dag` / `.conf`，跨算子优化、SLA 验证、容错重放都做不到。

天枢把开发范式升级为**声明式数据流 + 加载期编译**：开发者写 flow 函数描述数据依赖与算子语义，框架在加载期 trace 并编译为原生 DAG。验收门（Phase 1 H2，[ADR-0034](docs/adr/0034-gate-semantics-v3.md) 两层语义）：**主承诺**——编译产物相对同语义手写装配的额外延迟是加性的（`diff ≤ F + k×跳数 + m×扇出支数`，k ≤ 2ns/跳量级），声明层不按工作量抽成、不随消息大小放大；**从承诺**——每消息算子工作量 W ≥ (F + k×hops)/1% 时，P99 差 <1% 成立。

## 一句话

```cpp
tianshu::dsl::FlowBuilder b("perception");

auto cam = b.source<CameraMsg>("camera", std::chrono::milliseconds(33), camera_emit)
              .map<DetectMsg>(detect_op)
              .with_wcet(std::chrono::milliseconds(8));
auto lidar = b.source<CloudMsg>("lidar", std::chrono::milliseconds(10), lidar_emit);

b.join<DetectMsg, CloudMsg, FusedMsg>(cam, lidar, fuse_op)
    .with_wcet(std::chrono::milliseconds(3))
    .map<PredictMsg>(predict_op)
    .with_wcet(std::chrono::milliseconds(4))
    .sink(publish_out)
    // 端到端 deadline：加载期验证（链路预算 + 饱和度准入），
    // 不可满足直接拒载并给出违规路径——不进运行时抖动（ADR-0029）
    .with_sla(tianshu::sla::Sla{.deadline = std::chrono::milliseconds(50)});

const auto flow = b.build();  // 逐消息血缘自动携带；ADR-0030 落地后
                              // 同一张图编译为原生 .so，性能对标手写代码
```

`ti launch <flow名>` 加载时：按名 dry-run trace 出全局数据流图 → 六阶段编译（ADR-0030）→ 生成 `.so` + `.dag` + `.conf` → 加载运行，**运行时直接执行编译产物，不经解释器**（M-D 已接线；`ti compile` 提供离线预编译，H2 判决见 [ADR-0034](docs/adr/0034-gate-semantics-v3.md)）。

## 核心特性

| 特性 | 说明 | 设计文档 |
|---|---|---|
| **声明式 DSL** | flow 链式声明（source/map/join/op/stateful/span/from），写清数据依赖与算子语义，加载期成图 | [ADR-0021](./docs/adr/0021-dsl-v0.md) · [0024](./docs/adr/0024-dsl-op-primitive.md) · [0025](./docs/adr/0025-from-component-reference.md) |
| **SLA 编译** | 端到端 deadline **加载期验证**：链路预算 + 饱和度准入 + 预算下行分摊，不可满足拒载（附违规路径与削减建议） | [ADR-0029](./docs/adr/0029-sla-compilation.md) |
| **逐消息血缘** | 每条消息可溯源至根（实测 desktop-release：建根 21ns、单跳链 51ns/消息、移动 0.2ns）；丢帧自动重放/降级 | [ADR-0022](./docs/adr/0022-lineage-v0.md) · [0026](./docs/adr/0026-slice-input-model.md) |
| **状态即数据 + 故障恢复** | 版本化状态通道 + 恢复协议，进程重启后输出 **EXACT MATCH** 一致 | [ADR-0027](./docs/adr/0027-state-as-data-channel-taxonomy.md) |
| **记录与回放** | record v2：血缘入库、分块压缩（LZ4/ZSTD）、分片合并；离线回放输出与在线逐字节一致 | [ADR-0028](./docs/adr/0028-record-format-v1.md) |
| **加载期编译（H2 判决 PASS）** | 六阶段管线：trace → 分析 → 优化 → SLA 规划 → 源码 codegen → `.so` 装载；验收门为加性开销常数（k ≤ 2ns/跳级）+ 负载条件化 <1%（[ADR-0034](./docs/adr/0034-gate-semantics-v3.md)）；`ti launch <flow名>` / `ti compile` 已接线（ADR-0030 M-B/M-D） | [ADR-0030](./docs/adr/0030-l1-compiler.md) · [0034](./docs/adr/0034-gate-semantics-v3.md) |

> 设计就绪、按 Phase 渐进：GPU 加速与调度（[ADR-0006](./docs/adr/0006-gpu-acceleration.md)）· 多语言 SDK（[ADR-0007](./docs/adr/0007-api-spec-multi-language.md)）· 控制台（[ADR-0014](./docs/adr/0014-console.md)）。
> 工程基线：双构建零警告 CI（[ADR-0003](./docs/adr/0003-build-system.md)/[0004](./docs/adr/0004-build-entry.md)）· 轻架构 5 profile，desktop 已验证（[ADR-0005](./docs/adr/0005-lightweight-multiplatform.md)）。

## 与同类系统的关系

| 系统 | 关系 |
|---|---|
| **Cyber RT** | API 兼容，代码完全独立（不 fork、不 link、不引用） |
| **Spark / Flink** | 范式借鉴（声明式 + 加载期编译），不重合（域不同、约束不同） |
| **ROS 2** | 同域竞品，但 ROS 2 仍是命令式 |
| **JAX / torch.compile** | trace + codegen 路线的灵感来源 |

详见 [docs/00-overview.md §6](./docs/00-overview.md)。

## 项目状态

| 阶段 | 状态 |
|---|---|
| Phase 0：奠基期（仓库 / 文档 / CI） | ✅ 完成（2026-08） |
| Phase 1：PoC（验证三个核心假设） | 🟡 进行中——三假设全绿（均 2026-09-18）：H1（逐字节等价）✅、H2（产物对标手写）✅、H3（WCET 校准预测力，[ADR-0038](docs/adr/0038-h3-verification-semantics.md)）✅；收尾项：PoC demo 视频与 benchmark 数据公开 |
| Phase 2：MVP（替换 Apollo perception mainboard） | ⏳ |
| Phase 3：认证就绪（ISO 26262 ASIL-D） | ⏳ |

详见 [docs/01-roadmap.md](./docs/01-roadmap.md)。

## 文档索引

### 入口

- [docs/README.md](./docs/README.md) — 文档导览（三条阅读路径）
- [docs/00-overview.md](./docs/00-overview.md) — 一句话说清楚 + 四层架构 + 三层确定性
- [docs/01-roadmap.md](./docs/01-roadmap.md) — Phase 0/1/2/3 路线图 + 里程碑 + 风险登记
- [docs/06-benchmarks.md](./docs/06-benchmarks.md) — 基准与三假设验证数据（H1/H2/H3 判定数字、证据与复现入口）
- [demo/](./demo/) — PoC 演示：`bash demo/poc-demo.sh` 六拍走完声明 → trace → SLA → 编译 → 运行 → 血缘 → 校准（含 asciinema 录像）

### 活文档（as-built 架构与模块设计，每次迭代同步）

- [docs/arch/README.md](./docs/arch/README.md) — 架构文档导航 + 模块状态表 + **维护契约**
- [docs/arch/01-architecture.md](./docs/arch/01-architecture.md) — 总体架构：分层 / 模块地图 / 消息生命周期
- [docs/arch/modules/](./docs/arch/modules/) — 每模块一页：职责 / API / 内部设计 / 决策史 / 测试入口

### 架构决策记录（ADR）

- [docs/adr/0001-dsl-form.md](./docs/adr/0001-dsl-form.md) — DSL 形式选型：fluent builder + auto trace
- [docs/adr/0002-cyber-relation.md](./docs/adr/0002-cyber-relation.md) — 与 Cyber RT 的关系：完全重写，API 兼容
- [docs/adr/0003-build-system.md](./docs/adr/0003-build-system.md) — 构建系统双轨：CMake + Bazel
- [docs/adr/0004-build-entry.md](./docs/adr/0004-build-entry.md) — 构建入口标准化：原生 bazel/cmake，禁 wrap，配置文件覆盖
- [docs/adr/0005-lightweight-multiplatform.md](./docs/adr/0005-lightweight-multiplatform.md) — 轻架构 + 多端多平台（5 profile + 依赖治理 + OSAL/HAL）
- [docs/adr/0006-gpu-acceleration.md](./docs/adr/0006-gpu-acceleration.md) — GPU 加速与调度管理（设计就绪，实现按 profile 渐进）
- [docs/adr/0007-api-spec-multi-language.md](./docs/adr/0007-api-spec-multi-language.md) — 接口规范与多语言支持（C ABI + Python/Rust/Go/Node SDK）
- [docs/adr/0008-message-format-multi.md](./docs/adr/0008-message-format-multi.md) — 消息格式多支持（FlatBuffers / Protobuf / 自定义 struct）
- [docs/adr/0009-doc-code-language.md](./docs/adr/0009-doc-code-language.md) — 文档双语 + 代码注释/commit message 强制英文
- [docs/evaluation/0001-cross-machine-transport.md](./docs/evaluation/0001-cross-machine-transport.md) — 跨机通信选型评估（自研 vs Zenoh / CycloneDDS / Fast-DDS / MQTT / gRPC，**待用户拍板**）
- [docs/adr/0010-transport-shm-infra.md](./docs/adr/0010-transport-shm-infra.md) — Transport Backend 抽象 + 通用 SHM Allocator + INTRA 模式 + offset_ptr（通用基础设施）
- [docs/adr/0011-logging.md](./docs/adr/0011-logging.md) — 日志规范（结构化 / 异步 / 多 sink / 多语言 SDK）
- [docs/adr/0012-parameters.md](./docs/adr/0012-parameters.md) — 参数系统（统一声明访问 / 4 路配置优先级 / 引用 / 导入导出 / 热加载）
- [docs/adr/0013-cross-machine-transport.md](./docs/adr/0013-cross-machine-transport.md) — 跨机 transport（自研 SHM + Zenoh，含接口抽象 + MCU Zenoh-pico）
- [docs/adr/0014-console.md](./docs/adr/0014-console.md) — 控制台（全平台 + 跨机 + TUI+Web，设计完整，实现 Phase 3）
- [docs/adr/0015-discovery-abstraction.md](./docs/adr/0015-discovery-abstraction.md) — 服务发现抽象（DiscoveryBackend 接口，可替换实现）
- [docs/adr/0016-config-format.md](./docs/adr/0016-config-format.md) — 配置格式选型（TOML 主推 + YAML 兼容 + JSON 导入导出）
- [docs/adr/0017-license.md](./docs/adr/0017-license.md) — 许可证 Apache-2.0
- [docs/adr/0018-cpp-style-guide.md](./docs/adr/0018-cpp-style-guide.md) — C++ 风格指南（Google 基础 + clang-format/tidy 强制）
- [docs/adr/0019-coroutine-strategy.md](./docs/adr/0019-coroutine-strategy.md) — 协程策略（Phase 1 回调调度 / Phase 2 C++20 stackless）
- [docs/evaluation/0003-console.md](./docs/evaluation/0003-console.md) — 控制台方案评估（独立进程访问任意节点对象，**待用户拍板**，Phase 3）
- [docs/adr/0020-message-reflection-monitor.md](./docs/adr/0020-message-reflection-monitor.md) — 消息反射与 ti-monitor（schema sidecar v1）
- [docs/adr/0021-dsl-v0.md](./docs/adr/0021-dsl-v0.md) — DSL v0：链式声明 + 单线程级联语义
- [docs/adr/0022-lineage-v0.md](./docs/adr/0022-lineage-v0.md) — 血缘 v0：逐消息元数据溯源
- [docs/adr/0023-kauto-transport-v0.md](./docs/adr/0023-kauto-transport-v0.md) — kAuto 双发传输 v0
- [docs/adr/0024-dsl-op-primitive.md](./docs/adr/0024-dsl-op-primitive.md) — box/op 原语（读写下结点的 DSL 投影）
- [docs/adr/0025-from-component-reference.md](./docs/adr/0025-from-component-reference.md) — from() 组件引用
- [docs/adr/0026-slice-input-model.md](./docs/adr/0026-slice-input-model.md) — 切片输入模型（触发对齐的区间血缘）
- [docs/adr/0027-state-as-data-channel-taxonomy.md](./docs/adr/0027-state-as-data-channel-taxonomy.md) — 状态即通道 + 通道分类法 + 恢复协议
- [docs/adr/0028-record-format-v1.md](./docs/adr/0028-record-format-v1.md) — Record 格式 v2（血缘入库 + 分块压缩）
- [docs/adr/0029-sla-compilation.md](./docs/adr/0029-sla-compilation.md) — SLA 编译 v0（加载期 deadline 验证 + 预算分配）
- [docs/adr/0030-l1-compiler.md](./docs/adr/0030-l1-compiler.md) — L1 编译器（六阶段管线 + 源码 codegen 设计）

## 命名与品牌

**天枢**：北斗七星之首（Dubhe / α Ursae Majoris）。

- 枢 = 枢纽 = middleware：中间件的语义命中
- 七星拓扑天然契合 DAG 数据流图

Logo 双方向（branding/assets/）：

- `tianshu-starchart-*.svg` — 古星图风格（夜空 + 鎏金 + 七星连线）
- `tianshu-geometric-*.svg` — 极简几何（线条 + 节点 + DAG 拓扑）

## 维护者

Pride Leong · 2026

## 许可证

**Apache License 2.0**（详见 [LICENSE](./LICENSE) 与 [docs/adr/0017-license.md](./docs/adr/0017-license.md)）。专利相关权利在开源许可范围内，详见后续 LICENSE 文件。
