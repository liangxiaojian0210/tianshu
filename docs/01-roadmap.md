# 天枢 (TIANSHU) — 实施计划

> **文档定位**：4 阶段实施路线图，从奠基到认证就绪（叙事文档）。
> **维护者**：Pride Leong
> **状态**：v0.2（2026-09-09）· Phase 0 **完成** · Phase 1 **进行中**
> **关联**：[00-overview.md](./00-overview.md) · [adr/0001-dsl-form.md](./adr/0001-dsl-form.md) · [adr/0002-cyber-relation.md](./adr/0002-cyber-relation.md)

---

## 总览

| Phase | 名称 | 周期 | 目标 | 退出条件 |
|---|---|---|---|---|
| 0 | 奠基 | 2 周 | 仓库可 clone 可编译可读 | M0 通过 |
| 1 | PoC | 8-12 周 | 验证三个核心假设（H1/H2/H3） | M1 通过，专利新颖性有代码证据 |
| 2 | MVP | 3-6 月 | 替换一个 Apollo mainboard（推荐 perception） | M2，跑通测试车 |
| 3 | 认证就绪 | 6-12 月 | ISO 26262 ASIL-D 追溯 | M3，可交第三方认证 |

**关键里程碑链**：

```
M0 仓库就绪  →  M1 PoC 通过（H1+H2+H3 全绿）
                ↓ 任一假设失败 → 方案回炉
                ↓ 三假设全绿
M2 MVP 上车  →  M3 认证就绪
```

---

## Phase 0：奠基期（2 周）

**目标**：把仓库变成可工作的工程产物。

### 任务清单

| # | 任务 | 产物 | 验收 |
|---|---|---|---|
| 0.1 | 迁移 branding 资产 | `branding/assets/*.svg`（5 个） + 预览 HTML | 浏览器可开 |
| 0.2 | 写 README + docs/00 + docs/01 + 2 份 ADR | 4 份 md | 用户审核通过 |
| 0.3 | 定 C++ 版本（C++20）+ 构建系统（**CMake + Bazel 双轨**，详见 [adr/0003](./adr/0003-build-system.md)）+ 编译器要求 | `CMakeLists.txt` + `WORKSPACE` + 顶层 `BUILD` | hello world 在两套系统都编译通过 |
| 0.4 | 仓库骨架（目录、`.gitignore`、CI 配置） | GitHub Actions 跑通 | lint + test 通过 |
| 0.5 | 决定许可证（推荐 Apache 2.0） | `LICENSE` | 用户拍板 |
| 0.6 | 建里程碑 + issue tracker | GitHub Project / Issues | Phase 1 任务全部入 issue |

### 退出条件（M0）

- [ ] 仓库 clone 后 5 分钟内能编译跑 hello world
- [ ] README 让外人 5 分钟看懂项目定位
- [ ] docs/00 + docs/01 完整，ADR-0001 / ADR-0002 拍板
- [ ] CI（lint + test）跑通
- [ ] Phase 1 任务全部成 issue

---

## Phase 1：PoC — 验证三个核心假设（8-12 周）

**目标**：用最小可跑实现证明专利的核心新颖性是真的，不是纸面设计。

### 三个核心假设

| # | 假设 | 验证标准 | 失败兜底 |
|---|---|---|---|
| H1 | trace 能捕获所有数据流操作 | examples/* 全覆盖，输出与手写 100% 一致 | RAII guard 加强 + 显式 escape hatch |
| H2 | codegen 产物性能 ≈ 手写 | 5 类典型链路 P99 差 ≤ max(1%, 2ns/跳)——同语义基线（金标准携带 lineage），绝对余量承认为类型擦除下限的间接调用物理残余（[ADR-0032](./adr/0032-per-message-specialization.md) D5 重校准，2026-09-17） | pass 调优；若仍不达标 → 方案回炉 |
| H3 | RTA 的 WCET 估计准确 | Apollo 实测 P99.9 × 1.0~1.3 | profile-guided 校准 |

### 1.1 最小可跑子集（4 周）

| 模块 | 实现范围 | 暂不实现 |
|---|---|---|
| `tianshu/dsl` | `Node` / `Reader<T>` / `Writer<T>` / `on_input` / `with_sla` | `on_batch` / `on_window` / `with_state` |
| `tianshu/trace` | RAII guard 拦截 reader / writer / create_routine | 跨线程 trace、嵌套 trace |
| `tianshu/compiler` | Pass 0 trace + Pass 1 分析 + Pass 5 codegen | Pass 2/3/4 优化 |
| `tianshu/codegen` | 生成等价的 `Component<M0>` 子类源码 + `.dag` + `.conf` | 跨算子融合 codegen |
| `tianshu/transport`（最小） | INTRA + SHM（同进程内 + 同机跨进程） | 跨机 RTPS、HYBRID 自动选路 |
| `tianshu/coroutine`（最小） | 用户态协程（基于 `ucontext` 或汇编 swap） | 多核 work-stealing |
| `tianshu/scheduler`（最小） | 单策略 SchedulerClassic（优先级队列 + processor 池） | SchedulerChoreography |

**示例产物**：`examples/perception_chain.cpp`（detect → filter → nms 三算子链）

- 写法：声明式 flow 函数
- 跑法：`tianshu-ctl compile perception_flow` → 生成 `.so` + `.dag` + `.conf`
- 加载：`ti launch perception_flow.dag` 启动
- 验证：输出与手写等价

### 1.2 验证 H1（trace 完备性，1-2 周）

**输入**：5-10 个覆盖典型用法的 flow 函数（含扇入 / 扇出 / 跨节点 / 嵌套 callback）。

**手段**：

1. 每个 flow 跑 trace + codegen + 编译 + 运行
2. 对比手写 baseline 的输出（逐字节比对）
3. 对 trace AST 做覆盖率检查（每个 reader / writer 都被记录）

**验收**：100% 输出一致。如发现 trace 泄漏（某个 reader 没被记录），加 RAII guard 修复。

### 1.3 验证 H2（zero-overhead，2-3 周）

**输入**：5 类典型链路

| 类型 | 链路形状 | 算子数 |
|---|---|---|
| 短链 | A → B | 2 |
| 中链 | A → B → C → D → E | 5 |
| 长链 | A → B → ... → J | 10 |
| 扇入融合 | (A, B, C) → D | 4 |
| 扇出分发 | A → (B, C, D, E) | 5 |

**手段**：

- 每类 1M 次迭代
- 取 P50 / P99 / P99.9 延迟
- 对比对象：相同语义的手写 cyber 风格代码（在 tianshu 自家 transport 上跑）

**验收**：P99 延迟差异 < 1%。这是专利的核心承诺，必须达成。

**失败处理**：

- 差距 1-5%：做 pass 调优（融合、内联、寄存器分配）
- 差距 > 5%：触发方案修订，可能需要重写 codegen 策略
- 差距 > 20%：专利新颖性失效，回炉重设计

### 1.4 验证 H3（RTA 准确性，2-3 周）

**输入**：Apollo 现成 `.dag` / `.conf` + 实际 workload 录制（cyber_recorder）。

**手段**：

1. 把 Apollo 的 DAG 配置翻译成天枢 DSL（手工翻译一次）
2. 跑 tianshu 编译器，得到 RTA 估算的 WCET
3. 离线分析 lineage 日志，取实测 P99.9 WCET
4. 对比估算 vs 实测

**验收**：RTA 估算值在 [实测 P99.9, 实测 P99.9 × 1.3] 区间内。

- 估算 < 实测 → 太乐观，可能导致运行时违反 SLA（不可接受）
- 估算 > 实测 × 1.3 → 太保守，可能导致 SLA 不可满足（虽然安全但不可用）

### Phase 1 退出条件（M1）

- [ ] H1 全绿（trace 覆盖率 100%）
- [ ] H2 全绿（5 类链路 P99 差 ≤ max(1%, 2ns/跳)，同语义基线；M-C 特化已落地，正式判决待空闲窗口取数）
- [ ] H3 全绿（RTA 估算落在合理区间）
- [ ] 三个核心假设的验证报告归档（含原始数据 + 复现脚本）
- [ ] PoC demo 视频 + benchmark 数据公开

**M1 失败处理**：任一假设失败 → 暂停 Phase 2 启动，召开方案修订会议，可能触发专利修订。

---

## Phase 2：MVP — 生产可用子集（3-6 月）

**目标**：覆盖 Apollo 感知-预测-规划关键链路，能替换 perception mainboard 上车。

### 2.1 完整编译器 pass 链（4-6 周）

- Pass 2 逻辑优化（死通道消除、常量传播、冗余 reader 合并）
- Pass 3 算子融合（专利核心新颖性）
- Pass 4 SLA 物理规划（RTA + cpuset/priority 推导 + queue_size 推导）

### 2.2 Layer 2 血缘与容错（4-6 周）

- 帧级零拷贝血缘（~50ns/消息）
- 三级容错：`SYNC_REPLAY` / `ASYNC_REPLAY` / `DEGRADE`
- 跨进程血缘聚合（多进程数据流的统一视图）

### 2.3 Layer 3 SLA 调度（3-4 周）

- 端到端 deadline 派生（从顶层 SLA 推到每算子）
- 加载期 SLA 违反检测 + 建议生成
- 多加载进程的 cpuset 全局视图（解决 cyber 跨进程 cpuset 冲突问题）

### 2.4 有状态处理（2-3 周）

- `with_state<T>` 模板
- 状态 checkpoint（定期存档）
- 状态转移纯函数约束（编译期校验）
- 状态 replay（丢帧时从 checkpoint 恢复）

### 2.5 反压传播（2 周）

- 高低水线 + 跨算子反压
- 反压到上游传感器（降频信号）
- 反压事件入 lineage

### 2.6 Transport 完善（4 周）

- 跨机传输（建议参考 Fast-DDS 或自研基于 UDP/SHM 混合）
- HYBRID 自动选路（同进程 INTRA / 同机 SHM / 跨机 RTPS）
- QoS 配置（深度/可靠/历史）

### 2.7 上车集成（4 周）

- 选定 mainboard：**perception**（最典型，多传感器扇入）
- 把 Apollo 的 perception Component 全部翻译成天枢 flow
- 部署到 ORIN 测试车
- 跑通闭环：传感器 → perception → planning → control → CAN

### Phase 2 退出条件（M2）

- [ ] perception mainboard 用天枢重写并稳定运行 24 小时
- [ ] 端到端 SLA（200ms）满足
- [ ] 关键异常场景（丢帧 / GPU OOM / CPU 抢占）自动恢复
- [ ] benchmark：相比原 cyber 版本，性能不退化
- [ ] 测试车跑通 N+1 圈闭环

---

## Phase 3：认证就绪（6-12 月）

**目标**：满足 ISO 26262 ASIL-D 追溯要求。

### 3.1 批流统一（4-6 周）

- `on_batch<N>` + `on_window<N, step>`
- 编译器根据 SLA 自动决策流/批/窗口模式

### 3.2 算法组合（3-4 周）

- `Pipeline` / `Stage` 高级抽象
- 预置算法库（感知/预测/规划常用算子）

### 3.3 确定性执行验证（4 周）

- 纯函数状态转移的运行时强制
- 状态版本绑定到 lineage
- 确定性调度（去除时间戳依赖）
- 自动化确定性测试套件

### 3.4 可重复构建（3-4 周）

- 确定性 trace（去除 `__TIME__` / `rand()` / 指针地址）
- 纯函数 codegen（无副作用）
- 构建清单哈希链（源码 → IR → 二进制）
- CI 跨机 reproducibility 校验

### 3.5 工具链完善（4-6 周）

- `tianshu-ctl trace / compile / inspect / replay`
- lineage 可视化（Web UI）
- SLA 报告生成器
- 静态检查器（lint for tianshu DSL）

### 3.6 文档完整（持续）

- DSL 语法参考（含所有 API）
- 编译期/运行时契约
- ADR 全套（10+）
- 性能 benchmark 报告
- 迁移指南（从 cyber → tianshu）

### Phase 3 退出条件（M3）

- [ ] 通过内部 ISO 26262 预审
- [ ] 完整确定性测试通过（同输入 → 同输出，跨机跨次）
- [ ] 跨机 reproducibility 校验通过
- [ ] 可交第三方认证机构

---

## 风险登记

| 风险 | 概率 | 影响 | 缓解措施 |
|---|---|---|---|
| **codegen 性能落差 > 1%** | 低 | **致命**（毁掉专利新颖性） | Phase 1 优先验证 H2；失败立即方案回炉 |
| **重写 transport 工作量超预期** | 中 | 高 | 分阶段：先 INTRA+SHM，跨机延后；可临时用第三方库（如 zenoh）填充 |
| **trace 泄漏导致 H1 失败** | 低 | 高 | RAII guard + 覆盖率检查 + escape hatch（命令式回退） |
| **RTA 估算过保守，SLA 不可满足** | 中 | 中 | profile-guided 校准 + 经验库；必要时引入手动 override |
| **一人开发，Phase 2 周期长** | 高 | 高 | 严格优先级：M2 只做 perception 一个 mainboard，不铺开 |
| **协程实现汇编级 bug（架构相关）** | 中 | 高 | 优先用 `ucontext` / `boost.context` 起步，汇编优化延后；多架构 CI 矩阵 |
| **测试车资源紧张，集成滞后** | 中 | 中 | Phase 2 前期用仿真 + replay 验证，测试车窗口预占 |

---

## 当前进度（2026-09-09）

| 项 | 状态 |
|---|---|
| Phase 0 全部任务（0.1–0.6） | ✅ 完成（2026-08，M0 通过：双构建零警告 CI / 253 测试 / 覆盖率管线） |
| Phase 1 · 1.1 最小可跑子集 | ✅ 超额完成——L4 全栈 + DSL v0 + 血缘 + kAuto + record v2 + SLA v0 均已落地（细节见 [arch/](./arch/README.md)） |
| Phase 1 · H1/H2/H3 验证 | 🟡 H2 主战场：M-C 逐消息特化已落地（ADR-0032），正式判决待空闲窗口取数（MC-4） |
| Phase 2 / Phase 3 | ⏳ |
