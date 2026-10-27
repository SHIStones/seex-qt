# SeEx (Qt 重构版)

用 C++ / Qt 6 重写的 SeEx：把立创商城（LCSC）的元件批量导出成 Altium Designer 和 KiCad 能直接使用的库文件。

原版是 Rust + GPUI，位于 `../seex-master`。这个目录是 Qt 版本，界面与交互按原版一比一还原。

## 构建

需要：

- Visual Studio 2022（MSVC 14.40 及以上）
- Windows SDK 10.0.26100
- CMake 3.21+、Ninja
- Qt 6.8.3 msvc2022_64，含 `qtwebengine`、`qtwebchannel`、`qtpositioning`

Qt 用 `aqtinstall` 装即可，不需要 Qt 账号：

```bash
pip install aqtinstall
aqt install-qt windows desktop 6.8.3 win64_msvc2022_64 -O C:/Qt -m qtwebengine
aqt install-qt windows desktop 6.8.3 win64_msvc2022_64 -O C:/Qt -m qtwebchannel qtpositioning
```

然后：

```bash
./build.bat          # 配置 + 编译，产物在 build/SeEx.exe
```

`build.bat` 自己补齐了 Windows SDK 的 include/lib/bin 路径——`vcvars64.bat` 靠 `reg.exe`
查 SDK 位置，在受限环境里会被拦掉，所以脚本改成显式指定。

运行前需要 Qt 的 DLL 在 PATH 上：

```bash
PATH="/c/Qt/6.8.3/msvc2022_64/bin:$PATH" ./build/SeEx.exe
```

## 命令行

```bash
SeEx.exe --export C2040 C25804
```

按配置文件里的设置跑一次导出，进度打到 stderr，摘要打到 stdout。适合放进脚本或 CI。

## 目录结构

```
src/
  main.cpp                 入口：GUI 与 --export 两条路
  core/                    与界面无关的部分
    Extractor            从任意文本里抽 C 编号
    AppState             导入历史、匹配列表、导出设置
    Config               配置读写（与 Rust 版共用同一个文件）
    Metadata             符号参数与描述
    Util                 Windows-1252、GBK 修复、文件名清洗
  altium/                  Altium 二进制格式
    Cfb                  OLE 复合文档读写（SchLib/PcbLib 的容器）
    BinaryWriter         参数块与小端写入
    SchLib               符号库：解析 EasyEDA 符号、布局、写记录
    PcbLib               封装库：焊盘/走线/圆弧/区域记录
    Footprint            EasyEDA 封装数据 → PcbLib 元件
    LibPkg               .LibPkg 集成库包
  kicad/                   （尚未移植）
  net/
    LcedaClient          pro.lceda.cn 的搜索、详情、STEP/OBJ
  export/
    Exporter             一次导出的编排，跑在工作线程
  ui/
    Theme                配色与 QSS，数值与原版一致
    Components           卡片、行框、计数胶囊等
    MainWindow           顶栏 + 四个页面 + 状态栏
    ImportPage           手动导入、复制、清空、匹配列表
    HistoryPage          导入历史
    ExportPage           目标/范围/开关/路径/进度
    PreviewPage          WebView2 三栏预览
```

## 与 Rust 版的行为差异

- **配置文件共用**：仍然读 `%APPDATA%\ref42\SeExEgui\config\export_config.json`，
  两个版本可以互相切换而不丢设置。
- **界面**：四个页面、配色、文案与 Rust 版一致；顶栏 64px、状态栏、Toast 提示都保留。
- **预览**：用 `QWebEngineView` 加载 `item.szlcsc.com` 的产品页，注入脚本把
  「数据手册」弹窗裁成引脚 / 焊盘 / 3D 三栏。就绪信号从 `window.ipc.postMessage`
  改成 `window.__seexReady`，由宿主轮询。移植时踩到的三处坑：
  1. **弹窗只认 `pointerdown`**。它是 Radix 风格的组件，`.click()` 不触发，
     必须按 `pointerover → pointerenter → pointerdown → mousedown → pointerup →
     mouseup → click` 的顺序派发完整序列。
  2. **三栏容器的层级不固定**。不能假定它是弹窗的直接子节点，要从 `#containerId`
     往上走到弹窗的子级再取。
  3. **提示文字自绘**。`QLabel` 的富文本会忽略对齐标志，叠放布局又会把标签塞进
     错误的格子；`NoticeView` 直接在 `paintEvent` 里按自身矩形居中绘制，
     并且铺一层不透明底色——浏览器始终保持可见，否则收不到合成事件。
- **渲染进程的无障碍被关掉**：产品页弹窗里的关闭按钮是个 Chromium 判定的 "ignored node"，
  无障碍树遍历到它会直接 `Check failed: !IsIgnored()` 崩溃（`ax_node.cc`）。
  `main.cpp` 因此在创建 QApplication 前设置了
  `QTWEBENGINE_CHROMIUM_FLAGS=--disable-renderer-accessibility`。这个应用不使用无障碍接口，
  关掉没有副作用。

## 尚未移植

以下部分在 Rust 版里存在，Qt 版这一轮没有做：

| 模块 | 说明 |
|---|---|
| KiCad 导出 | `kicad/` 整个目录：符号、封装、库增量写入、3D 模型引用 |
| 3D 模型 | STEP 下载、`step_model` 的置板变换、`step_sanitizer` 的碎面清理、PcbLib 内嵌 |
| 追加已有 SchLib | PcbLib 已支持按原始流追加；SchLib 目前整库重写 |
| 封装的高级图元 | 自定义多边形焊盘、圆角矩形焊盘按矩形处理；无 3D Body |

导出前建议先用一个编号试一次，在 Altium 里打开确认无误后再批量跑。
