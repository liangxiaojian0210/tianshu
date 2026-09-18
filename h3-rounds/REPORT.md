# H3 WCET 验证报告（Round A/B，T7–T12）

> 判定脚本：`h3-rounds/h3_verdict.sh`（本目录）· 判定输出原文见 §8。
> **OVERALL: PASS（全部 7 个 stage PASS，所有 ε ≤ 10%）** —— rc 0。

---

## 1. 环境（Environment）

| 项 | 值 |
|---|---|
| 主机 | `noctis` · Linux 6.18.43-gentoo-dist x86_64 GNU/Linux |
| nproc | 24（异构 AMD，big-core 调度） |
| governor | 运行期间全部 24 CPU = `performance`（收尾恢复 `powersave`，trap 保证） |
| 屏蔽核 | 保留一个物理核的两个逻辑 CPU `3,15`；rig 先入 `tianshu-bench-h3` cgroup（cpuset=3,15）再钉到 `3`（enter-group-then-pin，h2 模式） |
| 切片排挤 | `user.slice` / `system.slice` AllowedCPUs 挤出 3,15（其余 22 CPU）；结束后恢复 0-23 |
| 运行期负载 | loadavg ≈ 6.6–9.3（宿主后台负载，已被 cpuset 挤出保留核；每轮 preamble 原文存档于 run-*.txt） |
| 构建产物 | `build/desktop-release/bin/h3_wcet_rig`、`.../ti-info`（2026-09-18 16:00/16:01 构建） |
| 时间窗口 | 2026-09-18 16:06–16:24 (+08:00)，warmup + 6 轮 |

每轮输出文件首部均有 env preamble（date / host / nproc / loadavg / governor / 屏蔽状态 / 命令行原文），此处不再重复。

## 2. 协议摘要（Protocol）

两轮预测力检验（two-round predictive power，ADR-0029 H3）：

1. **Round A（标定）**：全默认参数（micro 200000 / milli 12000 / fanin 40000 每源）跑 3 轮，`ti-info --calibrate` 从血缘时间戳取每 stage 的 in-context 端到端 p99.9。
2. **声明**：`declaration = ceil_{0.1µs}(1.3 × median(p99.9_A1..A3))`（T9 表见 §5）。
3. **Round B（验证）**：同一 rig、同一屏蔽、同一默认参数，加 `--declare <vector> --sla` 跑 3 轮；硬门 = 每个 B_r ≤ 1.3 × A_med。
4. **判定**：per-stage ε = (max−min)/median 跨六个值 {A1..A3, B1..B3}；B_med ≥ A_med → PASS；回落在 ε·A_med 内 → PASS (tie)；更大回落 → FAIL-CONSERVATIVE；越硬门 → FAIL-UNSAFE；任一 stage ε > 10% → ENV-INVALID（不出 PASS/FAIL）。

- **度量单位** = 每 stage 的 in-context p99.9（µs），按输出通道名索引（`h3/micro/m1` 等 7 个）。
- **录制开销**（LZ4 record v2 + 血缘）在 A/B 两轮完全相同地存在，判定为比值型，不引入系统性偏差。
- 每轮一个 `.trec`（留在 `/tmp/opencode/`，不入库）；仓库只存文本档案。

## 3. 自旋常数表（rig 文件头，冻结值）

来自 `benchmarks/h3_wcet_rig.cc` 头部注释（2026-09-18 一次未屏蔽 dry-run 标定后冻结；判定为比值型，量级只需保持在 ~2x 内）：

| stage | target(us) | iters | realized p50(us) |
|---|---|---|---|
| m1 | 50 | 38000 | 47.1 |
| m2 | 120 | 90000 | 111.3 |
| m3 | 800 | 600000 | 745.6 |
| m4 | 5000 | 3750000 | 4656.2 |
| a | 60 | 45000 | 56.7 |
| b | 600 | 450000 | 561.1 |
| j | 150 | 112000 | 140.6 |

本次实测 p50（如 m1 46.3–46.7、m4 4535.6–4546.7）与冻结表一致，常数未漂移。

## 4. Round A（T8）

warmup：1 次未屏蔽全默认跑（页/路径缓存预热），`/tmp/opencode/h3warm.trec`，rig elapsed 142.618 s。
随后 3 轮屏蔽跑（每轮档案 = env preamble + rig stdout + 本轮 calibrate 表）：

| 轮 | rig elapsed(s) | shield wall(s) | rig rc / ti-info rc |
|---|---|---|---|
| a1 | 136.768 | 138 | 0 / 0 |
| a2 | 136.259 | 137 | 0 / 0 |
| a3 | 136.106 | 136 | 0 / 0 |

## 5. 声明向量（T9）

由三轮 run-a*.txt 的 calibrate 表计算（p99.9 µs；A_med = 三值中位数；declare = 1.3 × A_med 向上取整到 0.1µs）：

| stage | p99.9_A1 | p99.9_A2 | p99.9_A3 | A_med | 1.3×A_med | declare(us) |
|---|---|---|---|---|---|---|
| h3/micro/m1 | 174.8 | 170.6 | 169.5 | 170.6 | 221.78 | **221.8** |
| h3/micro/m2 | 238.4 | 234.1 | 233.0 | 234.1 | 304.33 | **304.4** |
| h3/milli/m3 | 950.4 | 940.2 | 952.9 | 950.4 | 1235.52 | **1235.6** |
| h3/milli/m4 | 4762.1 | 4756.4 | 4762.7 | 4762.1 | 6190.73 | **6190.8** |
| h3/fanin/a | 59.2 | 59.2 | 59.8 | 59.2 | 76.96 | **77.0** |
| h3/fanin/b | 569.2 | 559.3 | 557.5 | 559.3 | 727.09 | **727.1** |
| h3/fanin/j | 143.7 | 142.8 | 143.3 | 143.3 | 186.29 | **186.3** |

**声明向量**：

```
h3/micro/m1=221.8,h3/micro/m2=304.4,h3/milli/m3=1235.6,h3/milli/m4=6190.8,h3/fanin/a=77.0,h3/fanin/b=727.1,h3/fanin/j=186.3
```

## 6. Round B（T10）

3 轮屏蔽跑，`--declare <上述向量> --sla`（同一向量）；shield 对每轮记录再跑 `--calibrate --wcet <同一向量>`，表内 DECLARED/RATIO 列 RATIO = 0.95–0.99，无 drift：

| 轮 | rig elapsed(s) | shield wall(s) | rig rc / ti-info rc |
|---|---|---|---|
| b1 | 136.887 | 137 | 0 / 0 |
| b2 | 138.120 | 138 | 0 / 0 |
| b3 | 140.066 | 141 | 0 / 0 |

SLA 加载期报告（每轮 rig stdout 内含；b1 节选另存 `sla-report-b.txt`）：`sla report: OK`，预算分摊
m1 985us / m2 1354us / m3 4979us / m4 24960us / b 3344us / j 855us。

## 7. Drift 演示（T11，均用 b1 记录）

- `drift-demo.txt`：`--wcet` = 全部 7 stage 取 Round-B suggest × 4
  （`h3/micro/m1=914.8,h3/micro/m2=1242.4,h3/milli/m3=5064.4,h3/milli/m4=24935.2,h3/fanin/a=324.8,h3/fanin/b=2930.0,h3/fanin/j=759.6`）
  → 7 条 `[drift] ... ratio=4.00 -> DRIFT-HIGH (over-conservative >3x, ADR-0029 D2)`，**rc=3**。
- `drift-demo-ok.txt`：`--wcet` = 声明向量原值 → 0 条 drift，RATIO 0.95–0.99，**rc=0**。

（过程注记：drift-demo.txt 生成一次时 wrapper awk 误把通道汇总表行当 calibrate 行、混入未知 stage 得到 rc=2；修正解析范围后重新生成，与测量无关。）

## 8. 判定（T12，`bash h3-rounds/h3_verdict.sh` 原文输出，rc=0）

```
== H3 verdict (two-round predictive power, ADR-0029 H3) ==
stage                A1       A2       A3     A_med       B1       B2       B3     B_med     eps  verdict
h3/micro/m1       174.8    170.6    169.5     170.6    175.9    172.2    186.5     175.9   9.80%  PASS
h3/micro/m2       238.4    234.1    233.0     234.1    238.9    235.8    250.3     238.9   7.30%  PASS
h3/milli/m3       950.4    940.2    952.9     950.4    973.9    998.8   1014.5     998.8   7.71%  PASS
h3/milli/m4      4762.1   4756.4   4762.7    4762.1   4795.2   4864.5   4956.0    4864.5   4.18%  PASS
h3/fanin/a         59.2     59.2     59.8      59.2     62.5     63.3     64.8      63.3   9.16%  PASS
h3/fanin/b        569.2    559.3    557.5     559.3    563.5    599.1    582.4     582.4   7.35%  PASS
h3/fanin/j        143.7    142.8    143.3     143.3    146.0    152.5    152.1     152.1   6.70%  PASS

OVERALL: PASS (all 7 stages PASS or PASS (tie), all eps <= 10%)
```

全部 7 stage 均为直接 PASS（B_med ≥ A_med，非 tie）；硬门（B_r ≤ 1.3×A_med）全部满足；ε 最大 9.80%（h3/micro/m1），未触 10% ENV 线。

## 9. 复现命令（Reproduction）

```bash
# 0) 构建（原生入口，ADR-0004）
cmake --preset=desktop-release && cmake --build --preset=desktop-release

# 1) warmup（未屏蔽，全默认）
./build/desktop-release/bin/h3_wcet_rig --out /tmp/opencode/h3warm.trec

# 2) Round A ×3
tools/h3_shield.sh h3-rounds/run-a1.txt --out /tmp/opencode/h3-a1.trec   # a2/a3 同理

# 3) 由 run-a*.txt 计算 1.3×A_med 向量（见 §5 表），然后 Round B ×3
tools/h3_shield.sh h3-rounds/run-b1.txt --out /tmp/opencode/h3-b1.trec \
  --declare 'h3/micro/m1=221.8,h3/micro/m2=304.4,h3/milli/m3=1235.6,h3/milli/m4=6190.8,h3/fanin/a=77.0,h3/fanin/b=727.1,h3/fanin/j=186.3' --sla

# 4) drift 演示（b1 记录）
./build/desktop-release/bin/ti-info /tmp/opencode/h3-b1.trec --calibrate --wcet '<4x suggest 向量>'   # rc 3
./build/desktop-release/bin/ti-info /tmp/opencode/h3-b1.trec --calibrate --wcet '<声明向量>'          # rc 0

# 5) 判定
bash h3-rounds/h3_verdict.sh   # rc 0 PASS / 1 FAIL / 2 ENV-INVALID
```

## 10. 异常与观察（Anomalies）

- `h3/micro/m1` 的 p99.9（≈170–186µs）远高于 p50（≈46µs）：micro 支路 20 万消息级联中的排队/调度尾部，属 rig 语义内行为；A/B 两轮同构出现，比值判定不受影响。该 stage ε=9.80% 也是七者中最高——单核级联高频段的尾部对环境最敏感，但仍在门内。
- p99.9 高于 p99 明显（如 m1 49.5 → 174.8）：v0 单线程级联的固有长尾；WCET_suggest 本就取 p99.9×1.3 覆盖。
- 宿主后台 loadavg 6–9 一直存在，但被切片排挤出保留核；6 轮 rig elapsed 136.1–140.1s，稳定性 ~3%。
- `.trec`（每轮约 51MB）均留在 `/tmp/opencode/`，仓库只归档文本。
- 无 rig 崩溃、无 shield 失败；6 轮全部 rig rc=0、ti-info rc=0。

## 归档文件清单

```
tools/h3_shield.sh            # T7 屏蔽脚本（h2 模式，h3 rig 专用）
h3-rounds/run-a1..3.txt       # T8 Round A 档案
h3-rounds/run-b1..3.txt       # T10 Round B 档案（含 SLA 报告 + DECLARED/RATIO 列）
h3-rounds/sla-report-b.txt    # T10 SLA 报告节选（b1）
h3-rounds/drift-demo.txt      # T11 4x → DRIFT-HIGH ×7, rc=3
h3-rounds/drift-demo-ok.txt   # T11 声明向量 → 无 drift, rc=0
h3-rounds/h3_verdict.sh       # T12 判定脚本
h3-rounds/REPORT.md           # 本报告
```
