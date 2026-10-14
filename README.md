# StellaMora

基于 C++20、Qt 6 和 QML 的桌面天文摄影软件。

当前阶段：UI 基线已完成，后续进入设备与成像模块开发。设备选择、连接状态和参数界面目前不代表真实硬件已接入；真实采集、设备控制与遥测仍需实现。

## 源码结构

```text
StellaMora/
├── CMakeLists.txt          # 构建、资源打包与测试
├── CMakePresets.json       # 共享 Qt 配置入口，使用 QTDIR
├── .gitignore
├── README.md
├── src/
│   ├── main.cpp           # 程序入口
│   ├── app/               # 应用状态与页面/设置路由
│   ├── devices/           # 设备模型与连接状态管理
│   └── imaging/           # 图像统计，目前为预览直方图
├── qml/
│   ├── Main.qml
│   ├── theme/             # 主题
│   ├── controls/          # 通用控件
│   ├── shell/             # 主窗口、导航、标题栏、状态栏
│   ├── pages/             # 工作区页面
│   ├── panels/            # 设备连接、参数、导星与直方图面板
│   ├── settings/          # 各类设备设置页面
│   └── assets/            # icons/ 图标、images/ 预览图
├── resources/             # Windows 图标与 .rc 资源
└── tests/
    ├── cpp/               # C++ 测试
    ├── qml/               # 控件与交互测试
    └── cmake/             # 构建/资源契约检查
```

必须提交 `src/`、`qml/`、`resources/`、`tests/`、`CMakeLists.txt`。建议同时提交本说明、`.gitignore` 和 `CMakePresets.json`。
当前 CMake 无条件定义测试目标，因此不能省略 `tests/`。SVG、ICO、JPG 虽不是代码，也是构建/运行所需资源；不要按图片扩展名一概忽略。

不提交：`out/`（含编译、截图、WSL 输出和 UI 备份）、`.vs/`、`CMakeUserPresets.json`、本地历史 `docs/`、打包目录与临时文件。忽略不会删除这些本机文件。

## 构建与测试

依赖：CMake 3.24+、C++20 编译器、Qt 6.9+（Core、Gui、Qml、Quick、QuickControls2、Test），以及 Ninja。
Windows 已使用 Qt 6.10.1 / MSVC 2022 x64 构建。编译器与 Qt 套件必须匹配。

Windows：在已加载 MSVC x64 环境的 PowerShell 中执行，Qt 路径按本机修改：

```powershell
$env:QTDIR = 'D:/Qt/6.10.1/msvc2022_64'
$env:PATH = "$env:QTDIR/bin;" + $env:PATH
cmake -S . -B out/build/local-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug "-DCMAKE_PREFIX_PATH=$env:QTDIR"
cmake --build out/build/local-debug --parallel
ctest --test-dir out/build/local-debug --output-on-failure
```

Linux / WSL：安装对应 Linux Qt 套件与编译工具，设置 Qt 路径后执行：

```bash
export QTDIR=/path/to/Qt/gcc_64
export PATH="$QTDIR/bin:$PATH"
cmake -S . -B out/build/local-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH="$QTDIR"
cmake --build out/build/local-debug --parallel
ctest --test-dir out/build/local-debug --output-on-failure
```

可执行文件：Windows 位于构建目录下的 `StellaMora.exe`，Linux 位于构建目录下的 `bin/StellaMora`。
运行 Qt GUI 需要相应平台插件与桌面环境；WSL GUI 使用 WSLg。Qt 运行库由本机 Qt 安装提供，不提交到源码仓库。

### 打包分发（给最终用户）

开发者构建产物不需要、也不应该提交到 Git。给最终用户分发时运行：



这会在  生成一个可分发的目录（exe + Qt 运行时 + toupcam.dll），压缩后作为 **GitHub Release 附件**上传，接收者解压即用，无需安装 Qt。不要把 dist/ 目录或 DLL 提交进 Git 仓库。

Linux 真机连接 ToupTek 设备（相机、滤镜轮等）前需安装 USB 权限规则，否则设备枚举不到：

```bash
sudo cp third_party/toupcam/linux/udev/99-toupcam.rules /etc/udev/rules.d/
sudo udevadm control --reload && sudo udevadm trigger
```

`CMakeUserPresets.json` 仅保存个人 Visual Studio、Qt 路径与 WSL 配置。共享预设目前只有隐藏的 Qt 基础入口，不是完整的独立构建预设；上面的命令不依赖个人预设。

## 后续模块开发

以当前目录为基础逐步增加实现，不预先建立大量空模块：

1. `src/devices/`：优先接通一种真实设备的发现、连接、断开、错误反馈与状态，再扩展设备类型。
2. `src/imaging/`：接入真实相机帧，保留原始位深；将显示预览和原始数据处理分开。
3. 随功能落地增加采集、存储、导星和序列模块；QML 负责显示与交互，硬件调用和耗时处理放在 C++。
4. 每个模块附对应测试，避免破坏现有窗口、设备设置和预览交互。

## 首次推送注意事项

已有 Git 跟踪的文件不会因新增 `.gitignore` 自动退出跟踪。若只保留本说明而不上传历史 `docs/`，在项目目录执行：

```powershell
git rm -r --cached -- docs
git add -- .gitignore README.md CMakeLists.txt CMakePresets.json src qml resources tests
git diff --cached --stat
git diff --cached --name-only
```

`--cached` 只移除索引记录，不删除本机文档；已进入旧提交的内容仍留在 Git 历史中。
确认暂存区没有打包文件、个人配置和敏感信息后，再自行 commit 和 push。不要添加 `-f` 强制提交被忽略的构建目录。
公开仓库前，另行确认预览图片、图标及后续第三方 SDK 的分发许可；本次整理不代替授权审查，也未为项目选择开源许可证。
