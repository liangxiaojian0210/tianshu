# 天枢 (TIANSHU) — 基准与假设验证数据

> **文档定位**：公开基准数据页——Phase 1 三假设（H1/H2/H3）的判定数字、原始证据位置与复现入口。
> **维护者**：Pride Leong
> **状态**：v1.0（2026-09-22）· 三假设全绿（均 2026-09-18 正式判定）
> **关联**：[01-roadmap.md](./01-roadmap.md) · [arch/modules/compiler.md §6.1/§7](./arch/modules/compiler.md) · [h3-rounds/REPORT.md](../h3-rounds/REPORT.md)

---

## 1. 三假设判定总览

| # | 假设 | 判定 | 一句话结果 |
|---|---|---|---|
| H1 | trace 捕获所有数据流操作 | ✅ PASS（2026-09-18） | 特化安装与通用 wiring **逐字节等价**（值 + 血缘 describe），14 用例锁定 |
| H2 | 编译产物性能对标手写 | ✅ PASS（2026-09-18） | 五形状**同轮差值（compiled − handwritten）全部 ≤0**——编译产物达到或反超同语义手写装配 |
| H3 | WCET 校准具备预测力 | ✅ PASS（2026-09-18） | 两轮预测力协议：7 stage 硬门全过，环境漂移 ε ≤ 9.80%（门 10%） |

## 2. 测量环境与纪律

- **宿主**：异构 AMD Ryzen AI 9 HX 370（逻辑 0-3/12-15 = Zen5 大核 5.16GHz；其余 Zen5c 小核）。
- **盾（shield）**：cgroup v2 保留核 + `user.slice`/`system.slice` 切片排挤 + performance governor；异构机上必须绑大核（绑小核全部数字无声放大 ~1.8×）。
- **同态协议**：基准二进制启动即做 thread-state warmer，三方（手写/解释/编译）同态测量，判决不依赖注册顺序（ADR-0033 D6）。
- **绝对数字是宿主相关的**；判决语义（差值符号、比值、预算常数）才是可迁移的结论。完整协议见 [arch/modules/compiler.md §6.1](./arch/modules/compiler.md)。

## 3. H2 — 编译产物 vs 手写（门语义 v3，ADR-0034）

**主承诺（加性）**：`diff ≤ F + k×跳数 + m×扇出支数`，预算 F ≤ 20ns/消息、k ≤ 2ns/跳。
**判决数字**（2026-09-18 空闲窗三轮，`r2-rounds/verdict-*`，同轮差值 = compiled − handwritten，单位 ns，负数 = 编译侧更快）：

| 形状 | p50 差值（三轮） | p99 差值 |
|---|---|---|
| 短链 A→B | **-1 / -1 / -1**（一字不差） | 全负 |
| 中链 A→…→E | -40 ~ -50 | 全负 |
| 长链 A→…→J | -110 ~ -111 | 全负 |
| 扇入 (A,B,C)→D | **-3817 ~ -5841** | 除一轮 +3318（已归档的调度尾环境项）外全负 |
| 扇出 A→(B,C,D,E) | -20 ~ -30 | 全负 |

- 加性预算以 ≤0 全部满足；负载条件化百分比从承诺（W ≥ (F+k×hops)/1% 时 P99 差 <1%）在任意 W 下平凡成立。
- **扇入大幅领先的语义**：三方每源一线程等价驱动（ADR-0037）下，框架 join 机器（原子 inbox + 单飞锁，不睡眠）对合理手写同步（std::mutex，竞争塌缩）是真实优势，盾内 compiled 371ns vs gold 4338ns（快 11.7×）。
- 线性形状平价的机制：per-fn 直呼（ADR-0035）+ 直槽就地访问（ADR-0036）+ 入口特化（ADR-0033）消除声明层中转开销。
- 迭代全程每轮预言对账：R1 五形状 +101~+460 全 FAIL → 归因 → 六轮定向优化 → ≤0；H1 等价用例全程锁定未破。

**复现**：`tools/h2_shield.sh`（§6.1 协议实现，trap 保证还原）。

## 4. H1 — trace 完备性（逐字节等价）

`tests/dsl/specialize_test.cc`：五形状 / 多生产者 / 混合 op / SLA 回退 / 历史收窄 / 录制内容 / 回放共 **14 用例**，`wire_specialized` 与 `wire` 的输出（值 + 血缘 describe 字节）逐一比对。

```bash
./build/desktop/bin/specialize_test   # 或 ctest -R specialize
```

## 5. H3 — WCET 校准预测力（ADR-0038）

两轮协议：Round A 标定 ×3 → 声明 `1.3×A_med` → Round B 新进程实测 ×3；硬门 `B_r ≤ 1.3×A_med`，环境漂移 ε = (max−min)/median ≤ 10%。

| stage | A_med (µs) | B_med (µs) | ε | 判定 |
|---|---|---|---|---|
| h3/micro/m1 | 170.6 | 175.9 | 9.80% | PASS |
| h3/micro/m2 | 234.1 | 238.9 | 7.30% | PASS |
| h3/milli/m3 | 950.4 | 998.8 | 7.71% | PASS |
| h3/milli/m4 | 4762.1 | 4864.5 | 4.18% | PASS |
| h3/fanin/a | 59.2 | 63.3 | 9.16% | PASS |
| h3/fanin/b | 559.3 | 582.4 | 7.35% | PASS |
| h3/fanin/j | 143.3 | 152.1 | 6.70% | PASS |

全部 7 stage 直接 PASS（B_med ≥ A_med）；漂移告警可机判：`ti-info --calibrate --wcet <向量>` 偏离建议值 >3× → DRIFT-HIGH/LOW，rc 3（4× 演示见 `h3-rounds/drift-demo.txt`）。

**完整报告（含声明向量、SLA 报告、异常观察、复现命令）**：[h3-rounds/REPORT.md](../h3-rounds/REPORT.md)。

## 6. 其他实测数据（desktop-release）

| 项 | 实测 | 出处 |
|---|---|---|
| 跨进程 SHM 吞吐 | **4.17M msg/s**（验收门 ≥1M），RTT p50 = 58µs（门 <1ms） | [shm-transport-notes](./development/shm-transport-notes.md) |
| 血缘开销 | 建根 21ns / 单跳链 51ns/消息 / 移动 0.2ns | [arch/modules/dsl.md](./arch/modules/dsl.md) |
| 编译 vs 解释执行 | 快 3.2-4.3×（R1 时期同轮测量，机制收益参照） | [arch/modules/compiler.md §7](./arch/modules/compiler.md) |

## 7. 证据索引

| 假设 | 原始证据 | 判定脚本 |
|---|---|---|
| H1 | `tests/dsl/specialize_test.cc`（可复现） | ctest |
| H2 | `r2-rounds/`（verdict-1/2/3 + 全程迭代档案） | `tools/h2_shield.sh` |
| H3 | `h3-rounds/`（run-a*/run-b* + REPORT.md） | `h3-rounds/h3_verdict.sh` |
