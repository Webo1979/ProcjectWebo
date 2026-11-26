# 网络调试助手

Qt Widgets 提供界面，TCP 和 UDP 均通过 gensio 收发。Windows 便携包目标为 Windows 10 x64。

## 使用

解压整个 Windows ZIP，双击目录内的 `GensioNetDebug.exe`。程序需要同目录的 DLL 和 `platforms/qwindows.dll`。

- **TCP 客户端**。填写目标 IPv4 和端口，点“连接”。
- **TCP 服务端**。选择本地 IPv4 和端口，点“监听”。默认 `127.0.0.1` 仅用于本机调试，`0.0.0.0` 监听所有 IPv4 网卡。多个客户端连接后，用“发送到”选择目标。
- **UDP**。同时填写本地和目标 IPv4 与端口，点“绑定”。程序接收本地端口收到的数据报，并显示实际来源。发送使用预先填写的目标端点。
- 发送区支持 UTF-8 文本、可选 CRLF 和 HEX 字节。HEX 示例为 `48 65 6C 6C 6F 00`。接收内容可按 HEX 查看。
- 日志显示时间、端点和字节数。每条日志预览前 4096 字节，计数器记录全部收发字节。TCP 的一次 RX 代表一次读取，可能包含多次发送的数据，也可能只包含一次发送的一部分。需要逐字节核对时使用接收 HEX。

### 本机快速验证

打开两个实例。第一个选择 TCP 服务端，监听 `127.0.0.1:9000`。第二个选择 TCP 客户端，连接同一地址，在两边各发一条消息。测试 UDP 时，分别绑定本地端口 9000 和 9001，并把目标端口设为另一个实例的本地端口。

## 源码与构建

`main.cpp` 是界面和 I/O 入口。Qt 定时器轮询 gensio 的非阻塞事件服务，I/O 回调在 Qt 线程上运行。TCP 短写的剩余数据会在写就绪时继续发送。

Qt Creator 可直接打开 `CMakeLists.txt`。选择与 gensio 库 ABI 一致的 Qt 工具链，并设置 `GENSIO_ROOT` 为 gensio 安装目录。命令行构建示例：

```sh
cmake -S . -B build/app -DCMAKE_BUILD_TYPE=Release -DGENSIO_ROOT=/path/to/gensio
cmake --build build/app
```

Windows 原生构建可用 MSYS2 的 **MINGW64** 终端，执行以下命令：

```sh
pacman -S --needed git mingw-w64-x86_64-gcc mingw-w64-x86_64-qt5-base mingw-w64-x86_64-meson mingw-w64-x86_64-ninja mingw-w64-x86_64-cmake
bash build-windows.sh
```

脚本下载固定版本的 gensio，构建 TCP/UDP 支持，然后在 `build/portable/` 中生成程序和运行依赖。源码项目兼容 Qt 5 和 Qt 6 的 Widgets API。
