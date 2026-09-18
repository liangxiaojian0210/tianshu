# ADR-0005：轻架构与多端多平台

- **状态**：已接受
- **日期**：2026-08-10
- **决策者**：Pride Leong
- **关联**：[00-overview.md](../00-overview.md) · [adr/0001](./0001-dsl-form.md) · [adr/0002](./0002-cyber-relation.md) · [adr/0003](./0003-build-system.md) · [adr/0004](./0004-build-entry.md) · [02-development-plan.md](../02-development-plan.md)

---

## 背景

天枢的初始定位是"自动驾驶车端 ECU"（见 [00-overview.md](../00-overview.md)）。本决策把目标域**扩展**到：

- 自动驾驶车端 ECU（ORIN / J5 / MDC / Intel ADL）
- 机器人控制器（ROS 替代场景，但跨多核 ARM）
- 工业 PC 与边缘 AI Box（Jetson / RK3588 / Amlogic）
- 嵌入式 Linux 设备（Cortex-A53/55/72 单核或双核）
- **MCU 级嵌入式**（Cortex-M7/M33，运行 FreeRTOS / Zephyr / RT-Thread）
- 服务器（云端训练 / 仿真 / replay）

这要求天枢**主动设计轻量化与可裁剪性**，而不是事后再"瘦身"。每个依赖、每行代码都要回答两个问题：

1. **能不能不要它？**（用 C++ 标准库 / 自研原语替代）
2. **能不能裁掉它？**（编译期条件，profile 不需要则不进二进制）

## 候选方案

### 方案 1：全功能优先，事后裁剪

先做车端 + 服务器版本，后期需要嵌入式时再做裁剪 fork。

**优点**：起步快，不增加早期复杂度。
**缺点**：到嵌入式时大量重写；依赖膨胀难收敛；天枢的"零运行时开销"承诺会被依赖拖累。

### 方案 2：完全嵌入式优先，性能场景后扩

从 MCU 起步，逐步扩到车端 / 服务器。

**优点**：架构先天轻；二进制小。
**缺点**：放弃车端 ORIN 性能优势；DSL / trace / 编译器在 MCU 上跑不动；PoC 期无法验证 H1/H2/H3。

### 方案 3：分层 profile，编译期裁剪（**已选**）

定义 5 个 profile（desktop / vehicle / embedded / mcu / server），通过 `--config=<profile>` 在编译期选择启用的模块和依赖。**核心 DSL / Trace / Compiler 只在 hosted 平台启用；MCU 走受限子集 + 离线编译模式**。

**优点**：单一仓库多端覆盖；依赖按 profile 引入；早期 PoC 在 vehicle 上验证，扩展到嵌入式是渐进路径。
**缺点**：需要 OS 抽象层（OSAL）+ 硬件抽象层（HAL）；CI 矩阵扩大。

## 决策

**选方案 3**：分层 profile + 编译期裁剪 + OSAL/HAL 抽象。

## 目标平台矩阵

| Profile | 硬件 | OS | CPU 核 | 内存 | 二进制上限 | 典型场景 |
|---|---|---|---|---|---|---|
| `desktop` | x86_64 / aarch64（开发机） | Linux / macOS | 8+ | 16GB+ | 无限制 | 开发、调试、单元测试 |
| `server` | x86_64（云服务器） | Linux | 16+ | 64GB+ | 无限制 | 训练、仿真、replay、batch |
| `vehicle` | aarch64（ORIN / J5） / x86_64（ADL） | Linux / QNX | 8-12 | 8-32GB | < 50MB | 车端实时控制（**核心目标域**） |
| `embedded` | ARM Cortex-A53/55/72 | Embedded Linux / RT-Linux | 1-4 | 256MB-1GB | < 10MB | 机器人控制器、边缘 Box、工业 PC |
| `mcu` | ARM Cortex-M7/M33、RISC-V RV32/RV64 | FreeRTOS / Zephyr / RT-Thread / bare-metal | 1 | 256KB-1MB RAM | < 1MB | 传感器节点、底层执行器、安全 MCU |

## 依赖治理原则

### 总则

> **每一个第三方依赖必须经过 ADR 审批**。无 ADR 的依赖 CI 直接拒绝。

依赖分四档：

| 档位 | 含义 | 准入条件 |
|---|---|---|
| 🟢 **允许** | C++ 标准库 / 单 header / 公认轻量 | 可直接用 |
| 🟡 **限制** | 部分功能引入，需挑选用 | ADR 说明挑哪些、用哪些、为什么 |
| 🟠 **审查** | 体积大或耦合深，必须评审 | 全员评审 + 替代方案对比 |
| 🔴 **禁用** | 不允许引入 | 无例外 |

### 🔴 禁用清单（🔴）

> 多语言绑定约束（详见 [adr/0007](./0007-api-spec-multi-language.md)）：所有公开 API 类型必须**可跨 FFI 边界**，禁用 C++ 模板/异常/STL 暴露。

| 依赖 | 理由 |
|---|---|
| **Boost**（除 `boost.context` 单 header 可考虑） | 编译时间爆炸、二进制膨胀、跨 profile 兼容差、跨语言不友好 |
| **Qt** / **glog** / **gflags** | 重 + 平台耦合；用自研 logger / absl flags / std::optional 配置替代；Qt 跨语言绑定成本高 |
| **Protobuf heavy runtime** | 用 `proto-lite` runtime；反射 / JSON 互通功能不引入（跨语言 SDK 用 lite 即可） |
| **任何依赖 ICU / iconv 的库** | 嵌入式不友好；天枢内部仅 UTF-8 |
| **任何依赖 pthread 二进制特定行为的库** | FreeRTOS / Zephyr 没有 pthread；走 OSAL |
| **Java / JVM / Python runtime**（运行时） | 不能跑在 MCU；DSL 改用 C++；Python/Java 仅作 SDK（详见 [adr/0007](./0007-api-spec-multi-language.md)） |
| **C++ 特性暴露在公开 API**（模板/异常/RTTI/STL） | 跨 FFI 不安全；公开 API 走 C ABI 不透明 handle |

### 限制清单（🟡）

| 依赖 | 允许范围 | 禁止范围 |
|---|---|---|
| **Abseil** | `<absl/strings>`, `<absl/synchronization>`, `<absl/time>`, `<absl/base>` | `<absl/container>` 重型节点、`<absl/flags>`（仅在 desktop/server） |
| **Protobuf** | `.proto` 定义 + lite runtime + codegen | 反射、JSON、TextFormat 全套 |
| **GoogleTest / GoogleMock** | 仅 `tests/`，binary 不链接 | 任何 production 代码 |
| **GoogleBenchmark** | 仅 `benchmarks/`，binary 不链接 | 同上 |
| **fmt** | 仅 header-only，可用 `std::format`（C++20）替代时优先 std | fmt::format 的 locale 功能 |

### 鼓励清单（🟢）

| 选项 | 备注 |
|---|---|
| **C++ 标准库**（`<chrono>`, `<coroutine>`, `<concepts>`, `<ranges>`, `<span>`, `<format>`, `<expected>`） | 优先使用 |
| **自研原语**（ObjectPool / CacheBuffer / AtomicHashMap） | 见 L4-PRIM |
| **单 header 库**（如 `gsl-lite`, `spdlog` 的 header-only 子集） | 仅当 header-only 且 < 5000 行 |
| **POSIX.1-2008** | 通过 OSAL 调用，不直接用 |

### 依赖治理流程

1. **申请**：提 PR + ADR 草稿（依赖名 / 用途 / 体积估算 / 替代方案对比）
2. **评审**：架构 review + 嵌入式可行性 review
3. **准入**：合入 ADR + 加入 `ALLOWED_DEPS.txt`
4. **CI 守护**：CI 跑 `bazel query deps` / `ldd` 对比白名单，新增未审批依赖直接 fail

## OSAL · OS 抽象层

天枢不直接调 POSIX / FreeRTOS API，统一走 OSAL：

```
tianshu/osal/
├── thread.hpp        # 线程（posix thread / freertos task）
├── mutex.hpp         # 互斥（pthread_mutex / freertos semaphore）
├── condvar.hpp       # 条件变量
├── time.hpp          # 时钟（CLOCK_MONOTONIC / freertos ticks）
├── shm.hpp           # 共享内存（posix shm / freertos stream buffer）
├── socket.hpp        # 网络（bsd socket / lwip / disabled in mcu）
├── file.hpp          # 文件 IO（disabled in mcu）
├── signal.hpp        # 信号（disabled in mcu）
└── env.hpp           # 环境变量（disabled in mcu）
```

每个 OSAL 接口有多 backend 实现：

| OSAL 接口 | POSIX backend | FreeRTOS backend | QNX backend | bare-metal backend |
|---|---|---|---|---|
| thread | pthread | xTaskCreate | pthread | 单线程退化（noop） |
| mutex | pthread_mutex | xSemaphoreCreateMutex | pthread_mutex | 关中断 |
| condvar | pthread_cond | task notification | pthread_cond | busy-poll |
| shm | shm_open / mmap | stream buffer | shm_open | disabled |
| socket | bsd socket | lwip socket | bsd socket | disabled |
| file | std::fstream | FATFS | std::fstream | disabled |

profile 启用哪些 OSAL backend 在编译期决定。

## HAL · 硬件抽象层

```
tianshu/hal/
├── cpu.hpp           # CPU 拓扑（核数 / 大小核 / NUMA）
├── cpuset.hpp        # CPU 亲和（sched_setaffinity / task affinity）
├── cache.hpp         # 缓存层级（L1/L2/L3 大小、line size）
├── pmu.hpp           # 性能计数器（optional）
├── gpu.hpp           # GPU 资源（CUDA / OpenCL / NPU，一类公民，详见 [adr/0006](./0006-gpu-acceleration.md)）
├── thermal.hpp       # 温度传感器（thermal zone）
└── power.hpp         # 电源管理（cpufreq / DVFS）
```

HAL 让 SLA 调度器在不同硬件上自动适配（L3-DERIVE 用）。GPU 是**一类公民**（不是 optional），见 [adr/0006](./0006-gpu-acceleration.md)。

## 编译模式

| 模式 | 标志 | 适用 profile | 备注 |
|---|---|---|---|
| `hosted-debug` | `-O0 -g` | desktop | 全功能 + 调试 |
| `hosted-release` | `-O3 -DNDEBUG` | server / vehicle / embedded | 全功能 + 优化 |
| `hosted-sanitizer` | `-fsanitize=...` | desktop / server | 全功能 + sanitizer |
| `freestanding-debug` | `-O0 -ffreestanding` | mcu（仿真） | 无 OS 依赖 |
| `freestanding-release` | `-Os -ffreestanding -fno-exceptions -fno-rtti` | mcu | 最小二进制 |

MCU profile 默认走 `freestanding-*`，禁止异常 / RTTI / 重 STL。

## 各 Profile 启用模块矩阵

| 模块 | desktop | server | vehicle | embedded | mcu |
|---|---|---|---|---|---|
| L1 DSL fluent builder | ✅ | ✅ | ✅ | ⚠️（受限子集） | ❌ |
| L1 Trace 引擎 | ✅ | ✅ | ✅ | ⚠️ | ❌ |
| L1 Compiler/Codegen | ✅ | ✅ | ✅ | ❌（离线编译） | ❌ |
| L1 DSL `with_gpu_backend`（详见 [adr/0006](./0006-gpu-acceleration.md)） | ✅ | ✅ | ✅ | ⚠️（仅 GPU SoC） | ❌ |
| L2 血缘 | ✅ | ✅ | ✅ | ⚠️（轻量） | ⚠️（最小 ring buffer） |
| L2 血缘 GPU 张量引用（cudaIpcMemHandle） | ✅ | ✅ | ✅ | ⚠️ | ❌ |
| L2 容错 SYNC_REPLAY | ✅ | ✅ | ✅ | ✅ | ✅ |
| L2 容错 ASYNC_REPLAY | ✅ | ✅ | ✅ | ⚠️ | ❌ |
| L2 容错 DEGRADE（含 GPU OOM 降级到 CPU） | ✅ | ✅ | ✅ | ✅ | ⚠️ |
| L3 SLA / RTA（CPU） | ✅ | ✅ | ✅ | ⚠️（简化） | ❌ |
| L3 GPU SLA / GPU RTA | ✅ | ✅ | ✅ | ❌ | ❌ |
| L3 跨加载进程全局视图 | ✅ | ✅ | ✅ | ❌ | ❌ |
| L4 Transport INTRA | ✅ | ✅ | ✅ | ✅ | ✅ |
| L4 Transport SHM | ✅ | ✅ | ✅ | ⚠️ | ❌ |
| L4 Transport RTPS / 跨机 | ✅ | ✅ | ⚠️ | ❌ | ❌ |
| L4 Coroutine | ✅ | ✅ | ✅ | ⚠️（ucontext） | ❌（纯回调） |
| L4 Scheduler Classic | ✅ | ✅ | ✅ | ✅（单核退化） | ⚠️（协作式） |
| L4 Scheduler Choreography | ✅ | ✅ | ✅ | ❌ | ❌ |
| L4 Service Discovery | ✅ | ✅ | ✅ | ⚠️（同机） | ❌（静态配置） |
| L4 反压传播 | ✅ | ✅ | ✅ | ✅ | ✅ |
| **L4 GPU 资源管理（设备/stream/内存池/IPC）**（详见 [adr/0006](./0006-gpu-acceleration.md)） | ✅ | ✅ | ✅ | ⚠️（lite） | ❌ |
| X 构建确定性 | ✅ | ✅ | ✅ | ❌ | ❌ |
| X 执行确定性 | ✅ | ✅ | ✅ | ⚠️ | ⚠️ |
| X 血缘追溯 | ✅ | ✅ | ✅ | ⚠️ | ❌ |
| T tianshu-ctl | ✅ | ✅ | ✅ | ❌ | ❌ |
| T 可视化 | ✅ | ✅ | ⚠️ | ❌ | ❌ |

✅ 完整支持 / ⚠️ 受限子集 / ❌ 不支持

## MCU profile 的特殊路径

MCU（Cortex-M）由于资源限制（256KB-1MB Flash，无 MMU，无 Linux），天枢走**离线编译模式**：

1. 在 desktop/server 上跑 trace + 编译，生成 C++ 源码
2. 用嵌入式工具链（arm-none-eabi-gcc）编译为 MCU 固件
3. MCU 上**只跑** L4 INTRA + 最小血缘 + 反压 + 容错（SYNC_REPLAY）
4. DSL 在 MCU 上**不暴露**给用户，用户操作的是固件 API

这意味着：

- MCU 用户**不写 flow 函数**，而是配置预编译的 flow 固件
- DSL 是 vehicle/desktop 用户的开发工具，MCU 是 deployment target
- 用户的 flow 函数 trace + 编译产物可同时生成 vehicle .so 和 mcu .elf

## 影响与新增框架

### INFRA 新增框架

| 框架 | 作用 |
|---|---|
| F-INFRA-DEPS | 依赖治理（白名单 / CI 守护 / 准入流程） |
| F-INFRA-OSAL | OS 抽象层（POSIX / FreeRTOS / QNX / bare-metal backends） |
| F-INFRA-HAL | 硬件抽象层（CPU / 缓存 / 热传感） |
| F-INFRA-PROFILE | profile 配置体系（与 ADR-0004 的 `--config=<name>` 集成） |

### L4 各框架的影响

| 框架 | 新增功能点 |
|---|---|
| L4-TRANS | 嵌入式 transport（仅 INTRA）、MCU stream buffer backend |
| L4-CORO | MCU profile 禁用协程（纯回调模式） |
| L4-SCHED | 单核协作式调度器（embedded/mcu） |
| L4-SD | 静态拓扑配置（mcu 不做运行时发现） |
| L4-CORE | profile 条件编译（disable 不用的 API） |

### ADR-0003/0004 的影响

- ADR-0003：构建矩阵新增 5 个 profile × 2 个构建系统
- ADR-0004：`--config=<profile>` 扩展（`--config=mcu` 启用 freestanding 编译）

## 资源预算（一阶目标）

| Profile | 二进制 | 常驻内存 | 启动时间 |
|---|---|---|---|
| desktop | 不限 | 不限 | < 5s |
| server | 不限 | 不限 | < 5s |
| vehicle | < 50MB | < 256MB | < 1s |
| embedded | < 10MB | < 32MB | < 500ms |
| mcu | < 1MB（含固件） | < 256KB | < 50ms |

每个 profile 在 CI 跑资源占用回归（`size` / `valgrind massif` / 启动 hook）。

## 风险与缓解

| 风险 | 缓解 |
|---|---|
| MCU profile 工作量超预期 | 优先级降到 Phase 3 之后；Phase 1 只覆盖 desktop/vehicle |
| OSAL 抽象成本（性能损耗） | 关键路径用 `inline` + 模板编译期分发；MCU 用宏直接到 backend |
| 依赖治理流程拖慢开发 | 预定义常见依赖白名单（如 fmt / spdlog header-only） |
| 双构建系统 × 5 profile 矩阵爆炸 | CI 分层：desktop/server 跑完整 / vehicle 跑核心 / embedded/mcu 只跑构建不跑运行 |
| 双平台代码漂移（hosted vs freestanding） | profile 条件编译必须用统一宏（`TIANSHU_PROFILE_DESKTOP` 等） |

## 后续可能演进

- 如果 MCU profile 实际需求强烈 → 单独出 `tianshu-mcu` 子项目，共享 OSAL/HAL 接口但独立演进
- 如果 QNX / VxWorks 强需求 → 单独 ADR 决定商业 RTOS 支持
- 如果 RISC-V 生态成熟 → 加入 `--config=riscv` profile
- 如果 WebAssembly 需求（如浏览器跑 demo） → 加 `--config=wasm` profile

## 参考

- C++ freestanding 编译：https://en.cppreference.com/w/cpp/freestanding
- FreeRTOS 任务与同步原语：https://www.freertos.org/
- Zephyr RTOS 文档：https://docs.zephyrproject.org/
- 嵌入式 C++ 规范（MISRA C++ / AUTOSAR C++14）：行业约束参考
