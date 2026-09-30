# CM4 上的应用开发

工作目录：`/home/pi/Workspace/typixdeck/firmware/diy-esp32s3-firmware`。同步仓库：[typixdeck/diy-esp32s3-firmware](https://github.com/typixdeck/diy-esp32s3-firmware)。

## 目前可以改什么

0.4.2 的应用仍编译进 ESP32 固件。可以在 CM4 上改时钟排版、MIDI 界面/键盘映射、合成器算法并运行宿主测试；**没有独立下载、热加载插件的运行时**。动态轻应用的权限、资源预算与存储方案仍见 [规划](hmi-and-apps.md#动态应用是否可靠)。本次开发环境没有新增插件加载能力。

| 修改内容 | 源码入口 |
| --- | --- |
| 应用列表、时钟、MIDI 布局 | `main/ui.c` 的 `draw_apps()`，其中 `s_app == 2` 是时钟 |
| 钢琴绘图与点击区 | `main/ui.c` 的 `draw_piano()` 与琴键命中逻辑 |
| 音色、复音、音符包络 | `main/instrument_engine.c` / `.h` |
| 硬件音频与输入整合 | `main/instrument.c`；改这里需要额外真机验证 |
| 界面演示状态与交互断言 | `tools/preview_ui.c` |
| 合成器测试 | `tests/test_instrument_engine.c` |

先体验低风险的时钟界面。可以对 ChatGPT 说：

> 请在 /home/pi/Workspace/typixdeck/firmware/diy-esp32s3-firmware 中工作。先读 AGENTS.md 并同步 GitHub。把时钟应用的日期字体放大、间距调紧凑一些，只改显示布局。运行宿主测试并打开应用预览，不刷写 ESP32。

## 预览和检查

CM4 已有 Python 3、GTK3、C 编译器、pkg-config、FreeType。无需在试用阶段安装 ESP-IDF。

```sh
cd /home/pi/Workspace/typixdeck/firmware/diy-esp32s3-firmware
TYPIX_HOST_SANITIZERS=undefined python3 tools/preview_apps.py
```

窗口用实际 C framebuffer 绘图代码生成时钟、MIDI、应用列表的图片。修改后按 F5 重新编译渲染，Esc 退出。图像使用演示数据，不能直接点击图片弹琴，不会读取或刷写 ESP32。编译失败会隐藏旧图并在终端显示错误。

```sh
# 终端或 SSH 下仅生成图片，无需图形会话
TYPIX_HOST_SANITIZERS=undefined python3 tools/preview_apps.py --render-only
# 图在 build/preview-ui/，不提交 Git
TYPIX_HOST_SANITIZERS=undefined python3 tools/check.py
```

默认渲染与 C 检查同时开启 ASan/UBSan。当前 CM4 系统中 GCC 和 Clang 的 ASan 空程序均因地址空间映射失败退出，因此上面的命令显式选择 UBSan，未关闭行为断言。这个选择基于运行探测，不按 CM 型号判断；不同系统可保留默认。CM4 的结果不包含 ASan 内存检测，Mac 仍需跑默认完整检查；预览 manifest 会记录实际 sanitizer。真实硬件部署另需 ESP-IDF 5.5.1 的 Xtensa ESP32-S3 工具链：可将分支 push 到 GitHub，由已配置工具链的 Mac 拉取后运行 `tools/build.py` / `tools/package.py`。构建成功还需经过 Copilot 维护/写入和真机验收；此次同步不会自动刷机。

## GitHub 交接

```sh
git status --short --branch
# 只有工作区干净时才切换/更新
git switch main
git pull --ff-only
git switch -c cm4/clock-layout
# 编辑、验证、查看 diff 后
git add main/ui.c
git diff --cached
git commit -m "Adjust clock layout"
git push -u origin HEAD
```

Mac 上 `git fetch origin` 后首次 `git switch --track origin/cm4/clock-layout`；已有分支则切到同一分支并 `git pull --ff-only`。反方向也一样，换机器前提交并 push。未提交文件不会自动同步；有冲突时先保留两边修改，再合并处理，不强推覆盖。

公开仓库可直接拉取；push 需要 CM4 自己的 GitHub 授权。仓库禁用了 deploy key，不能依赖专用部署密钥。不要复制 Mac 私钥或把 token 写入 remote URL；未授权时仍可本地修改、测试和 commit。合并 main 后两端再更新。不要提交 `build/`、账号、SSH 私钥或真实设备数据。
