#pragma once
#include <stddef.h>

typedef enum { BOOT_MAIN, BOOT_AUDIO, BOOT_SLOT_COUNT } boot_slot_t;
enum { BOOT_START = 1, BOOT_I2C, BOOT_LCD, BOOT_RGB, BOOT_UI,
       BOOT_SERVICES, BOOT_RUNNING };
enum { AUDIO_START = 1, AUDIO_CODEC, AUDIO_USB, AUDIO_READY };

// Bounded RTC checkpoints, never Flash/NVS writes or a memory/core dump.
void boot_diag_init(void);
void boot_diag_stage(boot_slot_t slot, unsigned stage);
void boot_diag_audio_result(int error);
void boot_diag_usb_result(int error);
// ASCII allowlisted fields only; returns bytes actually stored, excluding NUL.
size_t boot_diag_format(char *out, size_t capacity);
