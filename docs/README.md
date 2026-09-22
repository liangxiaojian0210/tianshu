# 天枢文档导览

> **文档定位**：docs/ 全树入口——不同读者按不同路径进入。
> 上层入口：[README.md](../README.md)（项目门面）。

---

## 三条阅读路径

| 你是谁 | 路径 |
|---|---|
| **5 分钟了解项目** | [README.md](../README.md) → [00-overview.md](./00-overview.md) → [01-roadmap.md](./01-roadmap.md) → [06-benchmarks.md](./06-benchmarks.md) |
| **学习 / 参与开发**（人或 AI） | [00-overview.md](./00-overview.md) → [**arch/01-architecture.md**](./arch/01-architecture.md)（总体架构 + 模块地图）→ [arch/modules/](./arch/modules/)（目标模块深读）→ [05-dsl-getting-started.md](./05-dsl-getting-started.md)（动手）→ 跑 [examples/](../examples/) |
| **查"当时为什么这么定"** | [adr/README.md](./adr/README.md)（按域检索）→ 具体 ADR |

## 目录结构

```
docs/
├── README.md                  ← 本导览
├── 00-overview.md             方案总览：定位 / 核心矛盾 / 分层愿景 / 核心假设（叙事）
├── 01-roadmap.md              Phase 0-3 路线图 + 风险登记（叙事）
├── 02-development-plan.md     功能点三级拆解（L4-xxx / F-xxx issue 粒度）（叙事）
├── 03-full-chain-demo.md      全链路闭环 Demo 讲解（叙事）
├── 04-data-model-generalization.md  数据模型一般化里程碑（叙事）
├── 05-dsl-getting-started.md  DSL 入门教程（叙事）
├── 06-benchmarks.md           基准与假设验证数据（H1/H2/H3 判定 + 证据索引）
├── arch/                      ★ 活文档（as-built 现状，每次迭代同步）
│   ├── README.md              架构文档导航 + 模块状态表 + 维护契约
│   ├── 01-architecture.md     总体架构：分层 / 模块地图 / 消息生命周期
│   └── modules/*.md           每模块一页（七节固定结构）
├── adr/                       架构决策记录（不可变快照，30+ 篇）
│   ├── README.md              索引 + 域视图
│   └── templates/adr-template.md
├── development/               工程实现笔记（SHM 陷阱 / 覆盖率管线）
└── evaluation/                选型评估（跨机传输 / ForkSHM / Console）
```

## 两类文档的分工

| | 叙事系列（00–05） | arch/ 活文档 | ADR |
|---|---|---|---|
| 回答 | "项目要做什么、演进到哪一步" | "代码现在长什么样、怎么用" | "当时为什么这么定、否了什么" |
| 时态 | 未来 + 现在 | **现在** | 过去（快照） |
| 更新方式 | 里程碑驱动 | **每次迭代强制同步**（见 [arch/README.md 维护契约](./arch/README.md#维护契约每次迭代必须执行)） | 只增不改（修订写新篇） |

## 贡献者 / AI 代理须知

改代码前先读目标模块的 `arch/modules/<m>.md`；改完后**必须**按 [arch/README.md 的维护契约](./arch/README.md#维护契约每次迭代必须执行)同步文档——这是本仓库迭代的收尾步骤，不是可选项。
