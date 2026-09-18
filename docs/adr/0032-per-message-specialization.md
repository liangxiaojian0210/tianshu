# ADR-0032：M-C 逐消息特化 —— 类型化宿主钩子、封闭通道快路径与 H2 门槛重校准

- **状态**：已接受
- **日期**：2026-09-17
- **决策者**：Pride Leong
- **关联**：[adr/0030](./0030-l1-compiler.md)（D4 修正 · D5 装置 · M-C 里程碑） · [adr/0021](./0021-dsl-v0.md) · [adr/0022](./0022-lineage-v0.md) · [adr/0028](./0028-record-format-v1.md) · [adr/0029](./0029-sla-compilation.md) · [01-roadmap §1.3](../01-roadmap.md)

---

## 需求清单

| # | 需求 | 来源 |
|---|---|---|
| 1 | 编译产物逐消息路径消除解释执行的通用机制开销（seq 全局互斥、LineageQueue deque 堆分配、HistoryRing 无条件捕获、逐跳上下文哈希） | ADR-0030 D8 收益边际：L1/L2c 落地后差距仍 4.0-7.6× |
| 2 | H1 不回退：compiled 与 interpreted 输出、血缘、seq、SLA、录制文件逐字节一致 | ADR-0030 需求 3/4 |
| 3 | 缓存键不变：特化是安装策略，不是图改写；同 flow 哈希与 M-B 一致 | ADR-0030 D1 |
| 4 | H2 判决可在物理上做出（门槛定义必须可实现） | roadmap §1.3 / D5 |

## 决策

### D1：D4 修正案 —— 类型墙与类型化宿主钩子

ADR-0030 D4 设想"算子函数体 lambda 内联进 dispatch 回调"。**架构上不可能按字面实现**：
IR 的类型已擦除为 `type_name` 字符串，算子 fn 被捕获在 `wire` 闭包内部，`.gen.cc`
（独立编译单元）永远无法直呼 fn 的函数体。修正为**双层结构**：

- **类型化闭包在宿主侧实例化**：`FlowBuilder::map/join/sink` 在类型已知处同时生成
  `specialize` 钩子（`make_map_specialize<TIn,TOut>` 等，flow.h 声明 + dsl_runtime.h
  定义，双声明纪律）；钩子把算子交给 `SpecializeBuilder`。
- **产物只做常量编排**：`.gen.cc` 的 `tianshu_flow_install` 调 `rt->begin_specialize(*flow)`
  取得 builder，按 **wire() 同序**（每 kind 的声明序）逐 decl 调 `specialize`，最后
  `plan.finalize()` 冻结扇出绑定。安装顺序与解释执行一致保证注册序（dispatcher 通知序、
  消费者扇出序）逐位相同。

产物侧的每跳代码因此由宿主实例化的模板闭包执行：`try_fetch → fn（无捕获 lambda
走单函数指针擦除）→ add_hop（通道常量 + 自持单写者计数器）→ 常量扇出 → dispatch(常量 id)`。
fn 保持一次间接调用，这是类型擦除下的物理下限（见 D5）。

### D2：通道计划从 Flow 自身推导（无 IR 漂移）

`SpecializeBuilder` 构造时对 Flow 声明图做一次生产者普查：

- **fast_fan(channel)** ⇔ 唯一生产者且 kind ∈ {map, join} 且 flow 无 SLA endpoint。
  fast 生产者的扇出不经 `publish_impl`：历史条件捕获、血缘认投递（单消费者移动、
  多消费者 N-1 拷贝 + 1 移动）、常量 id 直 dispatch。
- **inbox 输入** ⇔ 生产者为唯一 source（单定时线程）或 fast map/join（线性级联在
  驱动线程上、join 被单飞锁串行化）。`LineageInbox` = 定容环 + head/tail 原子
  （v0 同步级联下消费发生在发布的同一调用栈，深度实际恒 ≤1，环容量只为 join
  配对前的积累）。**多生产者通道（反馈边、op/stateful/span/from 输出）保持
  mutex+deque 队列**——它们的写者可能在并发线程上（op/stateful/span 的 handle
  无串行化保证，即使单生产者也不给 inbox）。
- **op/stateful/span/from 一律通用安装**（v0）；它们的消费者经 `ctx.queues`
  与 fast 生产者自然混合（fast 扇出推基类指针，LineageQueue/Inbox 同接口）。

### D3：语义收窄清单（compiled 与 interpreted 的公开行为差异）

1. **历史捕获条件化**：非 SLA flow 的特化安装只对有图声明观察者的通道
   （span 数据通道、stateful 状态通道）保留历史；纯 map/join/sink 链的中间通道
   `history()` 返回空环。record v2 hook 的是 publish 路径，不受影响；录制中
   fast 扇出整体回落通用 `publish_bytes`，record 文件与解释执行逐字节一致。
2. **SLA flow 全通用**：声明了 endpoint 的 flow 整体走通用安装（v0），thread_local
   born 戳逻辑保持单一实现；副作用是修复了 M-B 既有缺口——compiled SLA flow
   此前从不武装统计（`begin_specialize` 补上了 arming）。

### D4：H1 不变式（`tests/dsl/specialize_test.cc` 逐条锁定）

| # | 不变式 | 保障 |
|---|---|---|
| I1 | sink payload 序列一致 | 安装序 = wire() 序（通知序不变） |
| I2 | 血缘字节一致 | 同 rooted/add_hop/merge 内联头；join 按 a-then-b pop 后 merge |
| I3 | seq 逐通道单调、值域一致 | fast stage 自持计数器与通用 next_seq 语义同构（每通道从 0 起、每消息 +1），且两套计数器按 fast 规则永不同写一通道 |
| I4 | SLA snapshot 一致 | SLA flow 全通用 |
| I5 | record 文件一致 | 录制中回落通用路径（`recording_active` 逐跳原子检查，未录制时一次可预测分支） |
| I6 | replay 一致 | 入口分支自然覆盖 |
| I7 | 反馈/混合图 | 多生产者强制队列，混合安装经 ctx.queues 合流 |

附带修复：`start_recording`/`stop_recording` 现在清空 publish 上下文快照
（epoch 递增）——此前录制在首次发布之后武装会静默漏采已解析通道（既有缺陷）。

### D5：H2 门槛重校准（Pride Leong 2026-09-17 裁决，方案 1）

**原门槛**："产物 vs 手写 P99 <1%"。两个物理事实使字面门槛不可实现：

1. **金标准语义不对齐**：原手写金标准不携带 lineage（无 rooted、无逐跳 hop、无
   逐通道 seq）。逐消息血缘是框架的核心语义而非开销；H2 假设原文即"**相同语义**的
   手写代码"。**修正**：金标准补齐 lineage 纪律（根、每跳 add_hop+计数、join 合并、
   槽位投递、逐消费者拷贝），全部手写质量（SSO、单槽、无锁）。
2. **预算数学**：1% 预算（short p99 ≈51ns → 0.51ns）低于任何真实残余——一次
   fn 间接调用（类型擦除下限，≈0.2-1ns/跳）、入口一次分支与计数器访问之和在
   short/medium 上必然超预算。

**新门槛**：**p99 差 ≤ max(1%, 2ns/跳 绝对余量)**。2ns/跳 是间接调用 + 计数器 +
分支的物理残余论证值，不是随意放宽。基线数字必须在空闲大核按 §6.1 协议三轮重定
（旧无血缘基线作废，勘误链同 medium=110 先例）。

### D6：范围裁决

- **P2 optimize pass 继续搁置**：fn 语义在类型擦除后不可见，恒等 map 不可判定；
  结构性改写对五形状零收益。与 H2 正交。
- **P1 补全（类型一致/环拒绝）**：同上，不阻塞判决。
- 入口指针恒等缓存（`resolve_publish_ctx` 的 thread_local {owner,key,快照,epoch}
  缓存）惠及两条路径，epoch 机制保证跨接线轮次/录制武装的一致性。

## 被否决的备选

| 备选 | 否决理由 |
|---|---|
| `.gen.cc` 内联算子函数体（D4 原文） | 类型墙：IR 类型擦除、fn 藏于 wire 闭包，产物 TU 不可见 |
| LLVM JIT / 产物侧自带后端 | ADR-0030 D1 已否（依赖治理、可审计性）；类型墙同样存在 |
| DSL v1 表达式模板（类型级 stage 链，fn 可直呼） | ADR-0001 已否（编译时间爆炸、错误信息灾难）；一次只变一个变量 |
| join 输入用单槽（非环） | 配对前多消息积累会丢血缘（第二次到达覆盖第一次）；固定环 + 原子 head/tail |
| 全图无差别 inbox | op/stateful/span/from 的 handle 可能并发触发，破坏单写者前提；按生产者纪律分类 |
| 运行时 mode 开关（不产 .gen.cc，纯 runtime 特化） | 丢掉产物可审计性与缓存键锚点；codegen 编排成本低，保留 ADR-0030 架构 |
| SLA flow 也走特化 + 旁路直方图 | 复刻 thread_local born 戳逻辑两份，H1 风险大于收益；v0 全通用 |

## 风险与开放问题

- **单写者纪律是前提**：同通道并发双写者（用户绕过声明图手动双驱动）在 inbox
  上是数据竞争——v0 语义本身（消息交错）也不定义该场景；文档明示并发契约。
- **脏机测量不判决**：锁/原子路径在负载下不成比例劣化（load 3 时 compiled/gold
  1.3-2.5×，空闲预期 <1.1×）；一切数字以 §6.1 盾内三轮为准。
- **短形状预算紧**：入口（publish_bytes 的上下文解析 + 计数器 + dispatch）与金标准
  裸 dispatch 的差在 short 上占预算大头；若盾内判决 short 超门槛，重做方向是入口
  进一步薄化（产物导出类型化发布入口），不是放宽判据。
- fan-in join 的单飞锁在双生产者线程高频交叠时的公平性未测（H2 装置单线程驱动）。
