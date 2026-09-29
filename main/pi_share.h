#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "pi_link.h"
#define PI_SHARE_FILES 24
#define PI_SHARE_MAX_BYTES (256 * 1024)
typedef struct { char name[41]; uint32_t size; } pi_share_file_t;
typedef enum { SHARE_NONE, SHARE_LIST, SHARE_FILE, SHARE_SCREEN } pi_share_kind_t;
typedef struct {
    bool configured, busy;
    int phase, sdk_error, http_status;
    uint32_t elapsed_ms;
    int error; /* 0 success, 1 offline, 2 clock, 3 request, 4 memory, 5 pairing/storage, 6 cancelled */
    pi_share_kind_t kind;
    uint8_t count;
    pi_share_file_t files[PI_SHARE_FILES];
    uint32_t bytes;
    char name[41];
} pi_share_snapshot_t;
void pi_share_init(void);
/* Only USB CDC may provide pairing lines; fixed prefixes and bounded input. */
void pi_share_receive(const char *line);
bool pi_share_take_tx(char *out, size_t capacity);
void pi_share_get_snapshot(pi_share_snapshot_t *out);
bool pi_share_get_status(pi_link_snapshot_t *out); /* fresh HTTPS only */
bool pi_share_request(pi_share_kind_t kind, const char *name);
void pi_share_cancel(void);
void pi_share_forget(void);
size_t pi_share_format_diagnostics(char *out, size_t capacity);
size_t pi_share_copy_data(size_t offset, void *out, size_t capacity);
