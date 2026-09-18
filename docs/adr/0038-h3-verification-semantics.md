# ADR-0038：H3 验证语义 —— 两轮预测力协议与漂移比对自动化

- **状态**：已接受（2026-09-18 实施并判定 PASS）
- **日期**：2026-09-18
- **决策者**：Pride Leong
- **关联**：[adr/0029](./0029-sla-compilation.md)（H3 目标窗与校准回路） · [adr/0028](./0028-record-format-v1.md)（校准数据源 record v2） · [adr/0034](./0034-gate-semantics-v3.md)（判决环境有效性判据先例） · [adr/0021](./0021-dsl-v0.md)（rig 的 v0 同步级联语义）

---

## 需求清单

| # | 需求 | 来源 |
|---|---|---|
| 1 | H3（Phase 1 最后一个假设）需要可执行验证：估算 WCET 相对实测 P99.9 落在 [1.0, 1.3] 窗——低于实测不可接受（安全侧违规），高于 1.3× 不可用（过度保守） | roadmap §1.4 · ADR-0029 |
| 2 | 原方法（翻译 Apollo DAG + cyber_recorder 负载）依赖外部资产，本仓库不可复现——需要诚实等价的工作负载与协议 | 仓库现实（个人开源，无 Apollo 录制） |
| 3 | ADR-0029 Phase 1 里程碑要求偏差告警「可演示」；原实现只是 calibrate 输出尾部的静态提示文本，声明值不参与比对——需要机器可判定的告警路径 | ADR-0029 D2 · sla.md §7 已知限制 |
| 4 | 判定数字须在测量有效环境取得：异构 CPU + 宿主常态负载，沿用 H2 的盾协议与环境判据先例 | compiler.md §6.1 先例 |

## 决策

### D1：验证单位 = 每 stage 的 in-context P99.9（按输出通道索引）

不验证端到端延迟：`hop_cost`（默认 20µs）是文档化的保守常数（实测 INTRA 派发 <5µs），计入 H3 窗只会制造与结论无关的失败项。stage 时延 = `ts(out) − ts(倒数第二跳输入消息 ts)`，含排队、算子计算、发布与录制——与运行时语义一致。

### D2：两轮预测力协议（非循环）

- **Round A（标定）×3**：无声明跑 rig → `ti-info --calibrate` → 每 stage `A_med = median(p99.9)`；
- **声明**：`WCET = ceil_{0.1µs}(1.3 × A_med)`（即 ADR-0029 的建议公式）；
- **Round B（验证）×3**：新进程、同 rig、同屏蔽、带声明与 `with_sla` → 实测 `p99.9_B`；
- **判定（每 stage）**：硬门 `∀ B_r ≤ 1.3 × A_med`（逐轮无容差，违反即 FAIL-UNSAFE）；过门后 `B_med ≥ A_med` → PASS；`(A_med − B_med) ≤ ε × A_med` → PASS (tie)；否则 FAIL-CONSERVATIVE；
- **环境判据**：每 stage `ε = (max−min)/median` 跨六值 {A1..A3, B1..B3}，任一 ε > 10% → ENV-INVALID，不出判定（对齐 H2「判决数字须在有效环境取」的纪律；禁止重跑取数）。

语义：验证的是**校准回路的预测力**——上一轮标定的建议值能否安全（不乐观）且有用地约束下一轮实测。roadmap 兜底列的 profile-guided 校准由此提升为主路径；固定优先级 RTA 仍是 Phase 2（ADR-0029 D3 边界不变）。

### D3：工作负载 = 确定性自旋 rig（诚实等价）

`benchmarks/h3_wcet_rig.cc`：三分支七 stage（micro 50/120µs · milli 800µs/5ms · fanin 60/600/150µs join），固定迭代次数的整数混合核（累加器写入输出负载：不可死码消除、核内无计时调用、无新依赖）；常数一次 dry-run 定标后冻结。判定为比值型，绝对量级只需保持在目标 ~2× 内。屏蔽协议沿用 `tools/h2_shield.sh` 模式（`tools/h3_shield.sh`：performance governor + 切片排挤 + cgroup cpuset + enter-group-then-pin + trap 恢复）。

### D4：漂移比对自动化（`ti-info --wcet NAME=US[,...]`）

仅与 `--calibrate` 同用；calibrate 表增 `DECLARED(us)` / `RATIO(decl/suggest)` 列（未声明打 `-`）；`declared > 3 × suggest` → DRIFT-HIGH（过度保守）、`declared < suggest / 3` → DRIFT-LOW（不安全乐观）——ADR-0029 D2「偏差 >3×」的对称解读；任一 drift → **rc 3**；格式错 / 未知 stage / 缺 `--calibrate` → **rc 2**。不自动反填声明（v0 立场不变）。

### D5：录制开销 = 协议条件

LZ4 record v2 + 血缘录制在 A/B 两轮完全相同地存在，判定量为比值，不引入系统性偏差；该条件随验证报告归档。

## 后果与实测（2026-09-18，`h3-rounds/`）

- **判定 PASS（rc 0）**：7 stage 全部直接 PASS（B_med ≥ A_med，无一用到 tie 规则）；硬门全过；ε 最大 9.80%（h3/micro/m1，单核级联高频段尾部对环境最敏感），未触 10% 判据线。
- Round B calibrate RATIO 0.95–0.99，无 drift；声明向量全部落在 [B_med, 1.3×A_med] 内。
- ADR-0029 Phase 1 验收达成：4× 高报 → 7 条 DRIFT-HIGH + rc 3；窗口内声明 → rc 0（`h3-rounds/drift-demo*.txt`）。
- 证据与复现：`h3-rounds/REPORT.md`（环境 / 协议 / 常数表 / 判定原文 / 复现命令）；`.trec` 留 /tmp 不入库。
- Phase 1 三假设全绿（H1 逐字节等价 · H2 对标手写 · H3 校准预测力）；M1 剩余：`ti compile` CLI、`ti launch` dry-run 接线、demo 视频与 benchmark 公开。
- 测试面：`tests/cli/ti_info_calibrate_test.cc`（6 用例：rc 契约 0/2/3 + 表列输出）。
