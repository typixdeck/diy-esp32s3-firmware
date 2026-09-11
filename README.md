# TypixNode ESP32-S3 Firmware

ESP32-S3 coprocessor firmware for the TypixNode / TypixDeck handheld
(Raspberry Pi CM4/CM5 based cyberdeck). This is the main production firmware
running on the on-board ESP32-S3-PICO-1.

## Features

- **USB UAC audio**: full-speed USB sound card (48 kHz, ES8389 codec,
  speaker / headphone with HP detect, dual microphones)
- **USB CDC console**: debug commands and remote maintenance
  (`EGGFLY_REBOOT_TO_BOOT_MODE` reboot-to-download magic string,
  `EGGFLY_SCREENSHOT` framebuffer dump, `EGGFLY_AW_DUMP` IO-expander
  forensics, theme / tab / screen-toggle commands)
- **LCD GUI**: 1024x768 RGB (DPI) panel dashboard with four themes,
  Chinese/English UI (FreeType + font partition), battery / power monitoring
- **Touch**: GT911 reset handling behind the CM/ESP display MUX
- **System management**: AW9523B IO expander (power sequencing, safe
  shutdown chain to the CM), INA219 / STC3117 / CW2015 gauges, RX8130 RTC,
  QMI8658 IMU

## Build

ESP-IDF v5.4.x, target `esp32s3`:

```bash
. $IDF_PATH/export.sh
idf.py set-target esp32s3
idf.py build
```

Note: flash size must be set manually to 8 MB
(`CONFIG_ESPTOOLPY_FLASHSIZE="8MB"`); auto-detection misreads it as 2 MB.

## Flash

- Normal (firmware alive): write `EGGFLY_REBOOT_TO_BOOT_MODE\n` to the CDC
  port, then flash with esptool (auto hard-reset applies).
- Bricked firmware: hold the side ESP32 BOOT button (SW3), tap RESET (SW1),
  flash, then press RESET again to leave download mode.

Prebuilt images are under `release/`.

## License

MIT — see [LICENSE](LICENSE). Third-party components under `components/` and
`managed_components/` keep their own licenses.

## Building from a fresh clone

`managed_components/espressif__esp_codec_dev/` only tracks our patched ES8389
driver files, so the component manager sees a partial directory and refuses to
run (`File .component_hash or CHECKSUMS.json ... does not exist`). Once:

```bash
cp -r managed_components/espressif__esp_codec_dev/device /tmp/es8389_patch
rm -rf managed_components/espressif__esp_codec_dev build
idf.py set-target esp32s3          # downloads esp_codec_dev 1.6.2 + .component_hash
cp -r /tmp/es8389_patch/* managed_components/espressif__esp_codec_dev/device/
idf.py build
```

Full flash image (bootloader + partition table + app + font partition):

```bash
python -m esptool --chip esp32s3 merge_bin -o release/typixdeck_esp32s3_full_$(date +%Y%m%d).bin \
  --flash_mode dio --flash_size 8MB --flash_freq 80m \
  0x0 build/bootloader/bootloader.bin 0x8000 build/partition_table/partition-table.bin \
  0x10000 build/typixdeck_esp32s3_lcd_init_touch_gui_uac_cdc.bin 0x210000 fonts/puhui_subset.ttf
```

`tools/gen_charset.py` regenerates `fonts/charset.txt` from the UI strings
(EN / 简体 / 繁體 / 日本語) before re-subsetting Alibaba PuHuiTi 3.0.
