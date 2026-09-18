# 天枢架构文档（活文档 · Living Architecture Docs）

> **文档定位**：**as-built 系统参考**——描述"代码现在是什么样"，随每次迭代同步更新。
> **与 ADR 的分工**：[ADR](../adr/README.md) 是**不可变的历史决策快照**（当时为什么这么定、否了什么）；本目录是**可变的现状描述**（现在长什么样、怎么用）。决策变了写新 ADR，现状变了改这里。
> **维护契约**：见下方[维护契约](#维护契约每次迭代必须执行)——**改代码不更新本文档 = 迭代未完成**。
> **最后同步**：2026-09-18

---

## 阅读顺序

1. [01-architecture.md](./01-architecture.md) — 总体架构：分层视图、模块地图（目录 ↔ 模块 ↔ 状态）、一条消息的生命周期
2. [modules/](./modules/) — 每模块一页：职责边界 / 公共 API / 内部设计 / 决策史 / 测试入口
3. 需要了解"为什么"时 → [adr/README.md](../adr/README.md) 按域检索
4. 上手写代码 → [05-dsl-getting-started](../05-dsl-getting-started.md) → 跑 [examples/](../../examples/)

## 模块地图与状态

| 模块 | 代码位置 | 状态（2026-09-18） | 文档 |
|---|---|---|---|
| 基础原语库 | `tianshu/include/tianshu/base/` | ✅ 已实现（Phase 1，100% 函数覆盖） | [modules/base.md](./modules/base.md) |
| 调度器 | `tianshu/include/tianshu/sched/` | ✅ 已实现（回调式，无协程） | [modules/sched.md](./modules/sched.md) |
| 运行时核心 | `tianshu/include/tianshu/core/` | ✅ 已实现 | [modules/core.md](./modules/core.md) |
| 传输层 | `tianshu/include/tianshu/{transport,shm}/` | ✅ INTRA + SHM + kAuto；跨机 Zenoh 未实现 | [modules/transport.md](./modules/transport.md) |
| 声明式 DSL 与血缘 | `tianshu/include/tianshu/dsl/` | ✅ DSL v0 + 血缘 + 切片 + 状态 + record v2 + fallback 阶梯 v0 + M-C 特化安装（ADR-0032） | [modules/dsl.md](./modules/dsl.md) |
| SLA 编译 | `tianshu/include/tianshu/sla/` | ✅ v0 + H3 判定 PASS（校准回路，ADR-0038） | [modules/sla.md](./modules/sla.md) |
| L1 编译器 | `tianshu/include/tianshu/compiler/` | ✅ Phase 1 全量（M-A~M-D 闭环 + H2 PASS；P2 optimize pass descope——ADR-0032 D6） | [modules/compiler.md](./modules/compiler.md) |
| ti CLI 家族 | `tianshu/cli/` | ✅ ti / ti-launch / ti-monitor / ti-info / ti-compile | [modules/cli.md](./modules/cli.md) |

> 状态图例：✅ 已实现 · 🟡 部分实现 · 📐 设计就绪（仅 ADR，无代码）

## 模块页统一模板

每页固定七节，保证任何模块都能按同样的方式查阅：

1. **职责与边界**——做什么 / 明确不做什么
2. **公共 API 速览**——关键类型表 + 真实用法片段
3. **内部设计**——Mermaid 图 + 机制要点
4. **与其它模块的关系**——依赖 / 被依赖
5. **设计决策与被否方案**——ADR 摘要链接
6. **测试 / 基准 / 示例入口**
7. **已知限制与演进方向**

新建模块时，从任意现有模块页复制结构起步。

## 图规范

- 架构图统一用 **Mermaid**（```mermaid 围栏），GitHub 原生渲染、纯文本可 diff、AI 可维护。
- 允许的类型：`flowchart`（结构/分层）、`sequenceDiagram`（时序/协议）、`stateDiagram-v2`（状态机）。
- 节点标签含 `()` `:` `/` 等特殊字符时必须加引号。
- `docs/03`–`05` 叙事文档中的手绘 SVG 不迁移、不妨碍本规范。

---

## 维护契约（每次迭代必须执行）

**对象**：人类贡献者与 AI 编码代理一视同仁。
**时点**：迭代收尾、提交/合并之前。
**原则**：`git diff --name-only` 里每个 `tianshu/` 下的改动目录，都必须能在下表找到对应的文档动作（或明确确认无需更新）。

| 你改了什么 | 必须更新 |
|---|---|
| 模块公共 API（新增 / 修改 / 删除类型或函数签名） | `modules/<m>.md` §2；影响机制时连带 §3 |
| 模块内部机制、数据结构、并发/内存语义 | `modules/<m>.md` §3；跨模块时连带 `01-architecture.md` 对应图 |
| 新增 ADR | [adr/README.md](../adr/README.md) 索引表 + 受影响 `modules/<m>.md` §5 |
| 新增 / 迁移示例、测试、基准入口 | `modules/<m>.md` §6 |
| 模块实现状态变化（📐→🟡→✅） | 本页状态表 + `modules/<m>.md` 头部状态行 + `README.md` 特性表（若用户可见） |
| 新特性 / 行为变化 | `CHANGELOG.md` Unreleased 段 |
| Phase / 里程碑变化 | `00-overview.md` §8、`01-roadmap.md` 当前进度、`README.md` 项目状态表 |
| 每次任何同步 | 所改文档头部「状态 / 最后同步」行 |

### 收尾检查清单

- [ ] `git diff --name-only` 对照上表逐目录过一遍
- [ ] 新 ADR 已入 `adr/README.md` 索引（ADR 自身的既有纪律）
- [ ] 状态表与代码实际状态一致（没有"文档说✅代码其实没有"）
- [ ] 文档内相对链接全部可达

### 为什么强制

ADR 体系已经很好地保住了"决策不丢"；活文档要保的是"**现状不失真**"。文档与代码漂移一天，后来者（包括未来的自己和 AI）就要多付一天"从代码反推设计"的成本。语义正确性无法用 CI 断言，只能靠把同步动作写进迭代流程本身。
