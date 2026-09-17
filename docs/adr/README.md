# ADR 索引

> **编号规则**：ADR 编号是纯时间序流水号（Nygard 格式），动笔时分配，不预占位、不按域分段。
> 分类检索用本索引的域标签；ADR 正文互链靠编号稳定。
> 模板与流程见各 ADR 头部；新增 ADR 后必须更新本表。

## 状态图例

✅ 已接受 · 🔁 已被修订/部分取代（见备注） · ⏸ 已搁置

## 索引

| ADR | 标题 | 域 | 状态 | 一句话摘要 |
|---|---|---|---|---|
| [0001](./0001-dsl-form.md) | DSL 形式：fluent builder + 自动 trace | dsl | ✅ | 上层范式 JAX/torch.compile 风格，C++ 内嵌 DSL |
| [0002](./0002-cyber-relation.md) | 与 Cyber RT 的关系：独立重实现、API 兼容 | infra | ✅ | 零许可风险重写；L4 API 等价；含术语边界（Component/Operator、ti CLI 家族） |
| [0003](./0003-build-system.md) | 双构建系统 CMake + Bazel | build | ✅ | CMake 主、Bazel 验证，条目对齐 |
| [0004](./0004-build-entry.md) | 构建入口标准化：禁包装脚本 | build | ✅ | 原生 cmake/bazel 命令为唯一入口 |
| [0005](./0005-lightweight-multiplatform.md) | 轻量多平台：5 profile + 依赖治理 + OSAL/HAL | infra | ✅ | desktop→mcu 五档位，横切硬约束 |
| [0006](./0006-gpu-acceleration.md) | GPU 加速：设计就绪、实现 Phase 2/3 | gpu | ✅ | P0 降级，纯 CPU 链先验证 H1-H3 |
| [0007](./0007-api-spec-multi-language.md) | 多语言 SDK：C ABI + Python/Rust/Go/Node | infra | ✅ | 稳定 C ABI 为唯一契约面 |
| [0008](./0008-message-format-multi.md) | 消息格式多支持：FlatBuffers/Protobuf/POD | runtime | ✅ | MessageConcept 统一抽象，三格式自由混用 |
| [0009](./0009-doc-code-language.md) | 双语文档 + 英文注释/提交 | infra | ✅ | docs 双语、代码注释与提交英文 |
| [0010](./0010-transport-shm-infra.md) | Transport 抽象 + 通用 SHM Allocator + INTRA | runtime | ✅ | TransportBackend 统一接口；决策 5 增补 SHM channel 落地架构 |
| [0011](./0011-logging.md) | 结构化异步日志 | infra | ✅ | 无锁 MPSC <200ns 热路径，八级别多 sink |
| [0012](./0012-parameters.md) | 统一参数系统 | infra | ✅ | 四源优先级 + 热重载 + tianshu-ctl |
| [0013](./0013-cross-machine-transport.md) | 跨机传输：自研 SHM + Zenoh | runtime | ✅ | RTPS 出局；MCU 走 zenoh-pico |
| [0014](./0014-console.md) | Console 统一面板 | tooling | ✅ | TUI(ftxui)+Web(React)；实现 Phase 3；解析依赖 ADR-0020 |
| [0015](./0015-discovery-abstraction.md) | 服务发现抽象：DiscoveryBackend 插件化 | runtime | ✅ | Zenoh/Static/Multicast/Central 多后端 |
| [0016](./0016-config-format.md) | 配置格式：TOML 主 + YAML 兼容 + JSON 导出 | infra | ✅ | 三解析器独立 feature flag |
| [0017](./0017-license.md) | 许可证：Apache-2.0 | infra | ✅ | 专利授权 + 依赖兼容矩阵 + CI 检查 |
| [0018](./0018-cpp-style-guide.md) | C++ 风格指南 | build | ✅ | Google base + 100 列 + C++20，CI 零警告 |
| [0019](./0019-coroutine-strategy.md) | 协程策略：Phase 1 回调 / Phase 2 C++20 无栈 | runtime | ✅ | Phase 1 无协程（SLA 由线程调度保） |
| [0020](./0020-message-reflection-monitor.md) | 消息运行期反射与 Monitor 解析 | tooling | ✅ | schema 随通道分发 + DecoderRegistry + POD 字段表宏 |
| [0021](./0021-dsl-v0.md) | DSL v0：声明式图 API 与执行语义 | dsl | ✅ | fluent builder 记图 + 解释器同步级联；IR 字段即 FlowDecl |
| [0022](./0022-lineage-v0.md) | Lineage v0：数据血缘级联模型 | runtime | ✅ | root+hops 值对象；DSL map 自动追加 hop；旁路 FIFO v0 |
| [0023](./0023-kauto-transport-v0.md) | kAuto 自动选路 v0 | runtime | ✅ | 双发写端 + 读端注册表判定；顺序无关；无 discovery 依赖 |
| [0024](./0024-dsl-op-primitive.md) | DSL op 原语：自定义算子（端口 + 生命周期） | dsl | ✅ | 读写一体算子（底盘/执行器）；on_init 自举反馈环；血缘 map/source 同构；单一 from 引用原语 |
| [0025](./0025-from-component-reference.md) | from() 组件引用：DSL ↔ L4 合流 | dsl | ✅ | 三问三答（线程/生命周期/泵回）；输出通道注入；血缘边界修正为同步/异步；quiesce 静默 |
| [0026](./0026-slice-input-model.md) | 数据切片输入模型：触发器 + 有界历史查询 | dsl | ✅ | stage = trigger + fetch（map/join 为退化）；多父血缘 + 区间跳；在线/离线同 API |
| [0027](./0027-state-as-data-channel-taxonomy.md) | 状态即数据 + 通道分类学 | dsl | ✅ | 状态通道（版本/血缘/恢复）；有名=契约边界、匿名=可融合边界；算子是面 |
| [0028](./0028-record-format-v1.md) | Tianshu Record Format v2 | dsl | ✅ | 血缘入库+分块压缩+分片合并+消息索引+统计摘要+LZ4/ZSTD+POD/proto/fbs schema |
| [0029](./0029-sla-compilation.md) | SLA 编译 v0：加载期 deadline 验证与预算分配 | dsl | ✅ | 类型化 Sla+WCET 声明、链路确定性预算+饱和度准入两层分析、预算下行分摊、fail-fast |
| [0030](./0030-l1-compiler.md) | L1 编译器：六阶段管线与零开销 codegen | dsl | ✅ | 声明图即 IR、源码 codegen+系统编译器成 .so、缓存键=规范化哈希、H2 三方对比装置、M-A~M-D 里程碑 |
| [0031](./0031-fallback-degradation.md) | 降级阶梯：with_fallback 声明与运行期降级信号 | dsl | ✅ | 加载期校验 fallback 名、IR/.conf 携带、20ms 窗口 miss 检测、fallback_state() 信号（v0 不热切换） |
| [0032](./0032-per-message-specialization.md) | M-C 逐消息特化：类型化宿主钩子与 H2 门槛重校准 | dsl | ✅ | D4 类型墙修正、Flow 自推导通道计划、LineageInbox、历史捕获收窄、录制回落、门槛 max(1%, 2ns/跳) |
| [0034](./0034-gate-semantics-v3.md) | H2 门语义 v3：加性承诺与负载条件化百分比 | compiler | ✅ | 主承诺 diff≤F+k×跳+m×支（k≤2ns/F≤20ns/m待定）、从承诺 W≥(F+k·hops)/1% 时 <1%、判决双轨报告、加性实证 |
| [0035](./0035-per-fn-instantiation.md) | 算子直呼：specialize 钩子 per-fn 实例化 | dsl | ✅ | make_*_specialize<TIn,TOut,F>、FastStage 增 F 模板参、IR/产物契约不变、地板重论证联动 0034 |
| [0036](./0036-direct-slot.md) | 单写者直槽：线性通道免原子血缘交接 | dsl | ✅ | 同栈配对合法性、槽型三分（直槽/原子环/锁队列）、join 跨线程排除 |
| [0033](./0033-typed-source-entry.md) | 类型化源入口：入口段特化与判决口径统一 | dsl | ✅ | 宿主侧 SourceEntry、FanBinding 同构冻结、录制回落、历史收窄、协议 v2 线程预热 |

## 域视图

- **build**：0003 · 0004 · 0018
- **infra**：0002 · 0005 · 0007 · 0009 · 0011 · 0012 · 0016 · 0017
- **runtime**（transport/message/sched/lineage）：0008 · 0010 · 0013 · 0015 · 0019 · 0022 · 0023
- **dsl/compiler**：0001 · 0021 · 0024 · 0025 · 0026 · 0027 · 0028 · 0029 · 0030 · 0031 · 0032 · 0033 · 0034 · 0035 · 0036
- **gpu**：0006
- **tooling**（ti 家族/console/monitor）：0014 · 0020
