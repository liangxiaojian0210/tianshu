# ADR-0035：算子直呼 —— specialize 钩子的 per-fn 实例化

- **状态**：已接受（设计；实施排期下一轮）
- **日期**：2026-09-17
- **决策者**：Pride Leong
- **关联**：[adr/0032](./0032-per-message-specialization.md)（D1 类型墙、双闭包纪律） · [adr/0034](./0034-gate-semantics-v3.md)（k 预算与本篇联动重论证） · [adr/0030](./0030-l1-compiler.md)

---

## 需求清单

| # | 需求 | 来源 |
|---|---|---|
| 1 | k（每跳开销）的最大单项是算子间接调用：2-5ns/跳（间接跳转本身 + 内联与连锁优化失效） | R1/R2 归因（r1-rounds/attr、r2-rounds/attr-shares） |
| 2 | 类型墙约束：Flow 是运行时值（IR），只能以 `std::function` 携带算子 → IR 下游任何调用点都看不到算子体 | ADR-0032 D1 |

## 决策

### D1：机制 —— 类型知识在 FlowBuilder 调用点使用，而非穿过 IR

specialize 钩子工厂模板化为 `make_map_specialize<TIn, TOut, F>`（F = 算子 functor
的**原始类型**，在 `FlowBuilder::map` 调用点编译期已知）；钩子闭包按值捕获 F。
`FastMapStage` / `FastJoinStage` / `FastSinkStage` 增加 F 模板参数，notify 内
`f_(*msg)` 成为对已知类型对象的直呼——编译器可完全内联算子体并做跨调用边优化。
现有 `raw_fn_` 路径（无捕获函数指针提取）保留，作为非模板化安装的退路。

### D2：API 波及面

`FlowChain::map` 现以 `std::function<TOut(const T&)>` **按值**收参（flow.h:538）——
类型擦除正是发生在这个边界。改为 `template <typename TOut, typename F> map(F fn)`
（转发引用），`map_stream` / `join` / `sink` 同步改造（双闭包纪律：flow.h 声明 +
dsl_runtime.h 定义两边同步）。`MapDecl` 存两个闭包：`wire`（内含 `std::function`
拷贝，供通用安装与解释执行）+ `specialize`（捕获 F，供 per-fn 实例化）——**IR
结构不变**。

### D3：产物契约不变

`.gen.cc` 仍按声明序调用 specialize 钩子；per-fn 实例化发生在钩子内部，产物无需
感知类型——延续 ADR-0032 D1 的「产物只做常量编排」。稳定哈希不覆盖闭包载荷 →
**缓存键不变**，既有产物不失效。

### D4：代码膨胀预算

每个（消息类型 × 算子调用点）实例化一份 stage（notify + fan 展开，约数百字节）；
感知规模流 ~百级算子 → 增量 ~几十 KB 量级，可接受；宿主 TU 编译时间影响轻微。

### D5：地板重论证（联动 ADR-0034 D1）

fn 间接消除后，每跳地板 = 收件环原子协议 + 通道 seq ≈ 2-3ns → **k 预算随本篇
落地重校**（预期降至 ~1ns 级）；进一步压缩需「单写者免原子槽」（后继 ADR，线性
通道 push/pop 同栈可免原子）。

### D6：H1 不变

特化安装的行为逐字节等价由 `specialize_test`（9 用例）与 `source_entry_test`
（5 用例）继续锁定；本篇只改调用形式，不改任何可观察语义。

## 后果

- 正面：k 从 6-8ns/跳 压向 2ns/跳以内（fn 间接 2-5ns 消除）；算子体与框架代码
  跨边界优化（寄存器分配、常量传播）恢复；门槛地板重论证后预算更紧。
- 负面/风险：DSL 公开 API（`map`/`join`/`sink` 参数类型）变化——现有用户代码以
  lambda 传参不受影响（模板推导兼容），显式传 `std::function` 的调用需验证；
  模板实例化增加编译时间与二进制体积（D4 预算内）。
- 后续：实施 + 实测（预言：线性形状同轮差值每跳降 2-5ns）；随后「单写者免原子槽」
  ADR。
