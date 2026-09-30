# DIY 固件开发约定

- 先读 README.md、docs/VOICE-CODING.md、docs/hmi-and-apps.md。当前时钟/MIDI 是内置 C 应用，没有可动态加载的插件 ABI 或解释器，不可声称复制脚本就能运行在 ESP32。
- CM4 可以编辑源码、跑宿主行为测试与界面渲染。tools/preview_apps.py 使用真实 C 绘图代码和演示状态，不访问设备；图片预览不验证真实触摸、声音、Wi-Fi 或硬件时序。
- 开工先 git status；干净 main 才 git pull --ff-only。修改放任务分支，不覆盖未提交内容、不强推、不把整个设备目录 rsync 到仓库。
- 普通应用改动优先限制在 main/ui.c、main/instrument_engine.c 及对应 tests/。不要顺便改变板级供电、I²C/MUX、USB 描述符、Flash 分区、NVS 或启动逻辑。
- 验证用 python3 tools/check.py；当前 CM4 经探测 ASan 无法初始化，须按指南显式设置 TYPIX_HOST_SANITIZERS=undefined，结果只能标为 UBSan + 行为检查，不能声称已通过 ASan；生成预览用 python3 tools/preview_apps.py --render-only。真正固件构建仍需 ESP-IDF 5.5.1 的 ESP32-S3 工具链，宿主 cc 不能产出可刷写镜像。
- 用户只要求代码编辑/预览/同步时，不连接串口、不刷写或复位。设备维护先读工作区 docs/03-integration-surfaces.md，并遵循 Copilot 已验证的维护流程；已有明确用户授权时按其范围执行。
- 凭据、配对信息、NVS、真实截图、设备备份和未脱敏日志不得提交。演示图明确标注；不要把宿主测试通过当成硬件验收。
