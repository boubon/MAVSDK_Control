<img alt="MAVSDK" src="cpp/docs/assets/site/sdk_logo_full.png" width="400">

# MAVSDK_Control

本仓库是 [mavlink/MAVSDK](https://github.com/mavlink/MAVSDK) 的 fork，在原项目基础上新增了一个用于无人机控制与通信链路安全仿真的 C++ 示例：

```text
examples/mavsdk_control/
```

该示例程序面向无人机安全仿真实验场景，使用 MAVSDK 和 MAVLink 协议从电脑或树莓派向 Pixhawk 飞控板发送控制指令，并在 AirSim 硬件在环仿真环境中验证无人机飞行状态。项目目标是用软件控制程序替代传统遥控器和接收机链路，从而便于对无人机控制通信链路进行测试、可视化和安全性分析。

## 项目背景

实验平台主要包括：

- AirSim 仿真环境
- QGroundControl，用于飞控基础配置和基线测试
- Pixhawk 飞行控制板
- 树莓派，作为机载电脑或控制端
- 串口、Wi-Fi、自组网等通信链路
- 基于 MAVLink 协议的 MAVSDK C++ 控制接口

项目关注无人机控制指令在不同通信链路中的传输方式，以及在外部干扰、多控制端接入或网络中断等情况下系统的响应表现。

## 新增示例

主要新增代码位于：

```text
examples/mavsdk_control/mavsdk_control.cpp
```

同目录下还包含对应的构建文件：

```text
examples/mavsdk_control/CMakeLists.txt
examples/mavsdk_control/Makefile
```

## 主要功能

- 支持通过串口、Wi-Fi、自组网连接 MAVLink 兼容飞控。
- 使用 MAVSDK 的 `Action`、`Telemetry`、`Offboard` 插件实现无人机控制。
- 进入 Offboard 模式后，通过 NED 坐标系速度指令控制无人机运动。
- 支持键盘控制：
  - `W` / `S`：上升 / 下降
  - `A` / `D`：左转 / 右转
  - 方向键：前进 / 后退 / 左移 / 右移
  - `E`：解锁
  - `Q`：锁定
  - `L`：降落
- 基于 SFML 实现图形化控制界面，支持鼠标点击按钮控制。
- 在图形界面中统一处理键盘和鼠标输入。
- 在界面中实时显示 MAVSDK 日志消息。
- 对异常通信状态进行警告提示，例如其他控制端接入、通信中断等。
- 使用异步任务管理处理解锁、降落等操作，避免阻塞图形界面。

## 实验场景

该控制程序用于硬件在环无人机仿真实验流程：

1. 配置 QGroundControl、AirSim 和 Pixhawk 飞控板。
2. 验证控制电脑或树莓派到飞控板、再到 AirSim 的通信链路。
3. 使用 MAVSDK 控制程序手动控制仿真无人机。
4. 在串口、Wi-Fi、自组网等不同链路间切换。
5. 模拟外部通信链路攻击，包括：
   - 其他控制端注入控制指令；
   - 通过 Wi-Fi 中断方式抢夺或破坏控制链路；
   - 使用 Wireshark 抓包分析通信协议和指令数据。

## 依赖

该示例使用 C++17 编写，主要依赖：

- MAVSDK
- SFML 2.4 或更高版本
- CMake 或 Make
- MAVLink 兼容飞控，例如 PX4/Pixhawk
- AirSim 或其他安全的仿真/测试环境

## 安全说明

本项目用于仿真和受控实验室环境。若要将控制程序用于真实无人机硬件，应先在仿真或硬件在环环境中充分测试，确认失控保护和安全策略，并遵守当地航空、无线电和实验室安全规范。

## Upstream MAVSDK

以下内容为原 MAVSDK 项目的 README。

[![Linux](https://github.com/mavlink/MAVSDK/actions/workflows/linux.yml/badge.svg?branch=main)](https://github.com/mavlink/MAVSDK/actions/workflows/linux.yml)
[![macOS](https://github.com/mavlink/MAVSDK/actions/workflows/macos.yml/badge.svg?branch=main)](https://github.com/mavlink/MAVSDK/actions/workflows/macos.yml)
[![Windows](https://github.com/mavlink/MAVSDK/actions/workflows/windows.yml/badge.svg?branch=main)](https://github.com/mavlink/MAVSDK/actions/workflows/windows.yml)
[![Docs](https://github.com/mavlink/MAVSDK/actions/workflows/docs_deploy.yml/badge.svg?branch=main)](https://github.com/mavlink/MAVSDK/actions/workflows/docs_deploy.yml)

## Description

[MAVSDK](https://mavsdk.mavlink.io/main/en/) is a set of libraries providing a high-level API to [MAVLink](https://mavlink.io/en/).
It aims to be:
- Easy to use with a simple API supporting both synchronous (blocking) API calls and asynchronous API calls using callbacks.
- Fast and lightweight.
- Cross-platform (Linux, macOS, Windows, iOS, Android).
- Extensible (using the `MavlinkDirect` plugin, or the soon-to-be-deprecated `MavlinkPassthrough` plugin).
- Fully compliant with the MAVLink standard/definitions.

In order to support multiple programming languages, MAVSDK implements a gRPC server in C++ which allows clients in different programming languages to connect to. The API is defined by the proto IDL ([proto files](https://github.com/mavlink/MAVSDK-Proto/tree/master/protos)).
This architecture allows the clients to be implemented in idiomatic patterns, so using the tooling and syntax expected by end users. For example, the Python library can be installed from PyPi using `pip`.

The MAVSDK C++ part consists of:
- The [core library](https://github.com/mavlink/MAVSDK/tree/main/cpp/src/mavsdk/core) implementing the basic MAVLink communication.
- The [plugin libraries](https://github.com/mavlink/MAVSDK/tree/main/cpp/src/mavsdk/plugins) implementing the MAVLink communication specific to a feature.
- The [mavsdk_server](https://github.com/mavlink/MAVSDK/tree/main/cpp/src/mavsdk_server) implementing the gRPC server for the language clients.

## Repos

- [MAVSDK](https://github.com/mavlink/MAVSDK) - this repo containing the source code for the C++ core.
- [MAVSDK-Proto](https://github.com/mavlink/MAVSDK-Proto) - Common interface definitions for API specified as proto files used by gRPC between language clients and mavsdk_server.
- [MAVSDK-Python](https://github.com/mavlink/MAVSDK-Python) - MAVSDK client for Python (first released on Pypi 2019).
- [MAVSDK-Swift](https://github.com/mavlink/MAVSDK-Swift) - MAVSDK client for Swift (used in production, first released 2018).
- [MAVSDK-Java](https://github.com/mavlink/MAVSDK-Java) - MAVSDK client for Java (first released on MavenCentral in 2019).
- [MAVSDK-Go](https://github.com/mavlink/MAVSDK-Go) - MAVSDK client for Go (work in progress).
- [MAVSDK-JavaScript](https://github.com/mavlink/MAVSDK-JavaScript) - MAVSDK client in JavaScript (proof of concept, 2019).
- [MAVSDK-Rust](https://github.com/mavlink/MAVSDK-Rust) - MAVSDK client for Rust (proof of concept, 2019).
- [MAVSDK-CSharp](https://github.com/mavlink/MAVSDK-CSharp) - MAVSDK client for CSharp (proof of concept, 2019).
- [Docs](https://github.com/mavlink/MAVSDK/tree/main/cpp/docs) - MAVSDK [docs](https://mavsdk.mavlink.io/main/en/) source.

## Docs

Instructions for how to use the C++ library can be found in the [MAVSDK docs](https://mavsdk.mavlink.io/main/en/) (links to other programming languages can be found from the documentation sidebar).

Quick Links:

- [Getting started](https://mavsdk.mavlink.io/main/en/cpp/#getting-started)
- [C++ API Overview](https://mavsdk.mavlink.io/main/en/cpp/#api-overview)
- [API Reference](https://mavsdk.mavlink.io/main/en/cpp/api_reference/)
- [Installing the Library](https://mavsdk.mavlink.io/main/en/cpp/guide/installation.html)
- [Building the Library](https://mavsdk.mavlink.io/main/en/cpp/guide/build.html)
- [Examples](https://mavsdk.mavlink.io/main/en/cpp/examples/)
- [FAQ](https://mavsdk.mavlink.io/main/en/faq.html)

## License

This project is licensed under the permissive BSD 3-clause, see [LICENSE.md](LICENSE.md).

## Maintenance

This project is maintained by volunteers:
- [Julian Oes](https://github.com/julianoes) ([sponsoring](https://github.com/sponsors/julianoes), [consulting](https://julianoes.com)).
- [Jonas Vautherin](https://github.com/JonasVautherin)

Maintenance is not sponsored by any company, however, hosting of the [docs](https://mavsdk.mavlink.io/main/en/) and the [forum](https://discuss.px4.io/c/mavsdk/) is provided by the [Dronecode Foundation](https://dronecode.org).

## Support and issues

If you just have a question, consider asking in the [forum](https://discuss.px4.io/c/mavsdk/).

If you have run into an issue, discovered a bug, or want to request a feature, create an [issue](https://github.com/mavlink/MAVSDK/issues). If it is important or urgent to you, consider sponsoring any of the maintainers to move the issue up on their todo list.

If you need private support, consider paid consulting:
- [Julian Oes consulting](https://julianoes.com)

(Create a pull request if you wish to be listed here.)
