
## 2026-11-25 崩溃修复测试版

已根据进程 25848 的转储定位退出清理阶段的失效控件指针访问，移除对原始控件地址逐项断开连接的循环，交由 QObject 自动清理连接。

对照回归：恢复旧清理代码时，释放控件内存页后退出测试出现 0xC0000005；修复版连续 30 次清理及原有回归全部通过。最初普通堆内存测试未复现，改用已释放内存页确保失效指针读取可被发现。

这证明已修复可复现的插件清理缺陷，不代表 CST 所有工作流程无崩溃或全部界面已汉化；长时间实机使用仍待验收。建议作为预发布测试版分享。转储文件和机器日志不进入发布包。

最新 DLL SHA256：85df9bdeec8cc38a6c98b4e861c9feb9fafeee2435e9f7ec769f6162f2356fb7

下文包含旧版历史记录；旧哈希、目录名及未修复状态不代表本版。

# CST 中文启动器 · 便携版

2026-11-25 更新：针对“视图”和“仿真”页截图补充专业术语及功能区命令适配；增加本地工程模板网页向导适配。请使用本文件夹的启动器重新启动 CST，旧进程不会热更新。

解压即可使用的中文界面插件，目标平台为 Windows x64、CST Studio Suite 2025 和 CST 自带的 Qt 5.15。使用者需要自行安装并正常授权 CST。本项目不包含 CST 主程序、许可证、Qt SDK 或厂商运行库。

## 使用方法

1. 下载完整 ZIP 并解压到任意本地目录，不要只下载一个 EXE，也不要在压缩包内直接运行。
2. 双击 `CstChineseLauncher.exe`，或双击 `start-zh.cmd`。
3. 若没有自动找到 CST，点击“选择程序…”，选择本机 CST 安装目录下 `AMD64/CST DESIGN ENVIRONMENT_AMD64.exe`。
4. 点击“启动中文版”，等待 CST 打开。已运行的 CST 不会被热更新，需在新的实例检查效果。

整个文件夹可以搬到另一台电脑。首次使用时重新选择那台电脑的 CST 即可。无需管理员权限，无需把 DLL 复制进 CST 安装目录，也不需要安装 C++ 编译环境。

路径和可选个人词库保存在 `%LOCALAPPDATA%/CST-Chinese-Portable/`。把 UTF-8 的 `user.tsv` 放到这里可覆盖词条，每行格式为 `英文<TAB>中文`，参数占位符须保持一致。发布包不含个人词库。退出由此入口启动的 CST 后，删除解压目录即可移除本插件；需要清除路径记录时再删除上述个人设置目录。

## 翻译范围与兼容性

- 包含 1510 条控件词条，另有 210 条工程模板向导专用词条。覆盖已收录的功能区、菜单、固定面板、部分对话框和导航分类，数量不等于全软件覆盖率。
- 导航树适配 CST 的 RemoteUI 绘制代理。仅在显示时翻译固定分类，保留工程模型、用户名称、数值和命令编号。
- 本机前一版实机验证基于 CST 可执行文件版本 `2025.2.0.0`、Qt `5.15.11.0`。启动器会拒绝非 2025、非 x64 或非 Qt 5.15 的安装。2025 的其他维护版本仍需要单独验收。
- 尚未覆盖全部求解器、自绘/第三方模块、结果单元格、动态日志和完整帮助文档。本版本不承诺整个软件 100% 汉化。
- 导航树辅助功能、复制和脚本接口可能仍读到原始英文，这是保留模型数据的结果。

工程模板向导使用 Qt WebEngine 网页，新增适配只作用于本机 CST 安装目录中的 `Library/ConfigurationWizard/index.html`。通过隔离的脚本环境翻译指定显示区域，并监听动态切页。保留链接、选项编号、输入值、单位及原始工作流程数据。不会修改厂商网页文件。网页适配使用 CST 2025 Qt5.15 MSVC x64 的已核对导出函数；不匹配时不启用。

术语示例：Orthographic＝正交投影，Cutting Plane＝剖切平面，Dimension＝尺寸标注，Field Import＝导入场数据，Voltage/Current Monitor＝电压/电流监视器。剖切设置中的 Normal＝法向；不将这个含义扩展到所有同名上下文。

本启动器仅为新启动的 CST 进程设置插件环境，不修改系统全局环境或厂商文件。默认关闭诊断输出。启动器的“已发出启动请求”只代表 Windows 接受了请求；许可证、启动提示和实际软件状态仍以 CST 窗口为准。

## 目录与开发

- `CstChineseLauncher.exe`：已编译中文界面入口（.NET Framework 4.x）。
- `CSTRibbonZh/plugins/generic/CstRibbonZh.dll`：已编译 x64 插件，词库内嵌。
- `src/Launcher.cs`：启动器源码。
- `src/CSTRibbonZh/`：插件源码、词库和构建脚本。
- `tools/`、`tests/`：构建、打包和验证。
- `TEST_RESULTS.md`：本次验证的范围与限制。

重建启动器：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/build-launcher.ps1 -Test
```

重建插件需要 MSVC x64、Windows SDK、Qt 5.15 MSVC x64 SDK 和本机 CST 2025 的运行库。不会下载这些依赖：

```powershell
./src/CSTRibbonZh/build.ps1 -Verify -CstRoot '<CST安装目录>' -QtSdk '<Qt SDK目录>'
Copy-Item ./src/CSTRibbonZh/plugins/generic/CstRibbonZh.dll ./CSTRibbonZh/plugins/generic/CstRibbonZh.dll
python ./src/CSTRibbonZh/tools/check_catalog.py
python ./tools/package.py --output '<输出ZIP路径>'
```

仅检查安装兼容性、不启动 CST：

```powershell
$p = Start-Process ./CstChineseLauncher.exe -ArgumentList '--check --cst-root "<CST安装目录>" --report "<检查报告路径>"' -Wait -PassThru
$p.ExitCode
```

返回 0 表示静态检查通过，1 表示失败；`--report` 指定的已有文件会被覆盖。`--launch --cst-root "<目录>"` 可跳过启动器窗口直接启动；无参数始终显示中文界面。

## GitHub 私有分发

本项目计划放在 private 仓库。只有仓库所有者和获授权协作者能够查看、克隆和下载，普通访客不能凭链接下载。可使用仓库的 Code → Download ZIP；ZIP 内包含启动成品。也可分发由 `tools/package.py` 生成的版本包。

发布范围仅包括本项目源码、词表、自有编译产物和说明；不包含厂商软件、个人工程、机器诊断日志或账户凭据。本项目为非官方中文界面插件，与 CST 官方产品无隶属关系。仓库尚未指定开源许可证；私有分享不等于授予公开再分发许可。
