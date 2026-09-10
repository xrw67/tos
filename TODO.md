# tos 改版 TODO（对齐 Pallas Framework）

## 目的和边界

本文以 `/Users/xrw/code/cyanland/pallas-framework` 的公开头文件、API 合同和架构文档为功能来源，为 `tos` 制定渐进式改版清单。

- `tos` 当前基线：仅头文件 `Status` 与 `Result<T>`，GoogleTest、最小示例和三平台 CI 已具备。
- Pallas 当前基线：M0--M4 已完成；网络/IPC/Platform、扩展模块、示例文档和发布门禁仍缺原生 macOS/Windows 运行证据。因此它是功能与架构参考，不应被视为已经完成三平台发布验证的成品。
- 目标不是复制 Pallas 的源码、CMake target 名称或第三方依赖，而是在 `tos` 中建立等价且经过自身测试的公开合同。每个公共组件均须说明所有权、线程安全、错误、关闭/取消和平台差异。
- `tos::Status` 与 `tos::Result<T>` 是本项目唯一的错误/结果规范模型；保留当前 move-only 所有权、`Status` 表示无值操作结果、`Result<void>` 禁止的合同。不引入 `pallas::Error`、`pallas::Result` 或其公开别名。
- 本清单不纳入 GUI、完整 ORM、分布式服务治理、自研通用网络协议栈，以及未被实际应用场景证明需要的远程 Feature Flag、自动更新执行/重启/回滚。

## Pallas 功能清单与 tos 差距

| 能力域 | Pallas 的公开能力 | tos 状态 |
| --- | --- | --- |
| 错误与结果 | 结构化 `Error`（域、原生码、根因）、`Result<T>`/`Result<void>`、Abseil 兼容 | 已完成：保留 tos 自身的 `Status`/`Result<T>`；不移植 Pallas 错误模型 |
| Core | `Application`、`Runtime`、`Context`、`Module`、依赖 DAG、启动回滚和反向停止 | 未开始 |
| 通信 | 生命周期安全的 `ServiceRegistry`/handle、异步 FIFO `EventBus`、RAII 订阅和背压 | 未开始 |
| Task | `Executor`、有界 `ThreadPool`、future、取消、单调时钟 `Scheduler` | 未开始 |
| 配置与应用工具 | JSON/YAML、分层配置、类型/模式校验、reload、FeatureFlags、CLI 参数、环境读取 | 未开始 |
| 日志与诊断 | 同步/异步 logger、sink、轮转、结构化字段、诊断上下文 | 未开始 |
| Foundation 扩展 | `ByteSpan`、内存资源/内存池、时钟、JSON/二进制/Protobuf 序列化、OpenSSL 加密 | 未开始 |
| 可观测性 | Counter/Gauge/Histogram、Prometheus 文本、Health、trace/span、W3C 与可选 OpenTelemetry | 未开始 |
| Network | Boost.Asio TCP listener/socket、UDP、IPv4/IPv6、部分 I/O、超时和取消 | 未开始 |
| IPC | Unix domain socket、Windows named pipe、共享内存和跨进程锁 | 未开始 |
| Platform | UTF-8 path、系统/进程、文件读写与观察、终止信号、daemon/service host、崩溃报告 | 未开始 |
| 扩展模块 | HTTP client/server/health、SQLite 连接池、Filesystem、Process、签名软件更新 | 未开始 |
| 工程交付 | 独立 CMake targets、可选 feature、安装导出、`find_package`、sanitizer/fuzz/coverage、示例与合同测试 | 部分完成：接口 target、单测/示例/CI 已有；无安装包、组件 targets、质量预设或集成测试 |

## 实施顺序

### P0：先固定公共契约和构建骨架

- [ ] `P0-01` 编写 `docs/architecture.md` 和 `docs/api.md`：定义组件分层、允许依赖方向、所有权、线程安全、错误边界、三平台支持策略和语义化版本策略。
  - 验收：公开头独立包含；每个新增 API 在合同中有失败与生命周期说明。
- [x] `P0-02` 错误模型决策：保留现有 move-only `tos::Status` 与 `tos::Result<T>`；无返回值继续使用 `Status`，不支持 `Result<void>`，不引入 Pallas 的 `Error` 或 `Result`。
  - 验收：当前 Status/Result 测试和 README 中的移动、分配、值访问及失败路径合同继续成立；后续组件只能返回 tos 类型。
- [ ] `P0-03` 将单一 `tos` interface target 演进为按组件划分的 targets，并保留 `tos::tos` 聚合目标；禁止 Foundation/Task/Platform/Network/IPC 反向依赖 Runtime 或扩展。
  - 验收：配置期 target 依赖检查；minimal 与 full feature 图均可构建。
- [ ] `P0-04` 增加安装、导出与版本文件，支持 `find_package(tos CONFIG REQUIRED)`；保持 `add_subdirectory` 接入。
  - 验收：全新临时消费工程分别以两种方式构建并运行。
- [ ] `P0-05` 建立质量门禁：Debug/Release、ASan/UBSan、TSan（支持的平台）、clang-format、clang-tidy、coverage、fuzz 的 CMake presets；为 CTest 统一标签和超时。
  - 验收：本机与 CI 均执行相应矩阵，失败信息可定位到组件和测试。

### P1：完成 MVP 基础能力

- [x] `P1-01` 实现 `tos::time`：可注入 `IClock`、系统/手动时钟、单调耗时、UTC RFC3339 时间戳解析与格式化。
  - 验收：手动时钟使定时相关测试确定性；非法时间文本返回结构化错误。
- [x] `P1-02` 实现 `tos::config`：JSON/YAML 配置树、点路径类型化读取、必填/类型校验、合并与不可见部分更新的快照式 reload。
  - 验收：错误精确到路径；并发读取、格式错误、合并和 reload 一致性都有测试。
- [ ] `P1-03` 实现分层配置、环境变量和命令行：优先级固定为 defaults < file < environment < CLI；支持长短选项、重复值、位置参数、`--help`、`--version` 与来源追踪。
  - 验收：端到端示例验证覆盖顺序、未知参数和缺参失败，JSON 与 YAML 输入均有覆盖。
- [ ] `P1-04` 实现只读布尔 `FeatureFlags`，绑定配置中的固定前缀；不做远程同步、灰度或用户分群。
  - 验收：非法名称/类型、默认值和 reload 后立即可见均有测试。
- [ ] `P1-05` 实现线程安全 `Logger`：级别、控制台/文件 sink、格式化、flush，默认禁止输出密码、令牌等敏感配置；异步、有界队列、轮转作为第二阶段。
  - 验收：多线程写入不会损坏单条记录，文件失败可观察，关闭时已接收记录被刷新。
- [ ] `P1-06` 实现基础平台文件工具：UTF-8 路径适配、文本读写、目录/元数据查询和原子写入。
  - 验收：不存在、权限和替换失败返回明确错误；临时文件与资源清理可验证。

### P2：任务、生命周期与模块通信

- [ ] `P2-01` 实现 `Executor`、有界 `ThreadPool`、future 结果、取消令牌、统计和幂等关闭。
  - 验收：饱和拒绝、任务异常、任务内 shutdown、取消与析构竞态均有测试；不为每个任务创建线程。
- [ ] `P2-02` 实现基于单调时钟的 `Scheduler`，支持一次性/周期性任务和取消。
  - 验收：不为每个定时器建线程；覆盖漂移、长延迟、取消与最后一次执行竞争。
- [ ] `P2-03` 实现 `ServiceRegistry`：类型化服务、注册 token 和取得 handle 都是 RAII；注销不产生悬空调用。
  - 验收：并发 get/unregister、服务停止和 handle 生命周期测试通过。
- [ ] `P2-04` 实现 `EventBus`：拥有事件副本的异步 FIFO 发布、同步发布、RAII subscription 和可配置背压。
  - 验收：订阅/发布/reset 并发安全，明确报告无订阅者、拒绝、丢弃和关闭状态。
- [ ] `P2-05` 实现 `Module`、`ModuleManager`、`Context`、`Runtime` 与 `Application`：依赖图校验、确定性拓扑排序、初始化/启动回滚、反向停止和不可重启策略。
  - 验收：覆盖重复名、缺失依赖、环、迟注册、部分启动失败、重复 Stop 和终止信号优雅退出。

### P3：补齐通用 Foundation 与可观测性

- [ ] `P3-01` 实现 `ByteSpan`、标准 PMR 适配、带统计的内存资源和内存池；先避免自定义分配器复杂度进入 P1/P2。
  - 验收：对齐、容量耗尽、上游失败、统计和借用地址失效规则可测。
- [ ] `P3-02` 实现 JSON 与版本化、固定字节序的二进制 codec；Protobuf 仅作为可选 feature。
  - 验收：兼容性 fixture、畸形输入和版本/类型拒绝测试通过。
- [ ] `P3-03` 以成熟库封装 hash、Base64、SHA-2、RSA/Ed25519；记录密钥/敏感内存的责任边界。
  - 验收：非法编码、密钥、密文和认证标签均返回结构化错误；不得自行实现密码学原语。
- [ ] `P3-04` 实现 Metrics、Prometheus text exporter、Health registry；再提供结构化诊断上下文与日志字段传播。
  - 验收：指标注册冲突、标签、并发更新、快照和健康检查 deadline 都有测试。
- [ ] `P3-05` 实现本地 Tracer、move-only Span、W3C `traceparent` 注入/提取；OpenTelemetry exporter 作为可选 feature。
  - 验收：父子关系、恰好一次导出、属性/事件/状态、非法与全零 trace ID 都有测试。

### P4：跨平台 I/O、IPC 与宿主能力

- [ ] `P4-01` 选定并封装成熟网络后端（建议 Boost.Asio），实现 move-only TCP client/listener 与 UDP：IPv4/IPv6、部分 I/O、`SendAll`/`ReceiveExact`、deadline、取消和关闭唤醒。
  - 验收：真实 loopback 集成测试在 Linux、macOS、Windows 原生运行，不能用进程内 mock 代替。
- [ ] `P4-02` 实现 IPC：POSIX Unix domain socket、Windows named pipe、共享内存与跨进程锁；为不支持能力提供显式查询或结构化错误。
  - 验收：跨进程传输、最大帧拒绝、accept 取消、资源清理和互斥可测。
- [ ] `P4-03` 扩展 Platform：系统/进程查询及等待终止、目录观察、SIGINT/SIGTERM/Windows 控制台终止通道、daemon/Windows service host。
  - 验收：平台不支持的模式不模拟成功；每个平台均有原生运行证据。
- [ ] `P4-04` 实现进程级 RAII `CrashReporter`，在致命信号和未捕获异常中尽力生成元数据/调用栈，并保持原始退出语义。
  - 验收：崩溃路径不调用 logger、config 或用户回调；同一时刻只能安装一个实例。

### P5：按实际需求启用的扩展模块

- [ ] `P5-01` HTTP（可选 feature）：真实 HTTP/HTTPS client、路由 server、超时、流式上传下载和优雅停止；先暴露 `IHttpService`，不让其他模块依赖实现。
  - 验收：真实 client/server round trip、证书校验默认开启、限制和 handler 失败返回结构化错误。
- [ ] `P5-02` Database（可选 feature）：驱动无关连接池、move-only connection/transaction lease、SQLite 首个驱动。
  - 验收：池耗尽/超时、单连接并发限制、析构回滚和 `:memory:` 多物理连接语义均有集成测试。
- [ ] `P5-03` 将 P1/P4 的文件系统、进程能力包装为 Runtime 模块及 `IFileSystemService`/`IProcessService`，需要时再加 HTTP observability endpoint。
  - 验收：服务仅在模块运行期间可取得，模块间仅通过 Service/Event 合同协作。
- [ ] `P5-04` 软件更新（最后实现）：带签名的 SemVer 清单、HTTPS 下载、SHA-256、私有 staging、原子暂存、取消和进度；仅检查/验证/暂存，不执行替换、重启或回滚。
  - 验收：拒绝错误签名、降级、过期、不匹配平台/架构、越界路径和符号链接攻击。

### P6：交付收口

- [ ] `P6-01` 提供并自动运行最小 hello、CLI+配置、终止信号后台服务、任务/事件多模块、HTTP、Database、IPC、Metrics、Tracing 示例。
- [ ] `P6-02` 为每个能力维护需求到实现、测试和平台证据的追踪矩阵；新增 API 前先补 ADR 和合同。
- [ ] `P6-03` 在同一提交完成 Linux、macOS、Windows 的 configure、build、test、install 和安装包消费；运行对应 sanitizer/静态分析/fuzz 门禁。
- [ ] `P6-04` 发布前完成 API、迁移、平台差异、依赖许可证和安全边界文档；只在所有追踪项均有真实实现与自动化证据后标记版本完成。

## 首个可执行迭代

先完成 `P0-01` 至 `P0-05`，随后实施 `P1-01`、`P1-02`、`P1-05` 和一个“读取配置并记录日志”的 CLI 示例。该迭代会把现有错误模型、配置、日志、构建分发和测试合同连成可用的 MVP；`Runtime`、线程池和网络留待其公共基础契约稳定后再加入。
