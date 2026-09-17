# 天枢 (TIANSHU) — 方案总览

> **文档定位**：工程视角的项目方案（叙事文档）。
> **维护者**：Pride Leong
> **状态**：v0.2（2026-09-09）· Phase 0 完成，Phase 1 进行中 · 现状细节见 [arch/](./arch/README.md)（活文档）

---

## 1. 一句话定位

**天枢 (TIANSHU)** 是一种 **SLA 约束的声明式实时数据流编译框架**。开发者写声明式数据流，框架在加载期通过 trace + 静态 C++ codegen 编译为原生 DAG；验收目标为编译产物相对同语义手写装配的额外延迟是**加性常数**（每跳 2ns 级，不随算子工作量与消息大小放大），算子工作量 W ≥ 常数/1% 时 P99 差 <1%（Phase 1 H2，[ADR-0034](./adr/0034-gate-semantics-v3.md) 两层语义）。

定位类比：**MapReduce → Spark** 的范式跃迁，迁移到自动驾驶车端 ECU。

---

## 2. 解决的核心矛盾

Cyber RT、ROS 2、DDS 这一档中间件的性能已经够用（协程调度、共享内存、QoS 自动协商），但开发范式仍是**命令式**：开发者手写每个 Component / Node、手填 `.dag` / `.conf` / launch 文件。这种范式下：

- 拓扑是运行时通过 channel 字符串匹配隐式形成的，框架看不到全局
- 跨算子优化做不到（融合、消除死通道、关键路径分析）
- SLA 配置靠人肉试错，配错只在运行时偶发超时才暴露
- 丢帧、慢帧、降级没有统一机制，每个模块各写各的

天枢的目标是**保留中间件底层的性能原语**（共享内存、协程、QoS），**换掉上层的开发范式**（命令式 → 声明式 + 加载期编译）。

### 目标平台（5 档 profile，详见 [adr/0005](./adr/0005-lightweight-multiplatform.md)）

| Profile | 典型硬件 | 典型 OS | 二进制上限 |
|---|---|---|---|
| `desktop` | x86_64 开发机 | Linux / macOS | 不限 |
| `server` | 云服务器 x86_64 | Linux | 不限 |
| `vehicle` | ORIN / J5 / MDC / ADL | Linux / QNX | < 50MB |
| `embedded` | ARM Cortex-A53/55/72 | Embedded Linux | < 10MB |
| `mcu` | ARM Cortex-M7/M33、RISC-V | FreeRTOS / Zephyr / bare-metal | < 1MB |

天枢坚持**轻架构 + 最小依赖**：每个第三方依赖必须经过 ADR 审批；5 个 profile 通过编译期裁剪覆盖（详见 [02-development-plan F-INFRA-DEPS](./02-development-plan.md#f-infra-deps--依赖治理)）。

---

## 3. 与 Cyber RT 的关系（决策已定，详见 [adr/0002-cyber-relation.md](./adr/0002-cyber-relation.md)）

**完全重写，API 兼容**。

- 天枢**完全独立**：不引用 cyber 任何代码（不 `#include` cyber header、不 link cyber `.so`、不把 cyber 当后端 adapter）
- **所有层从零实现**：transport（INTRA / SHM / 跨进程 / 跨机）、协程（CRoutine）、调度器、reader / writer / node / component 全部自研
- **API 风格兼容 cyber**：公开 API 的命名和签名参考 cyber（`Node::CreateReader<T>(channel)`、`Writer<T>::Write(msg)`、`Component<M0, M1>`、`Reader<T>::Observe()` 等），目标是用户从 cyber 迁移到天枢时**源代码改动最小化**（理想情况只改 `#include` 和 namespace）
- **运行时完全是天枢代码**，不掺 cyber 二进制

这样带来的工程权衡：

| 维度 | 影响 |
|---|---|
| 工作量 | 比 fork / adapter 方案大（重写 transport + 协程 + 调度器是核心成本） |
| 上游同步 | 完全没有 Apollo 主干合并成本 |
| 许可证 | 可独立以宽松许可（Apache 2.0 / MIT）开源，零 Apollo 许可证风险 |
| 测试 / CI | 完全自控 |
| 用户迁移成本 | 仅 API 兼容不足以保证源码零改动，但能控制在"修改 namespace + 个别签名调整"量级 |
| 风险 | 重新实现 transport 是大工程；性能必须对标 cyber 现状（共享内存、HYBRID 自动选路等机制要重做） |

---

## 4. 四层架构 + 横切三层确定性

```
┌────────────────────────────────────────────────────────────┐
│ 高级抽象：AD Stack 适配层（感知/预测/规划即插即用）            │
├────────────────────────────────────────────────────────────┤
│ Pipeline / Stage（流式 / 批式 / 窗口 / 有状态 混编）          │
├────────────────────────────────────────────────────────────┤
│ fluent builder DSL                                          │
│   on_input / on_batch / on_window +                         │
│   with_sla / with_state / with_fusion / with_fallback       │
├────────────────────────────────────────────────────────────┤
│ 六阶段 DAG 编译器（加载期）                                  │
│   trace → 分析 → 逻辑优化 → SLA 物理规划(RTA) → codegen → .so│
├────────────────────────────────────────────────────────────┤
│ 运行时（天枢自研，API 兼容 cyber）                           │
│   CRoutine + 血缘 + 反压 + SLA 调度 + 状态 checkpoint        │
├────────────────────────────────────────────────────────────┤
│ 天枢 Transport（完全自研，不引用 cyber）                     │
│   INTRA · SHM · 跨进程 · 跨机 · HYBRID 自动选路              │
└────────────────────────────────────────────────────────────┘

横切：三层确定性
  · 构建确定性（相同源代码 → 比特级一致二进制）
  · 执行确定性（相同二进制 + 相同输入 → 相同输出）
  · 血缘可追溯（任一输出 → 追溯全部输入 + 状态 → 可重放验证）
```

### Layer 1：DSL + trace + 六阶段编译器

**DSL 形式**（详见 [adr/0001-dsl-form.md](./adr/0001-dsl-form.md)）：C++ fluent builder + auto trace，对标 JAX / torch.compile 路线。

```cpp
void perception_flow(Node& node) {
  auto camera = node.reader<CameraMsg>("/perception/front");
  auto lidar  = node.reader<LidarCloud>("/lidar/points");

  node.on_input({camera, lidar}, [&](auto c, auto l) {
    auto det = detect_op(AllLatest::fuse(c, l));
    predict_out.write(predict_op(det));
  })
  .with_sla(SLA{.deadline_ms = 50, .priority = 10})
  .with_fallback("perception_flow_lite")
  .with_fault_tolerance({.strategy = SYNC_REPLAY, .replay_window_ms = 50})
  .with_backpressure({.high_wm = 3, .low_wm = 1});
}

REGISTER_TRACEABLE_FLOW("perception_flow", perception_flow);
```

**六阶段编译**（加载期，一次性）：

| Pass | 名称 | 输入 | 输出 |
|---|---|---|---|
| 0 | trace | flow 函数 + 干跑上下文 | trace AST（reader/writer/create_routine 记录） |
| 1 | 分析 | trace AST | 全局数据流图（节点 + 边 + SLA 标注） |
| 2 | 逻辑优化 | 全局图 | 优化后的图（死通道消除、常量传播、冗余 reader 合并） |
| 3 | 算子融合 | 优化后的图 | 融合后的图（多个算子合入单一 CRoutine） |
| 4 | SLA 物理规划（RTA） | 融合图 + SLA | 调度方案（cpuset / priority / queue_size / 验证报告） |
| 5 | codegen | 调度方案 | C++ 源码（`Component<M0>` 子类 + `.dag` + `.conf`） |
| 6 | 编译 | C++ 源码 | `.so`（动态加载到 ti launch） |

### Layer 2：零拷贝数据血缘 + 分级容错

- **血缘**：每条消息记录 ~50ns 的元数据（生产者 ID + 时间戳 + 上游指针，不拷贝 payload）
- **分级容错**：
  - `SYNC_REPLAY`：丢帧时从 ring buffer 同步重放上一帧（< 1ms 恢复）
  - `ASYNC_REPLAY`：后台异步重放 + 当帧用上次（避免阻塞关键路径）
  - `DEGRADE`：切到 fallback flow（如 perception → perception_lite）

### Layer 3：SLA 硬约束调度器

- 端到端 deadline 作为 DSL 一等输入
- **RTA（Response Time Analysis）**：编译期计算每个任务的最坏响应时间，包含跨模块干扰
- 不可满足时**加载期就报错**（不是运行时偶发超时）
- 自动派生：cpuset / priority / pending_queue_size / 反压水线

### Layer 4：运行时咬合闭环

天枢自研 transport（INTRA / SHM / 跨机），上层叠加：

- CRoutine 协程（自研，详见 [adr/0002](./adr/0002-cyber-relation.md)）
- 血缘追踪器（每消息写入）
- 反压传播器（水线超限→上游降频）
- SLA 调度器（EDF + cpuset 绑定，CPU 资源）
- **GPU 调度器**（设计就绪，实现按 profile 渐进，详见 [adr/0006](./adr/0006-gpu-acceleration.md)）— GPU 设备/stream/内存池/IPC 共享/故障检测；Phase 0 锁定接口契约，Phase 2/3 渐进实现
- 状态 checkpoint（有状态算子定期存档）

### 横切：三层确定性

| 层 | 保证 | 工程价值 |
|---|---|---|
| 构建确定性 | 相同源代码 → 比特级一致二进制 | ISO 26262 可追溯；CI 哈希校验 |
| 执行确定性 | 相同二进制 + 相同输入 → 相同输出 | 事故调查：回放传感器数据 → 复现决策 |
| 血缘可追溯 | 任一输出 → 追溯全部输入 + 状态 → 可重放 | 回归测试：旧数据跑新代码 → 对比输出 |

---

## 5. 开发者契约

### 5.1 编译期给的保证

- DSL 类型检查（reader 类型、callback 签名）
- SLA 可调度性验证（RTA）
- 全局 cpuset 冲突检测
- 纯函数状态转移校验（用于有状态算子）
- codegen 产物的语法正确性

### 5.2 加载期（ti launch 启动）做的事

- trace flow 函数（dry-run，记录数据流操作）
- 跑六阶段编译器
- 编译 .so（如果带 JIT）或加载预编译 .so
- 加载期失败立即 abort，不让错误流到运行时

### 5.3 运行时给的保证

- 每条消息有血缘元数据
- SLA 违反产生 trace 事件（不静默）
- 丢帧/慢帧按 flow 配置自动容错
- 反压按水线传播
- 状态 checkpoint 周期性归档

---

## 6. 与同类系统的边界

| 维度 | Spark / Flink | ROS 2 | Cyber RT | **天枢** |
|---|---|---|---|---|
| 域 | 大数据 | 机器人 | 自动驾驶 | 自动驾驶 |
| 延迟 | 100ms-秒 | ms-秒 | ms-50ms | <1ms-50ms 硬实时 |
| 编程范式 | 声明式 | 命令式 | 命令式 | **声明式 + 加载期编译** |
| SLA | 无 | 无 | 人工 .conf | **硬约束 + 编译期 RTA** |
| 运行时 | JVM | C++ | C++ | C++ |
| 确定性 | 无 | 无 | 无 | **三层确定性** |
| 容错语义 | 节点崩溃→重算分区 | 节点重启 | 进程重启 | **丢帧→重放单帧** |

**一句话**：天枢不是大数据框架的子集，也不是 ROS / Cyber 的小修小补，是声明式 + 编译期 + 硬实时 + 确定性四个维度的交集。

---

## 7. 三个核心假设（PoC 必须验证，详见 [01-roadmap.md](./01-roadmap.md)）

| # | 假设 | 验证标准 | 失败兜底 |
|---|---|---|---|
| H1 | trace 能捕获所有数据流操作 | examples/* 全覆盖，输出与手写 100% 一致 | RAII guard 加强 + 显式 escape hatch |
| H2 | codegen 产物性能 ≈ 手写 | 两层语义（[ADR-0034](./adr/0034-gate-semantics-v3.md)）：主承诺 = 加性分解 `diff ≤ F + k×跳数 + m×扇出支数`（k ≤ 2ns/跳、F ≤ 20ns/消息、m 待定），空算子五形状直接量取；从承诺 = W ≥ (F+k×hops)/1% 时 P99 差 <1% | 引入 pass 调优与 ADR-0035 算子直呼；若仍不达标则触发方案修订 |
| H3 | RTA 的 WCET 估计准确 | Apollo 实测 P99.9 × 1.0 ~ 1.3 | profile-guided 校准 |

任一假设失败 → **专利新颖性失效**，需要回炉。

---

## 8. 现状（2026-09-09）

| 项 | 状态 |
|---|---|
| DSL 选型（fluent builder + auto trace） | ✅ 定（ADR-0001） |
| 与 Cyber RT 关系（独立实现） | ✅ 定（ADR-0002） |
| 仓库骨架（目录、文档、CI） | ✅ 完成（2026-08，M0 通过） |
| Phase 1 PoC | 🟡 进行中——L4 全栈（PRIM/SCHED/CORE/TRANS）、DSL v0 + 血缘、kAuto、record v2、SLA v0 已落地；L1 codegen（H2）为主战场（详见 [arch/](./arch/README.md) 状态表） |
| MVP（替换 Apollo perception 链路） | ⏳ Phase 2 |
| 认证就绪（ISO 26262 ASIL-D） | ⏳ Phase 3 |
