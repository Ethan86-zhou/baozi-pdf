# 包子PDF

**由 OpenAI Codex 根据用户需求生成并迭代。**

## 解决了什么困扰

这个项目来自仓库所有者的两个实际问题：

1. **只是想打开 PDF 阅读，却要安装体积很大的软件。** 浏览器已能满足普通阅读，但希望有一个轻量独立 EXE，用完即关，不带账号、更新器或后台常驻功能。
2. **Excel 中已有的内嵌 PDF，换默认阅读器仍打不开。** 没装 Adobe 时，即使将 Chrome / Edge 设为默认 PDF 阅读器，已有 Adobe OLE 对象也不一定能双击打开。

包子PDF通过 Windows 系统 PDF 能力控制体积，并为已验收的 Adobe OLE 类型提供只读兼容。保留 Excel 原有嵌入对象，不要求重新插入 PDF 或改成文件包。目标是轻量阅读和已有 Excel 对象兼容，不是完整替代 Adobe 的编辑功能。

## Codex 生成说明

代码、界面、OLE 兼容层、构建脚本与测试工具由 **OpenAI Codex** 根据用户要求生成、修改和调试；软件名称及交互方式由用户指定。本 README 也由 Codex 按用户要求编写。

开发期间使用真实 Excel 样本进行本机验收。**原工作簿、内嵌 PDF、私人截图、本机路径及原始日志不会公开。** 验收范围见 [ACCEPTANCE.md](ACCEPTANCE.md)，不宣称兼容所有 PDF、Office 或历史 Adobe 对象类型。

## 技术方案

原生 Windows 只读 PDF 阅读器，采用 C++ / Win32 + Windows.Data.Pdf 显示页面，PDFium 读取文字层并定位搜索结果。

## 使用

运行 `dist/包子PDF.exe`。x64 程序无需安装 C++ 运行库；`pdfium.dll` 必须与 EXE 放在同一目录。

发行包包含 `包子PDF.exe`、`pdfium.dll`、`PDFium-LICENSE.txt`、`licenses` 和使用说明。搜索组件约 7.5 MB，不再生成旧名称的兼容副本。

打开本机 PDF：点击“打开”、按 Ctrl+O、拖入文件，或将 PDF 拖到 EXE 上。

| 操作 | 快捷键 |
| --- | --- |
| 上页 / 下页 | 左右方向键、PageUp / PageDown |
| 首页 / 末页 | Home / End |
| 跳转页码 | Ctrl+G，输入后 Enter |
| 搜索 PDF 文字层 | Ctrl+F；输入后自动搜索，Enter / Shift+Enter 下一处 / 上一处 |
| 下一处 / 上一处搜索结果 | F3 / Shift+F3，或搜索栏按钮；首尾循环 |
| 关闭搜索栏 | Esc |
| 放大 / 缩小 | Ctrl+滚轮、+ / - |
| 适合整页 / 宽度 / 100% | Ctrl+0 / Ctrl+1 / Ctrl+2 |
| 顺时针旋转 | R |
| 全屏 / 退出全屏 | F11 / Esc |
| 关闭文档 / 退出 | Ctrl+W / Alt+F4 |
| 页面平移与翻页 | 滚轮上下移动；到页底/页顶继续滚动进入下页/上页，整页显示时直接翻页 |
| 页面平移 | 拖动页面、滚动条、上下方向键；Shift+滚轮横向滚动 |

搜索按文字层中的词或短语匹配，支持中文和英文，可勾选“区分大小写”。黄色标出当前页命中，橙色边框表示选中的命中，跳转时自动滚动到位置。搜索在后台执行，修改关键词或切换文档会取消旧搜索。关键词最多 256 个 UTF-16 代码单元；每次最多保留 5000 处，更多时显示 `5000+`。不做 OCR，扫描图片中的字不参与搜索；缺失或错误的 PDF 文字编码也会影响匹配。

## 系统 PDF 文件关联

选择 **帮助 → 关联 PDF / 设为默认阅读器**。程序会将包子PDF注册到 Windows 的 PDF“打开方式”和默认应用列表，并打开系统默认应用设置。选择 `.pdf` 并指定包子PDF后，双击硬盘上的 PDF 就会启动它。

这与 Excel 内嵌 PDF 关联是两个独立功能。程序使用 Windows 支持的关联注册，不改写系统保护的 UserChoice 哈希。移动 EXE 后应重新注册。

## Excel 中已有的 Adobe 内嵌 PDF

启用兼容后，在 Excel 内双击已支持类型的 PDF 图标即可；无需把普通 `.pdf` 默认应用改成包子PDF。

在其他电脑上，或移动了 EXE 后：打开包子PDF，选择“帮助 → 启用 Excel 内嵌 PDF”，然后重新打开 Excel。注册仅针对当前用户，不需要管理员权限。普通阅读无需注册。

恢复之前的对象关联：选择“帮助 → 恢复原有 Excel PDF 关联”。移动 EXE 后请用程序内菜单重新启用。私人机器上的快捷方式不随源码公开。

兼容范围：样本中实际使用的 Adobe CLSID `{B801CA65-A1FC-11D0-85AD-444553540000}`，原始 PDF 位于 OLE 复合存储的 `CONTENTS` 流。数据在内存中交给 Windows PDF 引擎，不提取到临时文件，不改写 Excel。保留已有嵌入对象；不要求重新插入或改成 Package。

本版本是阅读器，不实现 Adobe 编辑、新建 Adobe OLE 对象、PDF 自动化接口或全部历史 Adobe 类标识。启用会将当前用户该类对象的打开交给包子PDF；Adobe 已运行时，应先关闭 Adobe 和 Excel 再重开。Excel 以普通用户权限运行。

## 功能与资源边界

- 只读显示、单页浏览、打开、跳页、缩放、旋转、全屏、页内滚动；支持系统引擎能读取的加密 PDF，密码不持久保存。
- 支持全文文字层搜索、命中高亮与循环跳转；文件路径含中文、加密 PDF 和内存中的 Excel 内嵌 PDF 可搜索。密码只在当前文档打开期间保留在内存，不写入文件。
- 不提供文字选择复制、目录读取、编辑、OCR、表单填写、打印或连续多页滚动。
- 只有一个渲染工作线程，只保留当前页和当前渲染结果；新请求取代旧请求。单页位图限制为 1200 万像素，边长限制为 8192，超限时自动降低实际缩放并提示。
- 程序本身无联网客户端、更新器、账号、遥测、历史记录、开机启动、后台服务或计划任务。关闭阅读窗口后退出；OLE 激活但没有显示文档时最多等待 30 秒后退出。
- 普通阅读不写设置或注册表。系统 PDF 关联与 Excel 兼容仅在用户启用时注册到当前用户；Excel 恢复备份保存在 `HKCU\Software\BaoziPDF\OleBackup`。恢复时检查注册所有者，避免覆盖其他程序的后续修改。
- 页面渲染使用 Windows 系统引擎；附带不含 V8/XFA 的 PDFium，只用于文字层搜索。搜索线程一次处理一页，最多保留 5000 处命中的矩形，不生成全文索引或历史记录。
- 面向 Windows 10 1607 及以上的 x64 系统；本次实际验收环境见 `ACCEPTANCE.md`。未在其他系统版本上实测。

## 构建

需要 Visual Studio 2022 C++ Build Tools、Windows SDK 和 CMake。运行：

```powershell
.\build.ps1
```

首次构建从 `bblanchon/pdfium-binaries` 下载固定版本 `157.0.8086.0`（`chromium/8086`）的 Windows x64 包，校验固定 SHA-256 后缓存到 `third_party/pdfium`。后续构建使用缓存。构建脚本可自动查找 Visual Studio 自带的 CMake。

输出在 `dist`，包含 EXE、PDFium DLL 和许可证。MSVC C++ 运行库静态链接。依赖 Windows 的 C++/WinRT 头文件、PDF 与位图接口。

程序内嵌包子造型图标，供 EXE、窗口、任务栏及 PDF 文件关联使用。图标源文件位于 `src/assets/baozi.svg`；运行 `python tools/make_icon.py` 可用 Pillow 重新生成 PNG 和含 16–256 像素九种尺寸的 ICO。正常构建直接使用仓库内的 ICO，不需要 Python。

## 测试

`tests/make_fixtures.py` 使用仅供开发的 ReportLab、Pillow、pypdf 生成测试 PDF。这些工具及测试 PDF 均不随 EXE 运行。

```powershell
.\dist\包子PDF.exe --self-test "完整路径\tests\fixtures" "完整路径\tests\results\engine.tsv"
.\dist\包子PDF.exe --verify-pdf "完整路径\待验收.pdf" "完整路径\验收.tsv"
.\build\Release\OleProbe.exe "完整路径\oleObject1.bin" "完整路径\ole-probe.tsv"
```

先运行 `python tests/make_fixtures.py` 生成测试文档，然后执行 `ctest --test-dir build -C Release --output-on-failure`。覆盖滚轮规则，以及真实 PDF 的中英文搜索、短语、大小写、旋转矩形、扫描页、加密、内嵌数据、大文档结果上限与取消。

命令行接口另外支持 `--enable-excel`、`--disable-excel`、`--register-pdf`。开发工具在打包应用上下文中执行时，注册表可能被虚拟化；应通过 Windows 文件管理器或程序内菜单启用，让 Excel 的外部 COM 激活能够读取实际用户注册。

测试依赖见 `tests/requirements.txt`。生成器使用 Windows 自带的微软雅黑字体；生成的测试 PDF 不纳入仓库。用户提供的工作簿副本及截图仅保存在本地 `tests/private`，不纳入仓库或发行包。

## 技术参考

- [Microsoft PdfDocument API](https://learn.microsoft.com/en-us/uwp/api/windows.data.pdf.pdfdocument)
- [Microsoft PdfPageRenderOptions API](https://learn.microsoft.com/en-us/uwp/api/windows.data.pdf.pdfpagerenderoptions)
- [PDFium text search API](https://pdfium.googlesource.com/pdfium/+/refs/heads/main/public/fpdf_text.h)
- [PDFium Windows binaries and licenses](https://github.com/bblanchon/pdfium-binaries)
- [Microsoft OLE Compound Documents](https://learn.microsoft.com/en-us/windows/win32/com/compound-documents)
- [Microsoft IOleObject::DoVerb](https://learn.microsoft.com/en-us/windows/win32/api/oleidl/nf-oleidl-ioleobject-doverb)
