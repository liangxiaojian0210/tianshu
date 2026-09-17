# ADR-0036：单写者直槽 —— 线性通道的免原子血缘交接

- **状态**：已接受（2026-09-17 实施）
- **日期**：2026-09-17
- **决策者**：Pride Leong
- **关联**：[adr/0032](./0032-per-message-specialization.md)（通道计划与 inbox 并发纪律） · [adr/0034](./0034-gate-semantics-v3.md)（k 预算） · [adr/0035](./0035-per-fn-instantiation.md)（前一刀：fn 直呼后 k≈4.2ns/跳）

---

## 需求清单

| # | 需求 | 来源 |
|---|---|---|
| 1 | k（每跳开销）剩余主项 = 收件环协议：head/tail 原子访问（禁寄存器缓存）+ 运行期取模索引，实测 ~2-3ns/跳 | R2 归因（r2-rounds/attr-shares）；k 预算 ≤2ns（ADR-0034） |
| 2 | 上一轮「深度 1 但保留原子协议」的尝试预言未中回滚——证明开销在协议本身而非环的局部性 | 2026-09-17 C1 回滚记录 |

## 决策

### D1：直槽 = 普通成员单槽，仅限线性通道

`detail::DirectSlot`：`push` 赋值 / `pop` move-out，无环、无原子、无取模——与手写装配的单槽同形。合法性依据既有并发纪律（ADR-0032 推导链）：**同步级联里 push 与其配对 pop 发生在同一调用栈**（发布 → dispatch → 消费者回调弹出），深度恒 ≤1，第二个线程不可能触碰该槽。占用时 push（纪律下不可能发生）覆盖旧值，与 inbox 丢旧语义同构。

### D2：槽型三分，由通道计划判别

`register_specialized_slot(channel, kind)`，`kind ∈ {kLockedQueue, kAtomicInbox, kDirectSlot}`：

| 消费者 | 槽型 | 理由 |
|---|---|---|
| fast map / fast sink（线性） | **DirectSlot** | 同栈配对 |
| fast join 输入 | LineageInbox（原子环） | **跨线程配对**：A 线程 push 输入 a，配对触发可能在 B 线程栈弹出——原子是语义必需 |
| 通用段（op/stateful/span/from）、多生产者、tap | LineageQueue（锁） | 写者纪律未知或已知并发 |

判别经 `SpecializeBuilder::linear_slot / join_slot` 从 Flow 自推导，无 IR 双源。

### D3：H1 不变

push/pop 配对与内容逐字节不变；录制回落路径（publish_bytes → ctx.queues → 任意槽型）接口兼容。既有 14 个特化等价用例（specialize_test 9 + source_entry_test 5）继续锁定。

## 后果

- 正面：线性通道每跳收件协议开销（原子对 + 取模）清零；k 预期 4.2 → ~1-2ns/跳（实测见 compiler.md §7）。
- 负面/风险：直槽的正确性依赖「同步级联同栈」纪律——未来引入异步投递或消费者线程化时，线性槽必须随通道计划重新判别（本 ADR 的排除表是判别依据）；单槽无积累能力，任何破坏深度 ≤1 的语义变更（如批量投递）不得使用直槽。
- 与 ADR-0032 C1 尝试的区别：C1 是「深度 1 的原子环」（预言未中，回滚）；本篇是「免原子」——针对的根因（协议开销）与 C1 针对的假设（缓存局部性）不同。
