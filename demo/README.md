# PoC Demo — 一条命令走完声明式闭环

> **用途**：Phase 1 PoC 的可复现演示——声明 flow → 加载期 trace + SLA 判定 → 编译为 `.so` → 运行编译产物 → 录制/回放逐字节一致 → 逐消息血缘 → WCET 校准建议。
> **跑法**：仓库根目录 `bash demo/poc-demo.sh`（先 `cmake --preset=desktop-release && cmake --build --preset=desktop-release`）。
> **录像**：`PACE` 控制节拍（默认 1.5s，`PACE=3` 更从容；`PACE=0` 无停顿）。asciinema 用法见下。

## 六拍与对应的框架承诺

| 拍 | 命令（脚本内） | 演示什么 | 依据 |
|---|---|---|---|
| 0 | `ti`（无参） | 统一入口按 PATH 发现 `ti-*` 动词（launch/monitor/compile/info） | [ADR-0002](../docs/adr/0002-cyber-relation.md) |
| 1 | `traceable_flow_demo` | 单二进制全闭环：注册表枚举 → 按名 dry-run trace（**build 即 trace**）→ SLA 报告（`sla report: OK` + 预算分摊）→ artifact hash → 编译 → 运行编译产物，sink 内打印逐消息血缘 | [ADR-0030 M-D](../docs/adr/0030-l1-compiler.md) |
| 2 | `ti compile demo_traceable --flows <provider.so> --emit-source` | 离线编译 CLI：provider `.so` 经 dlopen 填充进程内注册表；打印 valid/degraded、产物路径、缓存命中、审计三件套（`.so`/`.dag`/`.conf`）；`--emit-source` 直接展示生成的 `.gen.cc`（M-C 特化安装代码） | [ADR-0030 M-B](../docs/adr/0030-l1-compiler.md) · [ADR-0032](../docs/adr/0032-mc-specialization.md) |
| 3 | `ti launch demo_traceable --flows <provider.so>` | flow 名装载：dry-run → 编译 → 安装编译产物运行至 SIGINT（脚本 5 秒后发 SIGINT，优雅停机 rc 0） | [ADR-0030 M-D](../docs/adr/0030-l1-compiler.md) |
| 4 | `record_replay_demo` | record v2：在线运行落盘，离线回放输出**逐字节一致**（39/39 bit-identical） | [ADR-0028](../docs/adr/0028-record-format-v1.md) |
| 5 | `ti info <record.trec> --messages 2 --lineage` | 免链接发布者代码读记录：摘要 + 逐通道统计 + 消息预览带血缘 describe | [ADR-0028](../docs/adr/0028-record-format-v1.md) |
| 6 | `ti info <record.trec> --calibrate` | H3 校准回路：从血缘时间戳取每 stage in-context p99.9，建议 `1.3×p99.9` 回填 `with_wcet()`；声明偏离 >3× 报 DRIFT（rc 3） | [ADR-0029](../docs/adr/0029-sla-compilation.md) · [ADR-0038](../docs/adr/0038-h3-verification-semantics.md) |

配套物：`examples/traceable_flow_provider.cc`（demo 专用 provider `.so`，与 `traceable_flow_demo` 共享 `traceable_flow_decls.h` 的同一份 flow 声明）。

## 已知 v0 行为（录像时不必惊讶）

- 第 3 拍中 sink 每 5 秒重打 tick 0-2：flow 模式的 `run_sources` 窗口会重触发 bootstrap 钩子（[cli.md §3.4](../docs/arch/modules/cli.md) 已记录的 v0 限制）。
- 第 2 拍显示 `cache: hit` 属正常——第 1 拍已把同 hash 产物编译进默认缓存目录 `build/tianshu-gen/`。

## 录像（asciinema）

```bash
# 终端录像（生成 demo/poc-demo.cast）
asciinema rec --overwrite -c "bash demo/poc-demo.sh" demo/poc-demo.cast

# 转 GIF（需要 agg）；或上传 asciinema.org 后嵌 player
agg demo/poc-demo.cast demo/poc-demo.gif
```

OBS 录屏版本：另开窗口展示 `examples/traceable_flow_decls.h` 源码（第 1 拍前），讲解词按上表"演示什么"列展开；性能判决数字（H2 五形状 ≤0、H3 ε≤9.8%）引用 [docs/06-benchmarks.md](../docs/06-benchmarks.md)。
