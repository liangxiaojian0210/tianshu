# ADR-0033：类型化源入口 —— 入口段特化与判决口径统一

- **状态**：已接受
- **日期**：2026-09-17
- **决策者**：Pride Leong
- **关联**：[adr/0032](./0032-per-message-specialization.md)（入口是 M-C 的最后一段通用段） · [adr/0030](./0030-l1-compiler.md)（D5 装置） · [adr/0021](./0021-dsl-v0.md)

---

## 需求清单

| # | 需求 | 来源 |
|---|---|---|
| 1 | 消除入口段（`publish_bytes` 通用段）在编译路径上的固定开销：上下文哈希/快照查找、每通道 push 锁、SLA 空探测；fan-in 形状三通道轮转使单槽缓存全失效，放大至 ~190ns/消息 | H2 R1 perf 归因（r1-rounds/attr/） |
| 2 | H1 不回退：入口经特化发布后，输出、血缘、seq、录制文件与通用路径逐字节一致 | ADR-0032 需求 2 同款 |
| 3 | 判决口径统一为生产现实态，消除对基准排列顺序的依赖 | R1 归因发现 glibc 单线程闩锁污染 |

## 决策

### D1：类型化源入口（宿主侧构造，产物中立）

`dsl::SourceEntry`：对**流内声明的源通道**构造的直发入口。构造时经
`FlowRuntime::bind_source_entry(flow, channel)` 一次性解析该通道的安装期常量
（通道 id、历史环指针、消费者槽位表——与 `SpecializeBuilder::finalize()` 冻结
`FanBinding` 完全同构），此后每条消息的发布是一条直线：

```
录制已武装?  → 回落 rt.publish_bytes（与 fast 段 fan 同一回落纪律）
历史有观察者? → HistoryRing::push（拷贝）
扇出        → 各消费者 LineageChannel::push（末槽 move）
分发        → DataDispatcher::dispatch(常量 id)
```

跳过：字符串哈希与单槽内容快照缓存、`pub_ctx_` 原子快照装载（含 shared_ptr
引用计数加减）、每通道 push 锁（源通道单写者由声明保证，见 D2）、SLA 探测。
消息类型经模板方法擦除为字节（`publish(msg, seq)` 内联为
`publish_bytes(&msg, sizeof(T), Lineage::rooted(channel, seq))`），产物侧无需
感知类型——延续 ADR-0032 D1 的类型墙纪律：类型知识留在宿主实例化点。

### D2：资格条件复用通道计划

入口特化仅当：流未声明 SLA 端点（否则全图保持通用，与 `fast_fan` 同一条件）；
且该通道**恰有一个生产者且种类为 source**（声明即单写者；多源/tap 通道不特化，
入口整体回落通用发布）。资格由 `bind_source_entry` 从 Flow 自身推导，无 IR 双源。

### D3：历史捕获收窄与 fast 段一致

入口的历史捕获采用 ADR-0032 的收窄规则（仅流声明了观察者的通道捕获——span
数据通道 / stateful 状态通道），而非通用路径的"无观察者则全捕获"。特化安装下
入口与每跳行为一致；通用路径（解释执行）行为不变。

### D4：回落纪律与生命周期

录制武装（`recording_active()`）时入口逐消息回落 `publish_bytes`，record 文件
字节一致（与 fast 段相同）。绑定指针的生命周期与 fast 段扇出绑定相同：有效至
下一次 wiring 轮；录制武装由运行时标志逐消息检查覆盖。

### D5：H2 装置的驱动角色对称

基准 compiled 路径的驱动循环改用 `SourceEntry`（线性一入口、fan-in 三入口），
与 handwritten 金标准直接调 `dispatch` 的驱动角色对称；解释执行保持
`publish_bytes`（解释器本身就是通用段）。生产接线（`SourceDecl::drive` 换用
入口、`ti launch` 装配）为 M-D/launcher 后续项，本 ADR 不实现。

### D6：判决口径 v2 —— 生产现实态、全基准同态

基准二进制启动即创建并回收一个线程（thread-state warmer），一次性翻转 glibc
`__libc_single_threaded`，使全进程 `std::mutex` 与 shared_ptr 引用计数进入原子
路径——生产宿主（源线程、观察线程）的常态。金标准、解释、编译三方在同一状态
下测量，判决不再依赖基准注册顺序。§6.1 协议与基线表随之重定。

## 后果

- 正面：入口固定项在编译路径上消除（线性形状 ~30ns/条、fan-in ~190ns/条，见
  §6.1 v2 基线实测）；判决口径自洽且可复现；run_sources install-only 不再创建
  线程（单线程宿主保持单线程语义）。
- 负面/代价：入口与通用路径的历史捕获语义在收窄规则下分叉（无观察者时特化不
  捕获，`rt.history()` 在特化安装下为空）——与 fast 段既有分叉一致，已由
  `specialize_test` 锁定；入口绑定是裸指针快照，rewire 前必须重建。
- 后续：生产接线（D5）、fan-in join 快路径与每跳残余（重做队列下两项）。
