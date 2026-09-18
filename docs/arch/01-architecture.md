# 天枢总体架构（as-built）

> **文档定位**：框架现状的系统视图——分层、模块地图、一条消息的生命周期。学完 [00-overview.md](../00-overview.md)（愿景叙事）后从这里进入实现。
> **维护契约**：模块增删、跨模块机制变化时必须更新本文（见 [README.md 维护契约](./README.md#维护契约每次迭代必须执行)）。
> **最后同步**：2026-09-09 · Phase 1 PoC

---

## 1. 一个关键映射：逻辑分层 ≠ 代码目录

ADR 体系用 **L1–L4 逻辑分层**讨论设计（术语边界见 [ADR-0002](../adr/0002-cyber-relation.md)），但**代码目录是物理组织，两者不是一一对应**。初学者最容易在这里迷路，先给映射表：

| 逻辑层 | 定义（ADR-0002） | 代码落点（as-built） |
|---|---|---|
| L1 · DSL / 编译器 | flow 声明与加载期编译 | `dsl/`（声明 + 解释器）+ `compiler/`（IR / 管线 / codegen，🟡 推进中） |
| L2 · 血缘 / 容错 | 逐消息溯源、恢复 | `core/lineage.h`（值对象）+ `dsl/`（map/join 自动追加 hop、区间跳、恢复协议、`with_fallback` 降级阶梯 [ADR-0031](../adr/0031-fallback-degradation.md)）+ `dsl/record*.h`（记录回放基底） |
| L3 · SLA | 加载期 deadline 验证与预算分配 | `sla/`（分析器 + 统计）+ `dsl/` 的 `with_sla` / `with_wcet` 声明面 |
| L4 · 运行时原语与传输 | 中间件底座 | `base/` `sched/` `core/` `transport/` `shm/` + `cli/`（ti 家族） |

> 规律：**声明面和解释器都在 `dsl/`，分析器在 `sla/` / `compiler/`，原语在 L4 各目录**。L2/L3 的"能力"散布在 L4 数据面之上——这正是"声明式框架"的形态。

## 2. 分层视图

```mermaid
flowchart TB
    subgraph L1["L1 · 声明与编译"]
        DSL["dsl/flow.h<br/>FlowBuilder · Stream&lt;T&gt; · FlowChain<br/>source / map / join / span_join / op / from / sink / with_fallback"]
        RT["dsl/dsl_runtime.h<br/>FlowRuntime 解释器（同步级联）"]
        COMP["compiler/ 🟡<br/>IR · pipeline · codegen<br/>（ADR-0030 六阶段，M-A~M-D 推进）"]
        SLA["sla/ · SLA 分析器<br/>链路预算 + 饱和度准入（ADR-0029）"]
    end
    subgraph L4C["L4 · 运行时核心"]
        CORE["core/ · Node / Reader&lt;T&gt; / Writer&lt;T&gt;<br/>DataDispatcher · DataVisitor（AllLatest）<br/>Component 家族 · Launcher · Lineage"]
        SCHED["sched/ · 回调调度器<br/>优先级队列 + N worker（无协程，ADR-0019）"]
        MON["core/monitor.h + field_table.h<br/>ti-monitor · POD 字段反射（ADR-0020）"]
    end
    subgraph L4T["L4 · 传输"]
        TRANS["transport/ · TransportBackend 抽象<br/>INTRA 零拷贝 · SHM 跨进程 · kAuto 双发选路（ADR-0023）"]
        SHM["shm/ · offset_ptr · ShmSegment · SpscRing"]
        SIDE["schema sidecar · 跨进程 schema 分发"]
    end
    subgraph PRIM["L4 · 基础原语"]
        BASE["base/ · ObjectPool · CacheBuffer · AtomicHashMap<br/>RWLock / SpinLock / TicketLock · BlockingCounter / Notification · SmallVec"]
    end
    subgraph TOOLS["工具面"]
        CLI["cli/ · ti / ti-launch / ti-monitor / ti-info"]
    end

    DSL --> RT
    RT -->|"声明图即 IR"| COMP
    DSL --> SLA
    RT --> CORE
    CORE --> SCHED
    CORE --> TRANS
    TRANS --> SHM
    TRANS --> SIDE
    CORE --> BASE
    SCHED --> BASE
    CLI --> CORE
    MON --> TRANS
```

> 箭头 = 主要供给/驱动关系（细粒度依赖见各 [模块页](#3-模块地图) §4）。

## 3. 模块地图

| 模块 | 目录 | 状态 | 一句话职责 | 详档 |
|---|---|---|---|---|
| 基础原语库 | `tianshu/base/` | ✅ | 无锁/低开销数据结构与同步原语，全框架地基 | [modules/base.md](./modules/base.md) |
| 调度器 | `tianshu/sched/` | ✅ | 回调式优先级调度（Phase 1 无协程） | [modules/sched.md](./modules/sched.md) |
| 运行时核心 | `tianshu/core/` | ✅ | Node/typed 读写器、分发与融合、Component、Launcher、Lineage、Monitor | [modules/core.md](./modules/core.md) |
| 传输层 | `tianshu/transport/` + `tianshu/shm/` | ✅* | INTRA + SHM 跨进程 + kAuto 自动选路（*跨机 Zenoh 未实现，ADR-0013） | [modules/transport.md](./modules/transport.md) |
| 声明式 DSL 与血缘 | `tianshu/dsl/` | ✅ | flow 声明、解释执行、切片输入、状态即数据、record v1/v2、fallback 降级阶梯 | [modules/dsl.md](./modules/dsl.md) |
| SLA 编译 | `tianshu/sla/` | ✅ | 加载期 deadline 验证 + 预算下行分摊 | [modules/sla.md](./modules/sla.md) |
| L1 编译器 | `tianshu/compiler/` | 🟡 | 声明图 → 优化 → 源码 codegen → `.so`（Phase 1 H2 验证项） | [modules/compiler.md](./modules/compiler.md) |
| ti CLI 家族 | `tianshu/cli/` | ✅ | ti / ti-launch（DAG 装载）/ ti-monitor（通道观测）/ ti-info | [modules/cli.md](./modules/cli.md) |

**验证/示例资产**：`tests/`（按模块镜像：base/sched/core/transport/shm/dsl/compiler）、`benchmarks/`（object_pool / shm_transport / lineage / codegen_vs_handwritten）、`examples/`（dsl_demo → lidar_imu → full_chain → record_replay → state_recovery，难度递进；叙事讲解见 [03](../03-full-chain-demo.md)/[04](../04-data-model-generalization.md)/[05](../05-dsl-getting-started.md)）。

## 4. 一条消息的生命周期

### 4.1 DSL 解释执行路径（在线）

```mermaid
sequenceDiagram
    autonumber
    participant SRC as source 节拍线程<br/>（绝对 deadline，无累积漂移）
    participant RT as FlowRuntime（级联）
    participant MB as 源通道的消费者 mailbox
    participant LN as Lineage（root + hops + branches）
    participant SINK as 下游 stage / sink

    SRC->>RT: emit(t0) 按 interval 节拍
    RT->>LN: 建 root（"ch#seq"）
    RT->>MB: publish(data, size, lineage_ptr)
    loop 每个注册在该通道上的消费者（互不偷帧）
        MB->>RT: pop → 调度 stage 回调
        alt map / span_join / op
            RT->>LN: 追加 hop（或区间跳 "ch#lo..#hi"）
        else join（AllLatest 融合）
            RT->>LN: 合并双亲分支集（root 去重，kMaxBranches=8 封顶）
        end
        RT->>SINK: 向输出通道 publish（级联继续）
    end
    Note over RT,SINK: 全程同步级联（ADR-0021 修正案）；<br/>record_to(path) 把通道历史环 dump 为 record 文件，<br/>replay_from 在新 runtime 重放 SOURCE 通道 →<br/>中间通道重算，输出逐字节一致
```

### 4.2 Component / DAG 装载路径（ti-launch）

```mermaid
flowchart LR
    A["ti launch app.dag"] --> B["DagConfig 解析<br/>（INI 子集，TOML 形）"]
    B --> C["ComponentFactory<br/>TIANSHU_REGISTER_COMPONENT 注册表"]
    C --> D{"组件形态"}
    D -->|"TimerComponent / TimerSourceComponent"| E["自有线程<br/>绝对 deadline 定时"]
    D -->|"输入驱动 Component&lt;M&gt;"| F["跑在发布者 dispatch 线程<br/>（from() 桥接语义，ADR-0025）"]
    E --> G["DataVisitor AllLatest 融合"]
    F --> G
    G --> H["proc() 计算"]
    H --> I["publish 携带血缘<br/>（mailbox 泵回 parent，输出=派生而非根）"]
    I --> J["反向顺序 shutdown<br/>run_for 返回前 quiesce 全部引用组件"]
```

### 4.3 跨进程路径（kAuto）

同进程走 INTRA 零拷贝 fan-out；`AutoWriter` 同时双发 SHM 广播（零读者时近乎免费），读端按 `IntraChannelRegistry` 判定本进程是否有真实写者来选路——**任意创建顺序皆正确，无需 discovery**（ADR-0023）。段布局、CAS 初始化、进程共享 condvar 唤醒等细节见 [modules/transport.md](./modules/transport.md) §3。

## 5. 代码组织与构建视图

```
tianshu/
├── include/tianshu/     # 公共 API（唯一出口，按模块分目录；ADR-0007 C ABI 演进面）
│   ├── base/ sched/ core/ shm/ transport/ dsl/ sla/ compiler/
│   └── version.h        # C ABI 版本接口
├── src/                 # 私有实现（扁平 .cc，与头文件目录对应）
├── cli/                 # ti 家族入口 main
examples/ tests/ benchmarks/   # 与模块镜像组织；tests 含 fork 级跨进程用例
```

- **双构建零警告**（CMake 主 + Bazel 验证轨，ADR-0003/0004）；构建入口只允许原生 `cmake` / `bazel` 命令。
- **5 profile**（desktop/server/vehicle/embedded/mcu）编译期裁剪（ADR-0005）；当前 desktop 已验证。
- 依赖白名单 `ALLOWED_DEPS.txt`（ADR-0005 审批制）。
- 覆盖率权威管线（GCC + lcov）见 [development/coverage-guide.md](../development/coverage-guide.md)。

## 6. 从这里去哪

- 深入某模块 → [modules/](./modules/) 对应页（§2 API / §3 内部设计 / §5 决策史）
- 查设计动机与被否方案 → [adr/README.md](../adr/README.md)（按域检索）
- 动手写第一条 flow → [05-dsl-getting-started.md](../05-dsl-getting-started.md) → 跑 `examples/dsl_demo`
- 看全链路闭环叙事 → [03-full-chain-demo.md](../03-full-chain-demo.md)
