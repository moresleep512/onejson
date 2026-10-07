# onejson

一个用 C++23 写的 JSON 解析练手项目，主要用来熟悉 JSON 语法、嵌套结构解析、字符串处理和数字转换，也顺便练习 CMake 和单元测试。

目前实现了从文件读取 JSON，以及在内存中访问、修改 JSON 数据。代码主要放在头文件中，通过 CMake 的 `onejson::onejson` 目标使用。

## 已实现的功能

- 解析对象、数组、字符串、数字、布尔值和 `null`，支持对象与数组嵌套。
- 处理 UTF-8 字符串、常见转义字符和 `\uXXXX` Unicode 转义，包括代理对。
- 使用 `[]` 链式访问或修改对象成员、数组元素。
- 用 `Number` 保存数字的原始文本，按需转换为整数或浮点数。
- 检查文件读取错误、非法 JSON、无效 UTF-8 和同一对象中的重复键。
- 使用 GoogleTest 测试解析、数据访问和数字转换。

解析时使用栈和状态来记录当前容器；`JsonValue` 使用 `std::variant` 保存不同类型的值。

## 项目结构

```text
onejson/
├── CMakeLists.txt
├── include/
│   ├── onejson.hpp          # Json 接口和解析逻辑
│   └── base/
│       ├── JsonValue.hpp    # JSON 值、对象和数组
│       ├── Number.hpp       # 数字校验、保存和转换
│       └── WorkString.hpp   # 文件读取和 UTF-8 字符处理
└── tests/
    ├── CMakeLists.txt
    ├── onejson_test.cpp
    ├── json_value_test.cpp
    └── number_test.cpp
```

## 构建与测试

需要支持 C++23 的编译器、CMake 3.24 或更高版本，以及 Abseil 和 GoogleTest 的源码。

将两个依赖放在同一个目录下，目录结构如下：

```text
cxx-third-party/
├── abseil-cpp/
│   └── CMakeLists.txt
└── googletest/
    └── CMakeLists.txt
```

在项目根目录执行以下命令。以 Windows PowerShell 为例，依赖路径请换成自己的路径：

```powershell
$env:CXX_THIRD_PARTY_ROOT = "C:/dev/cxx-third-party"
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

`CXX_THIRD_PARTY_ROOT` 用于指定依赖源码的父目录，CMake 会通过 `add_subdirectory` 引入依赖。测试默认开启，可以用 `-DBUILD_TESTING=OFF` 关闭；当前配置即使关闭测试，也会检查两个依赖目录是否存在。

## 使用示例

### 从文件读取

先准备一个 UTF-8 编码的 `example.json` 文件：

```json
{
  "name": "onejson",
  "version": 1,
  "enabled": true,
  "items": [{ "name": "example" }]
}
```

通过文件路径构造 `Json`，再用 `[]` 和 `get<T>()` 读取数据：

```cpp
#include <iostream>
#include <stdexcept>
#include <string>

#include "onejson.hpp"

int main()
{
    try
    {
        const Json json("example.json");

        std::cout << json["name"].get<std::string>() << '\n';
        std::cout << json["version"].get<Number>().as<int>() << '\n';
        std::cout << std::boolalpha << json["enabled"].get<bool>() << '\n';
        std::cout << json["items"][0]["name"].get<std::string>() << '\n';
    }
    catch (const std::runtime_error& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
```

`Json` 构造函数接收的是文件路径，文件读取或解析失败时会抛出 `std::runtime_error`。相对路径以程序运行时的工作目录为准。

在已引入 onejson 的 CMake 配置中，自己的程序可以这样链接：

```cmake
add_executable(demo main.cpp)
target_link_libraries(demo PRIVATE onejson::onejson)
```

### 在内存中构造和修改

```cpp
Json json;
json["user"]["name"] = "Alice";
json["user"]["age"] = 18;
json["user"]["enabled"] = true;
json["user"]["tags"][0] = "C++";
json["user"]["optional"] = nullptr;

const auto& user = json["user"];
const int age = user["age"].get<Number>().as<int>();
const bool empty = user["optional"].is_null();
```

可修改的 `[]` 会创建缺失的对象成员，并按需扩展数组，新增元素默认为 `null`。对 `const` 对象使用 `[]` 时，键不存在或数组越界会抛出 `std::out_of_range`；`get<T>()` 的类型与实际值不符时会抛出 `std::bad_variant_access`。

### 数字转换

```cpp
Number number("1.2500e+03");
const std::string& text = number.raw(); // 保留原始文本 "1.2500e+03"
const int integer = number.as<int>();  // 1250
const double value = number.as<double>(); // 1250.0
```

转换为整数时会向零截断小数，超出目标类型范围时取该类型的边界值；负数转为无符号整数时返回 `0`。浮点转换超出可表示范围时会抛出 `std::runtime_error`。

## 当前范围

这是一个学习和练习用的项目，接口和实现会随着练习继续调整。目前从文件解析时要求根节点是对象 `{}`，还没有提供直接解析 JSON 文本、序列化或写回文件的接口。
