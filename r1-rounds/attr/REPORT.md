# H2 R1 perf 归因记录（2026-09-17）

> 证据目录：盾内复现三轮、五形状 compiled/handwritten(poisoned) perf 采样、
> 二分实验与 gdb 取证输出。perf.data 原始文件留在 /tmp/opencode/h2-attr/（一次性），
> 符号份额表在 perf-symbol-shares.txt。采集驱动脚本 h2_perf.sh（盾三件套 + sudo perf
> stat/record，盾外记录器）。工具建议后续迁 tools/。

## 判据与结果

| 判据 | 结果 |
|---|---|
| A 复现 R1（±15ns） | PASS：盾内 compiled 五形状 181/441/842-851/822/490-491 vs R1 181/441/852/821/490 |
| B 入口逐符号 | publish_impl + HistoryRing::push + resolve_publish_ctx + ctx.push_lock 钉死；~40ns 残差标注（阈值下符号 + IPC 偏差） |
| C 每跳分解 | 符号级完成：dispatch+fill/fetch+FastStage lambda+LineageInbox+lineage/string，每跳绝对组件 5-9ns 基本线性 |
| D fan-in 分解 | PASS：入口放大 ~186ns（resolve 抖动 114 + shared_ptr refcount 72）+ join 机器 ~96ns + 3×history/push_lock |
| E medium/long 反常 | 解释：diff/hops 除法伪影；compiled 每跳线性，非线性来自 gold 侧每跳成本增长与固定入口分摊 |

## 新发现：测量装置缺陷（glibc 单线程闩锁污染）

- `FlowRuntime::run_sources` 对 duration=0（install-only）也无条件 spawn source 线程
  （dsl_runtime.cc:614-618）。线程瞬起瞬 join，但 pthread_create 已将 glibc
  `__libc_single_threaded` 永久置 0。
- 此后全进程 `CacheBuffer::std::mutex`（fill_bytes/try_fetch 每跳各一对）从非原子
  快路径（plain load/store，libc 0x98e39-41）掉入 `lock cmpxchg` 原子路径（0x98e01）
  + 原子 unlock（0x9a752-4 采样热点）。
- 证据：gdb bt 钉死 fill_bytes→pthread_mutex_lock；annotate 99.61% 样本在 cmpxchg
  失败分支；二分实验只有 compiled 前驱污染 gold（211→280 medium）；/proc task 全程
  单线程（无泄漏线程，闩锁单向）。
- gold 污染量（隔离态 vs 污染态 p50）：short 80→90、medium 211→280、long 521→651、
  fanin 291→351、fanout 240→350。
- 对 R1 判决的影响：全量运行 bench#3（cmp/short）起全进程 poisoned。medium/long/
  fanin/fanout 双方同态，diff 有效；short 的 gold（bench#1 干净）vs compiled
  （poisoned）为跨态比较——数值上两侧污染量相近，diff 仍 ~101。

## 状态匹配门差值（poisoned vs poisoned，每形状单进程隔离）

| 形状 | gold | compiled | diff | ns/跳(÷hops+1) | R1 记录 |
|---|---|---|---|---|---|
| short | 90 | 191 | 101 | 50.5 | 101 |
| medium | 280 | 441 | 161 | 32.2 | 170 |
| long | 651 | 822 | 171 | 17.1 | 210 |
| fan-in | 351 | 802 | 451 | 90.2 | 460 |
| fan-out | 350 | 480 | 130 | 65.0 | 139 |

FAIL 判决不变（门槛 ≤2ns/跳，仍差一个数量级）。

## compiled 绝对账（cycle-share × p50，ns/msg）

| 组件 | short | medium | long | fan-in | fan-out |
|---|---|---|---|---|---|
| mutex lock+unlockslow | 86 | 191 | 360 | 284 | 246 |
| dispatch | 17 | 63 | 128 | 55 | 46 |
| entry 通用段 | 27 | 28 | 30 | 142 | 25 |
| LineageInbox | 4 | 32 | 50 | 25 | 22 |
| FastStage 本体 | 6 | 37 | 67 | 21 | 4 |
| lineage(SmallVec/merge) | 7 | 22 | 55 | 57 | 16 |
| string | 10 | 21 | 59 | 20 | 39 |
| clock | 13 | 16 | 14 | 15 | 27 |
| other/残差 | 11 | 16 | 31 | 151* | 40 |

*fan-in other 含 shared_ptr refcount 原子 ~72ns（PtrCache miss 换入换出）与
  匿名 join 机器符号；resolve_publish_ctx 14.3%（~114ns）已计入 entry 通用段。

## fan-in 病理排序（+451ns 的构成）

1. **入口放大 ~186ns**：3 源通道轮转发布 vs thread_local 单槽 PtrCache → 每次发布
   全 miss（snapshot hash find + 48B 快照重建）+ shared_ptr ctx 换入换出的原子
   refcount（~72ns）。
2. 3× 入口通用段全价（history push ×3 + push_lock ×3）。
3. join 机器 ~96ns（FastJoinStage 21 + inbox 25 + merge lineage ~50）。

## 回炉方向（按数据收益排序，待拍板）

1. 入口整段特化（产物导出类型化发布入口，绕过 publish_bytes 通用段）
   —— short/medium/long 直接砍 ~100ns 固定项；fan-in 连同 1、2 项 ~280ns。
2. run_sources 0ms 不 spawn（装置修复，一行为变更）——判决回到干净态可比。
3. PtrCache 多槽化（4 槽开放寻址）——不做 1 时单独砍 fan-in 186ns 放大。
4. fan 去虚化 / join 快路径重做 —— 中等收益（join 本体 96ns、每跳 7-9ns）。
5. CacheBuffer std::mutex 取舍 —— 两侧共享非门差值来源，但绝对成本最大单头
   （long 360ns/msg），L4 层议题。

## 附录：perf-stat 顶层账（2026-09-17 补采，判据 E 计数器证据）

> 采集：`h2_perf.sh stat`（盾内，min_time=10s，perf stat -d -p PID）。两处方法学注记：
> ① perf `-x,` CSV 的 value 列**已按多路复用预换算**，首版解析再除 coverage 导致绝对量
> ×2.38 虚高（比值类不受影响），下表为修正值；② stat 窗口含编译/排序等消息外阶段
> （占比 ~15-35%，随形状），绝对量仅供量级与边际对比，比值类稳健。

| 运行 | cyc/msg | IPC | br/msg | brmiss 率 | L1d miss/msg | fe_idle |
|---|---|---|---|---|---|---|
| c-short | 1394 | 3.66 | 1010 | 0.21% | 12 | 22% |
| c-medium | 2896 | 4.07 | 2257 | 0.10% | 44 | 17% |
| c-long | 5292 | 4.04 | 4221 | 0.09% | **67** | 15% |
| c-fanin | 4853 | 3.66 | 3705 | 0.12% | 42 | 21% |
| c-fanout | 4083 | 3.57 | 2956 | 0.35% | 51 | 21% |
| h-short | 675 | 3.85 | 576 | 0.26% | 2 | 26% |
| h-medium | 1606 | 4.60 | 1567 | 0.11% | 2 | 16% |
| h-long | 3791 | 4.53 | 3661 | 0.17% | **2.5** | 14% |
| h-fanin | 1871 | 4.49 | 1878 | 0.18% | 2 | 20% |
| h-fanout | 2451 | 3.91 | 2153 | 0.43% | 10 | 27% |

### S1 差距构成 = 更多的工作，不是 IPC 塌缩或分支误预测

compiled IPC 比gold 低 ~10%（3.6-4.1 vs 3.9-4.6），分支数多 15-44%/消息；两侧
brmiss 率都 ≤0.43%——差距来自每消息执行与访存的绝对增量（原子锁、环、血缘拷贝），
不是预测失效。IPC 锚点交叉验证：h-medium 1606cyc@5.16GHz=311ns vs 迭代均值
295ns（+5%）、c-short 270 vs 254ns（+6%）——主报告 cycle-share→ns 换算保真度
约 ±10-15%，原 IPC 偏差警示有界。

### S2 L1-dcache miss 每消息 5-27×（结构性访存差）

compiled long ~67 vs gold ~2.5 misses/msg。通道名 SSO 假设**证伪**（flow.h:503/505
命名 = "cmp4/src" 8 字符 / "cmp4/~N"，全部 SSO；cfree 采样 ~0.5% 亦证每消息无堆）。
主嫌疑 = **每跳旋转载体**：gold 血缘走单槽 slot（每消息同一行，L1 常驻）；compiled
走 16 深定容 LineageInbox 环 + 入口 HistoryRing，轮转触冷行；SmallVec 分支拷贝与
fan-in shared_ptr 控制块叠加。按 ~14cyc/miss 估潜在停顿贡献 ~170ns/msg（long），
与主账 mutex/lineage/string 组件部分重叠（同一操作的 miss 与停顿），不重复计入。

### S3 compiled 边际每跳平坦、gold 边际每跳上涨（判据 E 计数器证据）

cycles 边际：compiled (5292−2896)/5=479 vs (2896−1394)/3=501 cyc/hop ≈ 93/97ns
@5.16GHz——平坦；gold (3791−1606)/5=437→85ns/hop vs (1606−675)/3=310→60ns/hop
——上涨 +42%。与主报告结论一致：diff/hops 的"medium 每跳贵过 long"伪影根源在
gold 侧非线性（深度越深每跳越贵），compiled 侧线性。注意含每迭代开销污染
（排序 ~60ns/msg 对称存在，c-* 另含编译摊销）。
