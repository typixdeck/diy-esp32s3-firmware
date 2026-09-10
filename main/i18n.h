// TypixDeck UI 多语言表（2026-09-10）
//
// 用法：代码里保持 tr(en, zh_cn) 双参调用不动；繁體 / 日本語 由 tr() 用 en 串在本表
// 查找。查不到时繁體回退简体、日语回退英文，所以补翻译只需往表里加行，不用改调用点。
// 带 printf 格式的串必须保持 %-转换的个数、顺序、类型完全一致。
//
// 字体：阿里巴巴普惠体 3.0 已覆盖繁體字与日文假名（实测 cmap），fonts/charset.txt
// 需包含本表用到的全部字符（tools/gen_charset.py 从本文件 + ui.c 自动抽取）。
// 不含韩文 Hangul——加韩语需换 Noto Sans CJK 之类的泛 CJK 字体。
#pragma once

typedef struct {
    const char *en;   // 查找键（与 tr() 第一参数逐字节相同）
    const char *tw;   // 繁體中文（台灣用語）
    const char *ja;   // 日本語
    const char *zh;   // 可选：同一 en 键对应多个简体串时用简体串消歧（NULL=不限）
} i18n_entry_t;

static const i18n_entry_t k_i18n[] = {
    // ---- 开机 / 状态芯片 ----
    { "WAITING FOR PI VIDEO SIGNAL - NONE YET", "等待樹莓派視訊訊號——尚無訊號", "Pi の映像信号を待機中——まだ信号なし" },
    { "PRESS □ KEY FOR SYSTEM MONITOR", "按 □ 鍵進入系統監控", "□ キーでシステムモニターへ" },
    { "STILL WAITING FOR PI SIGNAL", "仍在等待樹莓派訊號", "まだ Pi の信号を待っています" },
    { "HINT: FLIP SW8 TO THE USB6 SIDE, THEN POWER ON", "提示：先把 SW8 撥到 USB6 側再開機", "ヒント：SW8 を USB6 側にしてから電源を入れる" },
    { "STILL HERE. SO IS THE PI, PROBABLY.", "還在等。樹莓派大概也在。", "まだここにいます。Pi もたぶん。" },
    { "PI SIGNAL LOCKED", "已鎖定樹莓派訊號", "Pi の信号をロック" },
    { "FPS LIVE", "FPS 即時", "FPS リアルタイム" },
    { "NO SIGNAL", "無訊號", "信号なし" },
    { "SIGNAL", "有訊號", "信号あり" },

    // ---- 仪表盘：电池 ----
    { "TOTAL %.0fmAh / LEFT %.0fmAh", "總容量 %.0fmAh，剩餘 %.0fmAh", "総容量 %.0fmAh、残り %.0fmAh" },
    { "TOTAL %.0fmAh (EST.)", "估算總容量 %.0fmAh", "推定総容量 %.0fmAh" },
    { "EST. %d MIN LEFT (AVG %.2fW)", "預計續航 %d 分鐘（均 %.2fW）", "残り約 %d 分（平均 %.2fW）" },
    { "EST. %.1f H LEFT (AVG %.2fW)", "預計續航 %.1f 小時（均 %.2fW）", "残り約 %.1f 時間（平均 %.2fW）" },
    { "RUNTIME ESTIMATING...", "續航估算中…", "残り時間を推定中…" },
    { "DISCHARGING", "放電中", "放電中" },
    { "CHARGING", "充電中", "充電中" },
    { "POWER MAY BE LOW", "供電可能不足", "電力不足の可能性" },
    { "FULL · USB POWERED", "已充飽 · USB 供電中", "満充電 · USB 給電中" },
    { "USB POWERED", "USB 供電中", "USB 給電中" },
    { "USB IN   %4.0f mA  %.2f W", "USB 輸入  %4.0f mA  %.2f W", "USB 入力  %4.0f mA  %.2f W" },
    { "USB IN   --", "USB 輸入  --", "USB 入力  --" },
    { "LOAD     %4.0f mA  %.2f W", "系統負載  %4.0f mA  %.2f W", "システム負荷 %4.0f mA  %.2f W" },
    { "LOAD     --", "系統負載  --", "システム負荷 --" },
    { "BATTERY  %+4.0f mA  %+.2f W", "電池電流  %+4.0f mA  %+.2f W", "電池電流  %+4.0f mA  %+.2f W" },
    { "BATTERY  -- (MUX AT PI)", "電池電流  --（MUX 在 Pi 側）", "電池電流  --（MUX は Pi 側）" },
    { "UP %lu S · PRESS □ TO RETURN TO PI", "已運行 %lu 秒 · 按 □ 鍵返回樹莓派畫面", "稼働 %lu 秒 · □ キーで Pi 画面へ戻る" },
    { "BATTERY", "電池", "バッテリー", "电池" },
    { "BATTERY", "電池曲線", "バッテリー履歴", "电池曲线" },   // Tab 栏
    { "BATT %+.0f mA · %+.2f W", "電池 %+.0f mA · %+.2f W", "電池 %+.0f mA · %+.2f W" },
    { "LOAD %.0f mA · %.2f W", "負載 %.0f mA · %.2f W", "負荷 %.0f mA · %.2f W" },
    { "INA219 READ FAIL", "INA219 讀取失敗", "INA219 読み取り失敗" },
    { "USB POWER", "USB 供電", "USB 給電" },
    { "CONNECTED", "已連接", "接続済み" },
    { "UNPLUGGED", "未連接", "未接続" },
    { "READ FAIL", "讀取失敗", "読み取り失敗" },
    { "SENSORS %d/%d", "感測器在位 %d/%d", "センサー検出 %d/%d" },
    { "LOAD", "負載電流", "負荷電流" },

    // ---- 电池曲线页 ----
    { "COLLECTING DATA...", "正在採集資料……", "データ収集中……" },
    { "%d/%d SAMPLES", "%d/%d 取樣", "%d/%d サンプル" },
    { "NOW: %d%%  %d.%03dV  %+dmA", "目前: %d%%  %d.%03dV  %+dmA", "現在: %d%%  %d.%03dV  %+dmA" },
    { "NOW: --%%  %d.%03dV  %+dmA", "目前: --%%  %d.%03dV  %+dmA", "現在: --%%  %d.%03dV  %+dmA" },
    { "1 HOUR WINDOW / 5S STEP", "1 小時視窗 / 5 秒步進", "1 時間ウィンドウ / 5 秒ステップ" },

    // ---- 触摸测试页 ----
    { "X:%4d Y:%4d  EVENTS:%lu", "X:%4d Y:%4d  事件:%lu", "X:%4d Y:%4d  イベント:%lu" },
    { "TOUCH THE CANVAS  EVENTS:%lu", "觸摸畫布試試  事件:%lu", "キャンバスに触れてください  イベント:%lu" },
    { "GT911 TOUCH TEST - DRAW ON CANVAS", "GT911 觸控測試——在畫布上繪製", "GT911 タッチテスト——キャンバスに描画" },
    { "CLEAR", "清除", "クリア" },

    // ---- PI 信号页 ----
    { "FPS (FROZEN, PROBE OFF)", "FPS（凍結值，偵測已關）", "FPS（固定値、プローブ停止）" },
    { "FPS (DPI REFRESH RATE)", "FPS（DPI 更新率）", "FPS（DPI リフレッシュレート）" },
    { "PI RGB OUTPUT NOT DETECTED", "未偵測到樹莓派 RGB 輸出", "Pi の RGB 出力を検出できません" },
    { "FRAMES: %lu", "幀計數: %lu", "フレーム数: %lu" },
    { "LAST VSYNC: %lld MS AGO", "上次 VSYNC: %lld 毫秒前", "最終 VSYNC: %lld ミリ秒前" },
    { "LAST VSYNC: NEVER", "上次 VSYNC: 從未", "最終 VSYNC: なし" },
    { "INT STORMS: %lu", "中斷風暴: %lu", "割り込みストーム: %lu" },
    { "PI TELEMETRY (CDC)", "Pi 遙測（CDC）", "Pi テレメトリ（CDC）" },
    { "MODEL: %s REV %s", "型號: %s Rev %s", "モデル: %s Rev %s" },
    { "MODEL: %s", "型號: %s", "モデル: %s" },
    { "CPU: %.1f°C", "核心溫度: %.1f°C", "CPU 温度: %.1f°C" },
    { "NVME: %s°C", "NVMe: %s°C", "NVMe: %s°C" },
    { "FAN: %s RPM", "風扇: %s RPM", "ファン: %s RPM" },
    { "THROTTLED: %s", "降頻旗標: %s", "スロットル: %s" },
    { "LOAD: %s", "負載: %s", "負荷: %s" },
    { "PI TELEMETRY: OFFLINE", "Pi 遙測: 離線", "Pi テレメトリ: オフライン" },
    { "typixdeck-pi-info.service not running", "typixdeck-pi-info 服務未運行", "typixdeck-pi-info サービスが未起動" },
    { "SENSE PATH: PI GPIO2 (DPI VSYNC) - R83 -", "偵測鏈路: Pi GPIO2 (DPI VSYNC) → R83 →", "検出経路: Pi GPIO2 (DPI VSYNC) → R83 →" },
    { "AW9523 P0_7 INT - GPIO5 EDGE COUNT", "AW9523 P0_7 中斷 → GPIO5 邊緣計數", "AW9523 P0_7 割り込み → GPIO5 エッジ計数" },
    { "PRESS □ TO RETURN TO PI", "按 □ 鍵返回樹莓派畫面", "□ キーで Pi 画面へ戻る" },

    // ---- 设置页 ----
    { "Language 语言", "語言 Language", "言語 Language" },
    { "Theme 主题", "主題 Theme", "テーマ Theme" },
    { "Touch to switch. Saved to flash (NVS).", "觸摸切換，寫入 Flash（NVS）永久保存", "タッチで切替。Flash（NVS）に保存" },
    { "ACTIVE", "目前", "選択中" },
    { "CYBER", "賽博", "サイバー" },
    { "MINIMAL", "極簡", "ミニマル" },
    { "TERMINAL", "終端", "ターミナル" },
    { "EV RING", "EV 儀表", "EV メーター" },

    // ---- Tab 栏 ----
    { "DASH", "儀表板", "ダッシュ" },
    { "TOUCH", "觸控測試", "タッチ" },
    { "PI SIG", "PI 訊號", "PI 信号" },
    { "SETUP", "設定", "設定" },

    // ---- 耳机抢屏页 ----
    { "HEADPHONE EVENT", "耳機插拔事件", "ヘッドホン検出イベント" },
    { "GUESS: UNPLUGGED", "推測：已拔出", "推定：取り外し" },
    { "GUESS: PLUGGED", "推測：已插入", "推定：接続" },
    { "POLARITY UNCONFIRMED", "極性待實測確認", "極性は未確認" },
    { "AUTO RETURN IN 2S", "2 秒後自動返回", "2 秒後に自動で戻る" },
};
#define K_I18N_COUNT (sizeof(k_i18n) / sizeof(k_i18n[0]))
