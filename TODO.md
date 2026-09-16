# tos 改版 TODO（对齐 Pallas Framework）

## 目的和边界

本文以 `/Users/xrw/code/cyanland/pallas-framework` 的公开头文件、API 合同和架构文档为功能来源，为 `tos` 制定渐进式改版清单。

- `tos` 当前基线：仅头文件 `Status`、`Result<T>`、时间和配置能力已具备：`Time`/`Duration`/时钟、JSON/YAML 配置树、类型化读取、合并、快照式 reload，以及手动组装的 `LayeredConfig` 与来源追踪；GoogleTest、最小示例和三平台 CI 已具备。
- Pallas 当前基线：M0--M4 已完成；网络/IPC/Platform、扩展模块、示例文档和发布门禁仍缺原生 macOS/Windows 运行证据。因此它是功能与架构参考，不应被视为已经完成三平台发布验证的成品。
- 目标不是复制 Pallas 的源码、CMake target 名称或第三方依赖，而是在 `tos` 中建立等价且经过自身测试的公开合同。每个公共组件均须说明所有权、线程安全、错误、关闭/取消和平台差异。
- `tos::Status` 与 `tos::Result<T>` 是本项目唯一的错误/结果规范模型；保留当前 move-only 所有权、`Status` 表示无值操作结果、`Result<void>` 禁止的合同。不引入 `pallas::Error`、`pallas::Result` 或其公开别名。
- 本清单不纳入 GUI、完整 ORM、分布式服务治理、自研通用网络协议栈，以及未被实际应用场景证明需要的远程 Feature Flag、自动更新执行/重启/回滚。

## Pallas 功能清单与 tos 差距

| 能力域 | Pallas 的公开能力 | tos 状态 |
| --- | --- | --- |
| 错误与结果 | 结构化 `Error`（域、原生码、根因）、`Result<T>`/`Result<void>`、Abseil 兼容 | 部分完成：保留 tos 自身的 move-only `Status`/`Result<T>`，并提供可操作的错误码分类与只读谓词；不移植 Pallas 错误模型。仍需补充传播辅助工具，结构化 payload 留待有原生错误信息需求时实现 |
| Core | `App`、`Context`、`Module`、依赖 DAG、启动回滚和反向停止 | 已完成（`tosapp`/`tos::app`）；Runtime 及调度能力留待后续 |
| 通信 | 非拥有的类型化 `ServiceRegistry`、同步 `EventBus` 和 RAII 订阅 | 已完成：`Context` 支持显式服务指针注册、查询和注销，以及线程安全的同步 EventBus；异步 FIFO 与背压不在当前范围内 |
| Task | `Executor`、有界 `ThreadPool`、future、取消、单调时钟 `Scheduler` | 已完成：Executor、有界 ThreadPool、future、协作取消、统计、幂等关闭和单调 Scheduler 已实现 |
| 配置与应用工具 | JSON/YAML、分层配置、类型/模式校验、reload、FeatureFlags、CLI 参数、环境读取 | 部分完成：JSON/YAML 配置树、点路径类型化读取、合并、快照式 reload、手动 `LayeredConfig` 和来源追踪已实现；配置文件加载、环境变量、CLI、模式校验和 FeatureFlags 未实现 |
| 日志与诊断 | 同步/异步 logger、sink、轮转、结构化字段、诊断上下文 | 部分完成：同步线程安全 Logger、控制台、按大小滚动 JSON Lines 文件、强类型字段与 flush/shutdown 已实现；异步队列和诊断上下文未实现 |
| Foundation 扩展 | 内存资源/内存池、时钟、JSON/二进制/Protobuf 序列化、OpenSSL 加密 | 部分完成：`tos::span` 及基于 OpenSSL 的摘要、Base64、RSA 和 Ed25519 已实现；内存资源、二进制 codec 与 Protobuf 未实现 |
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
- [x] `P0-02a` 扩展 `StatusCode` 与无分配分类谓词：保留现有枚举值和 `kTimeout` 的超时/截止时间语义，新增 `kCancelled`、`kUnknown`、`kAlreadyExists`、`kPermissionDenied`、`kUnauthenticated`、`kResourceExhausted`、`kFailedPrecondition`、`kAborted`、`kOutOfRange`、`kUnimplemented`、`kUnavailable` 和 `kDataLoss`，并提供对应的只读谓词。
  - 验收：每个错误码在 API 合同中有明确的调用方处理语义，`ToString()`、比较、移动、`Result<T>` 传播和未知枚举值都有测试；0.x 迁移说明明确既有数值不变且不承诺 Abseil 枚举或 ABI 兼容。
- [ ] `P0-02b` 提供 `Status`/`Result<T>` 传播辅助工具：增加经测试的 `TOS_RETURN_IF_ERROR` 和 `TOS_ASSIGN_OR_RETURN` 宏或等价 C++17 接口，避免调用方手写易错的 move-only 错误传播。
  - 验收：操作数恰好求值一次，错误以 `std::move` 传播，成功值可移动提取；在 `if`/`else`、临时对象、命名对象和异常构造路径中均有编译与运行测试，宏不耦合 logger 或其他 Runtime 组件。
- [x] `P0-03` 将基础能力收敛为 `tosbase` 静态库，并公开 `tos::base`；模块化 Application 作为独立 `tosapp`/`tos::app` 构建，依赖方向保持单向。
  - 验收：两个目标传递所需依赖；minimal、完整测试/示例和 `tos::app` 消费目标均可构建。
- [ ] `P0-04` 增加安装、导出与版本文件，支持 `find_package(tos CONFIG REQUIRED)`；保持 `add_subdirectory` 接入。
  - 验收：全新临时消费工程分别以两种方式构建并运行。
- [ ] `P0-05` 建立质量门禁：Debug/Release、ASan/UBSan、TSan（支持的平台）、clang-format、clang-tidy、coverage、fuzz 的 CMake presets；为 CTest 统一标签和超时。
  - 验收：本机与 CI 均执行相应矩阵，失败信息可定位到组件和测试。

### P1：完成 MVP 基础能力

- [x] `P1-01` 实现 `tos::time`：可注入 `IClock`、系统/手动时钟、单调耗时、UTC RFC3339 时间戳解析与格式化。
  - 验收：手动时钟使定时相关测试确定性；非法时间文本返回结构化错误。
- [x] `P1-02` 实现 `tos::config`：JSON/YAML 配置树、点路径类型化读取、必填/类型校验、合并与不可见部分更新的快照式 reload。
  - 验收：错误精确到路径；并发读取、格式错误、合并和 reload 一致性都有测试。
- [ ] `P1-03` 补齐分层配置的来源适配、环境变量和命令行：现有 `LayeredConfig` 已支持调用方手动提供具名层、确定性合并和来源追踪；新增 API 固定采用 defaults < file < environment < CLI 的优先级，并支持配置文件加载、长短选项、重复值、位置参数、`--help` 与 `--version`。
  - 验收：端到端示例验证覆盖顺序、未知参数和缺参失败，JSON 与 YAML 输入均有覆盖。
- [ ] `P1-04` 实现只读布尔 `FeatureFlags`，绑定配置中的固定前缀；不做远程同步、灰度或用户分群。
  - 验收：非法名称/类型、默认值和 reload 后立即可见均有测试。
- [x] `P1-05` 实现同步线程安全 `Logger`：级别、控制台/JSON Lines 文件 sink、格式化、按大小滚动和 flush；敏感信息筛选由调用方负责，异步与有界队列留作第二阶段。
  - 验收：多线程写入不会损坏单条记录，文件失败可观察，关闭时已接收记录被刷新。
- [x] `P1-06` 实现基础平台文件工具：UTF-8 路径适配、文本读写、目录/元数据查询和原子写入。
  - 验收：不存在、权限和替换失败返回明确错误；临时文件与资源清理可验证。

### P2：任务、生命周期与模块通信

- [x] `P2-01` 实现 `Executor`、有界 `ThreadPool`、future 结果、取消令牌、统计和幂等关闭。
  - 验收：饱和拒绝、任务异常、任务内 shutdown、取消与析构竞态均有测试；固定工作线程不为每个任务创建线程。
- [x] `P2-02` 实现基于单调时钟的 `Scheduler`，支持一次性/周期性任务和取消。
  - 验收：单计时线程配合借用 Executor；覆盖固定频率跳 tick、长延迟关闭、取消与最后一次执行竞争。
- [x] `P2-03` 实现非拥有的 `ServiceRegistry`：`Context` 提供类型化 `RegisterService(T*)`、返回 move-only `ServiceHandle<T>` 的 `GetService<T>()` 和 `UnregisterService(T*)`；句柄通过 RAII 归还借用，并在注销成功后销毁服务。Config/Logger 由 Context 直接提供，不注册为 Service。
  - 验收：重复、空指针、缺失、期望指针不匹配、未归还借用的注销拒绝和并发注册表访问均有测试；Config/Logger 由 Context 直接提供，注册表不删除或保活服务对象。
- [x] `P2-04` 实现同步 `EventBus`：类型化调用方线程发布、RAII subscription 和关闭。
  - 验收：订阅、发布、reset 和 shutdown 并发安全；无订阅者和关闭状态返回明确 Status。异步 FIFO、事件副本与背压不在当前范围内。
- [x] `P2-05` 实现模块化 `App` 框架：`tosapp`/`tos::app` 提供 `App`、`Module`、`Context` 和 App 状态机、依赖图校验、确定性拓扑排序、OnLoad 加载回滚及 OnUnload 反向清理；App 私有共享 Executor 和 Scheduler 由 App/Context 提供。
  - 验收：覆盖重复名、缺失依赖、环、迟注册、部分启动失败、重复 Stop、并发控制器调用和析构清理。
- [x] `P2-06` 实现嵌入式动态调试控制：`tos::app` 提供文本命令 `DebugController`，由 App 以普通处理器注册 `status` 和 `log-level` 来输出 App、Logger 与线程池快照；宿主和模块可分别通过控制器及 Context 动态注册、注销和替换处理器；不开放监听端口、任意执行、暂停恢复或内置生命周期控制。
  - 验收：分词、稳定文本输出、等级过滤、无效命令/参数、动态注册/冲突/注销、在途处理器和并发调用均有自动化测试；示例通过 CTest 运行。

### P3：补齐通用 Foundation 与可观测性

- [ ] `P3-01` 实现标准 PMR 适配、带统计的内存资源和内存池；先避免自定义分配器复杂度进入 P1/P2。
  - 验收：对齐、容量耗尽、上游失败、统计和借用地址失效规则可测。
- [ ] `P3-02` 实现 JSON 与版本化、固定字节序的二进制 codec；Protobuf 仅作为可选 feature。
  - 验收：兼容性 fixture、畸形输入和版本/类型拒绝测试通过。
- [ ] `P3-03` 以成熟库封装 hash、Base64、SHA-2、RSA/Ed25519；记录密钥/敏感内存的责任边界。
  - 验收：非法编码、PEM、密钥、签名和 RSA-OAEP 密文均返回结构化错误；不得自行实现密码学原语。实现与本地自动化测试已具备，待 Linux/macOS/Windows CI 原生运行证据后标记完成。
- [ ] `P3-04` 实现 Metrics、Prometheus text exporter、Health registry；再提供结构化诊断上下文与日志字段传播。
  - 验收：指标注册冲突、标签、并发更新、快照和健康检查 deadline 都有测试。
- [ ] `P3-05` 实现本地 Tracer、move-only Span、W3C `traceparent` 注入/提取；OpenTelemetry exporter 作为可选 feature。
  - 验收：父子关系、恰好一次导出、属性/事件/状态、非法与全零 trace ID 都有测试。
- [ ] `P3-06` 按实际的 Platform、Network 或 IPC 需求为失败 `Status` 增加命名的二进制 payload：用于保留原生错误码、HTTP 状态或可重试等机器可读上下文；不引入 Abseil 类型或改变 move-only 所有权。
  - 验收：键命名规则、`kOk` 行为、覆盖/删除/遍历、带嵌入空字符的 payload、借用视图的生命周期和移动后行为都有合同与测试；至少一个真实平台或 I/O 适配器验证原生错误信息能保留并被调用方读取。

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

先完成全部 P0 条目，随后实施 `P1-01`、`P1-02`、`P1-03`、`P1-05` 和一个“读取配置并记录日志”的 CLI 示例。该迭代会把现有错误模型、配置、日志、构建分发和测试合同连成可用的 MVP；`Runtime`、线程池和网络留待其公共基础契约稳定后再加入。
