# arch_render — 架构浏览器离屏渲染 / 几何检查工具

把 `resources/arch/*.json`（或任意拓扑 JSON 文件）用真实的 `ArchScene`（与 Architecture tab 相同的代码）离屏渲染成 PNG，
或打印每条总线的折线坐标、标签矩形，并列出布局问题。内容作者调整 `x/y/w/h` 时用它快速验证布局，不需要启动 Ripes。

双语文本按 `--lang zh|en` 解析（默认 `zh`）；`arch_render_i18n.cpp` 代替 `src/i18n.cpp`，因此不依赖 RipesSettings。

## 构建（直接用 toolchain 里的 MinGW + Qt，不走 CMake）

在仓库根目录的 Git Bash 中：

```bash
bash tools/arch_render/build.sh
```

脚本编译 `archmodel.cpp`、`archscene.cpp`、`arch.qrc`、`arch_render_i18n.cpp`、`arch_render.cpp`，链接成 `build/arch_render/arch_render.exe`，
并用 `lrelease` 把 `resources/i18n/archexplorer_zh_CN.ts` 编译成 `build/arch_render/archexplorer_zh_CN.qm`（供 `--qm` 使用，使图例的类型名称显示中文）。

## 使用

```bash
# 无头运行：offscreen 平台 + 系统字体目录（否则中文显示为方块）
export QT_QPA_PLATFORM=offscreen QT_QPA_FONTDIR="C:/Windows/Fonts"
export PATH="/p/workspace/soc/toolchain/Qt/6.8.3/mingw_64/bin:$PATH"
R=build/arch_render/arch_render.exe
QM="--qm build/arch_render/archexplorer_zh_CN.qm"

$R --lang en all dump                     # 检查全部内置拓扑（英文），期望每个都是 0 problem(s)
$R --lang zh ps3 dump                     # 单个内置拓扑，并打印每条总线的折线 / 标签坐标
$R --lang en my_topology.json dump        # 检查任意 JSON 文件
$R --lang en my_topology.json out.png     # 渲染任意 JSON 文件（宽度至少 1200 px）
$R --lang en --step 0:3 my.json out.png   # 渲染第 0 个 flow 的第 3 步高亮状态
$R --lang zh $QM out_dir                  # 每个内置拓扑渲染 <id>.png 和 <id>_step.png（1:1 像素）
$R --lang zh $QM out_dir dark             # 深色调色板
$R --lang zh $QM docs/arch/images docs    # 重新生成 docs/arch 中引用的中文图
$R --lang en docs/arch/images docs        # 英文图（文件名加 _en）
```

`dump` 报告的问题：

| 行 | 含义 |
|---|---|
| `OVERLAP label X x block Y` | 总线 X 的标签压在块 Y 上 |
| `OVERLAP label X x label Z` | 两条总线的标签互相重叠 |
| `OVERLAP label X x legend` | 标签压在图例上 |
| `THROUGH bus X segment N x block Y` | 总线 X 的第 N 段穿过了非端点块 Y |
| `ELIDED  block Y label` | 块 Y 的粗体标题放不下，显示时被省略号截断 |
| `CLIPPED block Y sublabel` | 块 Y 的 sublabel 换行后高度不够，被裁掉 |
| `note: label X crosses bus Z` | 标签压在另一条连线上（只提示，不计入问题数；标签有底色，文字仍可读） |

注意：穿过 `kind: "bus"` 宽条（例如 PS3 的 EIB）的中断 / 信箱连线也会被报告为 `THROUGH`，是否接受由作者判断（PS3 的 5 条属于这种情况）。
