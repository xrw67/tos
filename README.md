# tos

C++ 快速应用开发库，为应用提供可复用的基础组件，减少工程搭建和通用功能的重复开发。

## 构建与验证

当前已实现仅头文件的 `Status`、`Result<T>`、`Time`、`Duration`、`Config` 和 `LayeredConfig`，并提供 GoogleTest 单元测试、最小示例和三平台 CI。其他应用组件仍处于需求规划阶段。

### 环境要求

- CMake 3.24 或更新版本。
- 支持 C++17 的编译器：Linux 使用 GCC，macOS 使用 Apple Clang，Windows 使用 MSVC。
- macOS 安装 Xcode Command Line Tools；Windows 安装 Visual Studio 2022 的“使用 C++ 的桌面开发”工作负载。
- GoogleTest 1.17.0 完整源码随仓库存放于 `third_party/googletest/`，使用 BSD-3-Clause 许可证；版本、来源和校验值记录在 [第三方库说明](third_party/README.md) 中。
- nlohmann/json 3.11.3 的单头文件和 MIT 许可证位于公开 include 树的 `include/tos/vendor/nlohmann/`；包含 `<tos/json.h>` 后可使用 `tos::json`，不产生运行时库或网络下载。
- fkYAML 0.4.4 的单头文件和 MIT 许可证位于 `include/tos/vendor/fkyaml/`；包含 `<tos/yaml.h>` 即可使用，不产生运行时库或网络下载。
- fmt 12.2.0 的公开头文件和 MIT 许可证位于 `include/tos/vendor/fmt/`；包含 `<tos/format.h>` 即可在 header-only 模式下使用，不产生额外链接依赖或网络下载。
- 配置和构建无需下载依赖，也无需初始化 Git 子模块。仅启用测试时构建 GoogleTest，当前不构建 GoogleMock。

### 本地编译与运行

在仓库根目录执行，三平台使用相同的命令：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DTOS_BUILD_EXAMPLES=ON
cmake --build build --config Release --parallel
ctest --test-dir build -C Release -L unit --output-on-failure --no-tests=error --timeout 30
ctest --test-dir build -C Release -L example --output-on-failure --no-tests=error --timeout 30
```

`unit` 运行 Status、Result、Time 和 Duration 的 GoogleTest 单元测试，覆盖错误状态、值访问、移动所有权、异常恢复、Unix 时间规范化、RFC3339 和手动时钟；`example` 检查最小示例退出码为 0，且输出为 `tos example ready`。没有匹配的检查时 CTest 会报错，运行失败时展示详细信息。

`CMAKE_BUILD_TYPE` 用于 Makefiles 等单配置生成器，`--config Release` 和 `-C Release` 用于 Visual Studio 等多配置生成器。两者同时保留以便跨平台使用。

| 构建选项 | 独立构建默认值 | 作用 |
| --- | --- | --- |
| `BUILD_TESTING` | `ON` | 引入 GoogleTest，构建测试程序，并注册 CTest 检查 |
| `TOS_BUILD_EXAMPLES` | `ON` | 构建最小示例；同时开启测试时注册示例检查 |

通过 `-DBUILD_TESTING=OFF` 或 `-DTOS_BUILD_EXAMPLES=OFF` 可分别关闭这些功能；关闭测试后仍可单独构建示例。两者同时关闭时只提供接口库，不生成可执行文件或静态库。

### 编写单元测试

在 `tests/` 中新增测试源文件，使用 GoogleTest 的 `TEST` 或 `TEST_F` 定义用例，并将源文件加入 `tests/CMakeLists.txt` 中的 `tos_unit_tests` 目标。该目标链接 `tos::tos` 和 `GTest::gtest_main`，无需自行编写 `main()`。

CMake 的 `gtest_discover_tests()` 会在 CTest 运行前自动发现用例，统一添加 `tos.` 名称前缀、`unit` 标签和 30 秒超时；新增用例无需逐个修改 CI。分配回归测试使用独立的 `tos_allocation_tests` 可执行文件，保留 6 项检查：成功零分配、Status 移动零分配、Result 状态借用及提取零分配、字符串右值转移缓冲区，以及两种分配失败清理路径。仅在被测操作期间统计分配和释放，避免影响常规测试。

退出阶段的状态检查由独立进程 `tos_result_shutdown_test` 验证，保留已移动错误、无活动值两个回归场景，同样带有 `unit` 标签。

第三方库统一由 `third_party/CMakeLists.txt` 管理，GoogleTest 仅在测试开启时从仓库内源码构建；nlohmann/json、fkYAML 和 fmt 分别通过公开 `<tos/json.h>`、`<tos/yaml.h>` 与 `<tos/format.h>` 提供。完整检出仓库后即可离线构建，不再使用 FetchContent 或 `FETCHCONTENT_SOURCE_DIR_GOOGLETEST` 配置。

### 接入其他工程

将仓库放入消费工程的 `third_party/tos`，在该工程的 CMake 配置中添加：

```cmake
add_subdirectory(third_party/tos)
target_link_libraries(your_app PRIVATE tos::tos)
```

`your_app` 应由消费工程先通过 `add_executable` 或 `add_library` 定义。`tos::tos` 传递头文件路径和至少 C++17 的编译要求；消费工程自行设置其目标是否启用编译器语言扩展。

作为子工程时，两个构建选项默认关闭；若上层工程或 CMake 缓存已经设置了同名选项，则尊重现有值。当前未提供安装导出或 `find_package` 接入。

### 自动化验证

[GitHub Actions CI](https://github.com/xrw67/tos/actions/workflows/ci.yml) 在每次推送、PR 和手动触发时运行三个并行 Release 任务：

| 系统镜像 | 编译器 | 检查内容 |
| --- | --- | --- |
| `ubuntu-24.04` | GCC | 编译、GoogleTest 用例、示例运行 |
| `macos-14` | Apple Clang | 编译、GoogleTest 用例、示例运行 |
| `windows-2022` | MSVC / Visual Studio 2022，x64 | 编译、GoogleTest 用例、示例运行 |

每个 CI 任务最长运行 15 分钟，每项 CTest 检查最长运行 30 秒。失败任务不会取消其他平台的任务。后续增加业务组件时，应增加对应单元测试并接入 CTest 和 CI。

本地实测环境及结果见下方记录；Linux、Windows 以及 GitHub macOS runner 的结果以 CI 实际运行记录为准。镜像标签固定系统系列，预装工具链仍可能随镜像更新，并不代表最低支持版本。

### 本地验证记录

2026-09-09 在 macOS 26.6.2（arm64）、Apple Clang 21.0.0、CMake 4.3.1 环境验证通过：

- 初始骨架阶段：Unix Makefiles 和 Ninja Multi-Config 的 Release 编译及两项 CTest 检查。
- 关闭测试、关闭示例、同时关闭两者的配置和构建；关闭测试后仍可运行示例。
- 临时消费工程通过 `add_subdirectory` 接入，默认不构建 tos 测试和示例；链接 `tos::tos` 后获得头文件路径及 C++17 要求。
- 示例校验脚本在程序返回非零退出码或输出错误时正确报错。
- Status / Result 独占所有权阶段：Ninja 和 Ninja Multi-Config 的 Release 编译、49 项 GoogleTest 用例、3 项退出阶段检查及 1 项示例检查全部通过。
- 添加 `Status::ToString()` 并精简测试后：Ninja Release 构建及 52 项 CTest 检查全部通过（49 项 GoogleTest、2 项退出阶段检查、1 项示例）；非法模板类型的配置阶段检查已移除。
- 在全新构建目录中关闭测试，示例编译及运行通过，GoogleTest 未参与构建。
- Debug 配置下启用 ASan/UBSan，53 项 CTest 检查全部通过；分配回归测试确认普通错误由所有者释放，长字符串右值构造仅分配错误表示。
- 使用现有 clang-tidy 配置检查 Status / Result 公共头文件通过；改为普通指针布局后，之前的疑似泄漏告警未再出现。此记录仅涵盖公共头文件诊断。
- 下方用法示例独立编译并输出 `localhost:8080`。编译检查确认不支持的类型参数被拒绝，忽略两个类型的返回值会触发 `[[nodiscard]]` 诊断。

## 状态与结果

无返回值操作使用 `tos::Status`，有返回值操作使用 `tos::Result<T>`。两个类型都标记了 `[[nodiscard]]`，提醒调用方处理返回结果；无需额外链接库。

```cpp
#include "tos/result.h"
#include "tos/status.h"

#include <iostream>
#include <string>

tos::Status validate_port(int port) {
    if (port <= 0 || port > 65535) {
        return {tos::StatusCode::kInvalidArgument, "port must be between 1 and 65535"};
    }
    return tos::Status::Ok();
}

tos::Result<std::string> make_endpoint(int port) {
    auto status = validate_port(port);
    if (!status) {
        return status;
    }
    return "localhost:" + std::to_string(port);
}

int main() {
    const auto endpoint = make_endpoint(8080);
    if (!endpoint) {
        std::cerr << endpoint.status().message() << '\n';
        return 1;
    }
    std::cout << endpoint.value() << '\n';
    return 0;
}
```

### Status

`Status` 默认构造或通过 `Status::Ok()` 得到成功状态；`Status(code)` 创建不带信息的状态。`Status(code, message)` 接受字符串、字符串字面量或 `std::string_view`，失败时创建独占的错误表示；字符串右值可以移动到其中。成功时不复制或消耗传入的消息，但调用方构造消息表达式的开销仍然存在。

`ok()` 和显式布尔转换判断是否成功，`code()` 返回错误码，`message()` 返回借用文本的 `std::string_view`。成功状态的信息始终为空，即使构造时传入了信息。视图不提供所有权，不应依赖它在来源 Status 被赋值、移动或销毁后仍然有效；需要独立文本时使用 `std::string(status.message())`。

| 错误码 | 数值 | 含义 |
| --- | --- | --- |
| `StatusCode::kOk` | 0 | 成功 |
| `StatusCode::kInvalidArgument` | 1 | 参数错误 |
| `StatusCode::kNotFound` | 2 | 资源不存在 |
| `StatusCode::kTimeout` | 3 | 操作超时 |
| `StatusCode::kInternal` | 4 | 内部错误 |

相等比较先检查是否具有相同表示，再比较错误码和消息内容。Status 禁止复制构造和复制赋值，只支持移动；移动转移所有权，源对象变为成功状态，自移动不改变内容。默认构造、`Status::Ok()`、移动和析构均不抛异常；所有错误状态的首次构造都需要分配，即使没有消息也可能抛出 `std::bad_alloc`。接受错误码的构造函数因此不标记 `noexcept`，但传入 `kOk` 时不会分配。

错误表示由单个 Status 拥有，没有引用计数或原子操作。覆盖旧状态或析构时立即删除原错误表示及其消息，不让成功状态保留旧消息容量。并发只读访问是安全的；涉及同一对象的赋值、移动或析构时需要调用方同步。

`ToString()` 返回独立的 `std::string`，适合记录日志：成功输出 `OK`，错误输出符号名称，有消息时追加 `: 消息`。未知错误码输出 `UNKNOWN(数值)`。消息按原始长度保留，包括嵌入的空字符，不进行转义；调用不会修改状态，但字符串构造可能抛出分配异常。

```cpp
const tos::Status error(tos::StatusCode::kNotFound, "missing");
std::string text = error.ToString();  // "NOT_FOUND: missing"
auto empty = tos::Status(tos::StatusCode::kTimeout).ToString();  // "TIMEOUT"
auto success = tos::Status::Ok().ToString();  // "OK"
```

### 内存布局

Status 仅保存一个 `std::unique_ptr<ErrorRep>`：`nullptr` 表示成功，非空指针独占堆上的错误表示，无消息错误也采用相同存储。错误表示只包含错误码和字符串，消息构造后不可修改；不使用指针位标记，不提供 payload 或共享存储。

| 场景 | 对象本体（本机 64 位环境） | 额外存储 |
| --- | --- | --- |
| 成功 | 8 字节 | 无堆分配 |
| 无消息错误 | 8 字节 | 分配错误表示 |
| 带消息错误 | 8 字节 | 错误表示；长字符串还可能需要字符缓冲区 |
| 移动已有错误 | 每个对象 8 字节 | 转移原表示，不新增分配 |

本机 `sizeof(Status)` 从最初的 32 字节降至 8 字节，`sizeof(Result<int>)` 从 40 字节降至 16 字节。对象本体大小不包含堆存储，所有错误的首次构造均需要分配错误表示。移动构造只转移指针；移动赋值还会释放目标原先拥有的错误表示。

### Result

`Result<T>` 从值或可构造为 `T` 的对象得到成功结果，也可从失败的 Status 右值构造，例如 `Result<int> result = std::move(error);`。它不能默认构造，不要求 `T` 有默认构造函数，支持 `std::unique_ptr` 等只能移动的类型。由于内部保存 Status，Result 也禁止复制；移动能力跟随 `T`。从普通值左值构造仍可复制该值。

- `ok()` 和显式布尔转换只判断结果是否成功；成功值本身可以是 `false` 或空指针。
- 左值上的 `status()` 返回 `const Status&`，使用 `const auto& error = result.status();` 借用状态，不复制、不转移错误。不要依赖引用在来源 Result 被赋值、移动或销毁后仍然有效。
- 非 const 右值上的 `status()` 转移错误表示的所有权。提取错误或移动失败 Result 后，源 Result 仍没有值，`ok()` 返回 false，`status()` 返回带有 `Result error has been moved` 信息的 `kInternal`；成功结果的值不受 `status()` 影响。
- 需要独立 Status 时使用 `auto error = std::move(result).status();`；正常错误的提取不分配，提取出的对象可以超过来源 Result 的生命周期。const 右值上的 `status()` 已禁用，因为它不能转移所有权。
- 成功时左值 `status()` 返回静态成功状态的引用；异常后无活动值或错误已被移走时，返回对应静态内部错误状态的引用。右值访问这些特殊状态时构造独立 Status；错误状态可能分配，因此 `status()` 不标记 `noexcept`。用成功 Status 构造 Result 会抛出 `std::invalid_argument`，因为此时没有可用的成功值。
- 三个静态兜底状态按需初始化并保留至进程结束，不参与静态析构，以支持全局对象析构期间的状态检查。每个 `Result<T>` 实例化最多保留两份兜底错误存储，成功兜底不分配；普通错误仍由 Status 正常释放。
- `value()`、`*result` 和 `result->` 均检查状态，失败时抛出包含错误码和信息的 `std::logic_error`。正常业务失败通过结果返回，无需依靠异常处理。
- `value()` 和 `operator*` 保留 const 与引用类别；可通过 `std::move(result).value()` 提取只能移动的值。源 Result 仍可能成功，但值已经被移动。
- 返回的引用和指针借用 Result 内部对象；Result 析构或切换存储的类型会使它们失效。同一值的赋值、移动及并发访问还须遵守 `T` 自身的规则。
- 值类型的构造、复制和移动异常直接传播。如果异常赋值使底层 `std::variant` 无活动值，Result 表现为带固定信息 `Result has no value after an exception` 的 `kInternal` 错误，仍可检查或重新赋值恢复。

`T` 必须是可析构、非数组、非 const/volatile 的对象类型，不支持引用类型、`Result<Status>` 或 `Result<void>`；无值操作直接使用 Status。两个类型均不提供对象级锁，涉及同一对象的修改或移动时需要调用方同步。构造错误信息和显式复制消息文本可能抛出内存分配异常。

### 兼容性变化

- `message()` 从 `const std::string&` 改为 `std::string_view`；需要拥有文本的代码应显式构造 `std::string`，不能继续依赖原来的字符串引用契约。
- Status 和 Result 均禁止复制。传递已有错误的所有权需要 `std::move(error)`；用于错误传播的局部变量不要声明为 const。
- `Result::status()` 在左值上返回借用引用，在非 const 右值上返回独立 Status；const 右值访问被禁用。原先的 `auto error = result.status();` 应根据需要改为借用引用或显式移动提取。
- 接受错误码的 Status 构造均可能因分配失败抛出异常，包括无消息错误和字符串右值消息，因此不标记 `noexcept`。
- 移动后的源 Status 现在变为成功状态；移动后的失败 Result 则报告错误已被移走的 `kInternal`，不再保留原错误码。
- Status 和 Result 的对象布局发生变化，所有使用方必须重新编译，不与旧构建产物保持 ABI 兼容。

## 时间

`<tos/time.h>` 提供参考 Go `time.Time` 和 `time.Duration` 设计的 UTC 时间 API。`tos::Duration` 是有符号纳秒耗时，常量 `Nanosecond`、`Microsecond`、`Millisecond`、`Second`、`Minute` 和 `Hour` 可直接使用；`ParseDuration()` 接受类似 `1h2m3.4s` 的组合单位。`ToString()` 输出同样的紧凑表示。超出 `int64_t` 纳秒范围或格式非法时，解析返回 `Result<Duration>` 中的 `kInvalidArgument`。

`tos::Time` 是可复制、不可变的 UTC 瞬时值，也以 Unix epoch 起的有符号纳秒保存。`FromUnix(seconds, nanoseconds)` 与 Go 一样规范化纳秒分量，`Unix()`、`UnixMilliseconds()` 和 `UnixMicroseconds()` 对 epoch 前的非整单位向下取整；也提供 Go 命名的 `UnixMilli()`、`UnixMicro()` 和 `UnixNano()` 别名。`Add()` 和 `Sub()` 处理时间范围溢出时返回 `Result`，不静默回绕。`FormatRfc3339()` 始终输出 UTC `Z` 后缀；`ParseRfc3339()` 支持 UTC 和数值偏移输入，返回归一化的 UTC 值。

```cpp
#include "tos/time.h"

const auto delay = tos::ParseDuration("1h2m3.4s");
const auto started = tos::ParseRfc3339("2026-09-09T08:00:00+08:00");
if (!delay || !started) {
    return 1;
}
const auto deadline = started.value().Add(delay.value());
if (deadline) {
    std::string text = deadline.value().FormatRfc3339();
}
```

`IClock` 允许依赖注入；`SystemClock` 读取系统 UTC 时钟，可能随系统校时倒退；`ManualClock` 用于确定性的测试或调度，支持线程安全的 `Now()`、`Set()` 和有溢出检查的 `Advance()`；`MonotonicClock::Elapsed()` 基于 `std::chrono::steady_clock` 测量单调耗时，不能转换为 UTC。本实现没有 Go `Time` 的 location 或隐藏单调读数，格式化和解析不读取进程时区。

所有值类型均不持有外部资源，复制和并发只读安全。`SystemClock`、`ManualClock::Now` 和 `ManualClock::Set` 不抛异常；字符串格式化的分配异常以及构造错误 `Status` 的分配异常会直接传播。

## 配置

`<tos/config.h>` 提供 JSON/YAML 的只读配置树。`Config::Parse()` 仅接受 object/mapping 根节点，并把 JSON 与 YAML 解析失败、YAML 非字符串 mapping key、带 tag 的 YAML 节点、路径和类型错误转换为 `Status`：缺失路径为 `kNotFound`，其余预期输入错误为 `kInvalidArgument`。错误信息包含输入来源和（读取错误时）完整点路径。YAML 锚点或别名只要能展开为此树即可使用。分配失败仍按 C++ 异常传播。

路径使用 `.` 分隔 object key，并可在数组处使用十进制索引；字面 `.` 和反斜杠分别写成 `\\.` 与 `\\\\`。键名保持大小写敏感。`Has()` 是便捷存在性检查，缺失或路径格式错误都返回 `false`；需要区分错误时使用对应的 `Get*` 接口。`GetBool`、`GetInt64`、`GetDouble` 和 `GetString` 都要求节点类型精确匹配；`GetUint64` 还接受可无损表示的非负有符号整数，不进行字符串、布尔和浮点转换。

```cpp
#include "tos/config.h"

const auto parsed = tos::Config::Parse(
    "service:\n  host: localhost\n  ports: [8080, 8443]\n",
    tos::ConfigFormat::kYaml, "app.yaml");
if (!parsed) {
    return 1;
}
const tos::Config config = parsed.value();
const auto port = config.GetInt64("service.ports.1");
if (!port) {
    return 1;
}

tos::ConfigStore store(config);
const tos::Status reloaded = store.Reload(R"({"service":{"ports":[9000]}})",
                                          tos::ConfigFormat::kJson, "next.json");
if (!reloaded) {
    return 1;
}
const tos::Config snapshot = store.Snapshot();
```

`Config::Merge()` 递归合并双方都是 object 的字段；数组、null、标量和类型不一致字段由 overlay 完整替换，两个输入不会被修改。`ConfigStore` 使用原子共享指针发布完整不可变快照，读取可与 reload 并发，且只会观察旧树或完整新树。

`LayeredConfig` 将具名的 `ConfigLayer` 组合为只读快照，适用于自行准备好的默认值、文件内容或其他配置来源。层标签必须非空且在同一集合中唯一。优先级数值越小，覆盖能力越强；同一优先级时，输入 vector 中靠后的层优先。对象递归合并，数组、null、标量和类型不一致值由高优先级层整体替换。`Snapshot()` 返回可独立保留的 `Config`，`SourceOf(path)` 返回定义最终路径的最高优先级层标签；对于由多个层合并的 object，它表示包含该 object 路径的最高优先级层，不表示整个子树只来自该层。

```cpp
auto defaults = tos::Config::Parse(R"({"service":{"host":"localhost","port":80}})",
                                   tos::ConfigFormat::kJson, "defaults");
auto file = tos::Config::Parse(R"({"service":{"port":443}})", tos::ConfigFormat::kJson,
                               "app.json");
if (!defaults || !file) {
    return 1;
}
auto layered = tos::LayeredConfig::Create({
    {"defaults", 100, std::move(defaults).value()},
    {"file", 10, std::move(file).value()},
});
if (!layered) {
    return 1;
}
const tos::Config effective = layered.value().Snapshot();
const auto source = layered.value().SourceOf("service.port");  // "file"
const auto port = effective.GetInt64("service.port");
if (!source || !port || source.value() != "file" || port.value() != 443) {
    return 1;
}
```

`LayeredConfig` 的层集合不可变，副本共享同一状态并支持并发读取；替换配置时，构造新的 `LayeredConfig` 并把其 `Snapshot()` 发布到 `ConfigStore`。它不读取文件、不监听文件，也不读取环境变量或命令行参数；这些能力仍留待后续组件实现。所有预期输入失败通过 `Result` 的 `Status` 报告，字符串、容器和合并树所需的分配异常会直接传播。

## 基础需求草案

本文描述计划建设的能力，不代表已经实现。首版暂以命令行工具和后台服务为主要场景，技术基线暂定为 C++17 和 CMake，后续可按实际使用需求调整。

### 1. 项目目标

- 提供统一、易用的基础 API，让开发者集中处理业务逻辑。
- 支持组件独立使用，也支持组合成完整应用。
- 降低第三方依赖和平台差异对业务代码的影响。
- 提供可编译运行的示例，帮助开发者快速完成第一个应用。

### 2. 首版范围

首版（MVP）围绕“启动应用、读取配置、执行任务、记录日志、处理错误、退出应用”形成完整流程。

| 模块 | 基础需求 | 验收标准 |
| --- | --- | --- |
| 状态与错误处理 | 提供统一的 `Status` 类型，包含成功状态、错误码和错误信息；使用 `Result<T>` 携带返回值 | 能区分成功、参数错误、资源不存在、超时和内部错误；失败结果不能被当作有效值读取 |
| 日志 | 支持 DEBUG、INFO、WARN、ERROR 级别，控制台及文件输出，可配置最低输出级别；记录时间和级别 | 级别过滤有效；多线程写入不会破坏单条记录；文件打开或写入失败时有可观察的错误反馈 |
| 配置 | 支持一种配置文件格式（首版暂定 JSON）、默认值、环境变量和命令行覆盖；提供类型检查与必填项校验 | 优先级为命令行 > 环境变量 > 配置文件 > 默认值；格式或类型错误能定位到具体配置项 |
| 命令行参数 | 支持长短选项、带值选项、开关参数，以及帮助和版本输出 | 能解析有效参数；未知参数、缺少参数值时给出明确提示和非零退出码 |
| 应用生命周期 | 提供初始化、运行和停止流程；支持中断信号触发正常退出 | 初始化失败能够释放已分配资源；停止操作可重复调用；正常退出时完成资源清理和日志刷新 |
| 常用工具 | 提供文件读写、目录操作、时间与耗时测量等常用功能，优先复用标准库 | 文件不存在、权限不足等失败可被调用方识别；耗时测量使用单调时钟 |
| 构建与接入 | 提供 CMake 构建配置，支持通过 `add_subdirectory` 和安装后的 `find_package` 接入 | 独立示例工程能通过两种方式链接并运行；构建不依赖开发者机器上的绝对路径 |

### 3. 后续扩展

以下能力按实际应用需求逐步加入，不作为首版交付条件：

- 并发与调度：线程池、异步任务、定时器、取消和超时控制。
- 网络通信：HTTP 客户端及服务端，按需扩展 TCP、UDP。
- 数据访问：SQLite 等轻量存储及序列化适配。
- 工程模板：生成最小应用工程及常见场景示例。

GUI 框架、完整 ORM、分布式服务治理及自研通用网络协议栈暂不纳入范围。

### 4. 技术与质量要求

- **语言与构建**：使用 C++17 和 CMake；公开头文件可独立包含，公共符号统一放入 `tos` 命名空间。
- **平台兼容**：目标平台为 Linux、macOS 和 Windows，分别通过 GCC、Clang 和 MSVC 的持续集成验证；具体最低编译器版本在建立构建系统时记录。
- **资源管理**：使用 RAII 表达资源所有权，避免通过裸指针隐式转移所有权；明确对象生命周期及清理顺序。
- **错误约定**：可预期的操作失败通过统一状态或结果类型返回；第三方异常在适配边界转换；内存分配失败等异常行为单独说明，不承诺禁用异常支持。
- **线程安全**：每个公共组件说明是否支持并发调用；首版日志组件必须支持多线程写入。
- **依赖管理**：优先使用标准库和成熟第三方组件；记录依赖版本与许可证，可选模块按需启用。
- **配置与日志安全**：默认不记录密码、令牌等敏感配置；外部输入在使用前进行校验。
- **性能**：避免无必要的内存复制和隐式后台线程；为日志、配置读取等主要路径提供可复现的基准，暂不设缺少场景依据的吞吐指标。
- **兼容性**：采用语义化版本；在 0.x 阶段允许调整 API，但需要记录破坏性变更及迁移方式，首版不承诺 ABI 稳定。

### 5. 文档与测试

- README 提供环境要求、构建步骤、接入方式和最小应用示例。
- 公共 API 说明参数、返回值、错误情况、所有权及线程安全约定。
- 单元测试覆盖核心行为、边界条件和失败路径，并统一通过 CTest 执行。
- 集成测试覆盖配置覆盖顺序、启动失败清理、中断退出和多线程日志等跨模块行为。
- 示例至少包含一个读取配置并输出日志的命令行应用，以及一个可正常停止的后台任务应用。

### 6. 首版交付标准

1. 新环境按文档完成配置、构建、测试和示例运行，无需修改源码或硬编码路径。
2. 首版范围内的各模块满足对应验收标准，两个示例可运行。
3. Linux、macOS、Windows 的构建和测试通过，并记录实际验证的工具链版本。
4. 配置错误、文件访问失败、初始化失败等场景能返回明确错误，退出时没有遗漏的资源清理。
5. 对外交付公开头文件、所需库文件、CMake 接入配置、使用文档和依赖许可证说明。
