#include "pi_share.h"
#include "net_service.h"
#ifdef PI_SHARE_HOST_TEST
#include "share_fakes.h"
#else
#include "esp_http_client.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "mbedtls/base64.h"
#include "mbedtls/x509_crt.h"
#include "nvs.h"
#endif
#include <stdio.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

typedef struct { uint32_t version; char ip[16], token[65], cert[1536]; } config_t;
typedef struct { char line[160]; } pair_line_t;
static QueueHandle_t s_queue;
static SemaphoreHandle_t s_lock;
static config_t s_config;
static pi_share_snapshot_t s_state;
static pi_link_snapshot_t s_status;
static int64_t s_status_at;
static pi_share_kind_t s_request;
static char s_request_name[41], s_reply[80];
static uint8_t *s_data;
static atomic_bool s_cancel, s_forget, s_pair_overflow;
#define LOCK() xSemaphoreTake(s_lock, portMAX_DELAY)
#define UNLOCK() xSemaphoreGive(s_lock)
static bool valid_ip(const char *ip) {
    unsigned a,b,c,d; char end;
    if (strlen(ip) > 15 || sscanf(ip,"%u.%u.%u.%u%c",&a,&b,&c,&d,&end)!=4) return false;
    return a > 0 && a < 224 && a != 127 && b < 256 && c < 256 && d < 256;
}
static bool valid_name(const char *s) {
    size_t n = strlen(s); if (!n || n > 40 || s[0] == '.') return false;
    for (; *s; s++) if (!( (*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') ||
        (*s >= '0' && *s <= '9') || *s == '.' || *s == '-' || *s == '_')) return false;
    return true;
}
static bool valid_token(const char *s) {
    if (strlen(s)!=64) return false;
    for (;*s;s++) if (!((*s >= '0' && *s <= '9') || (*s >= 'a' && *s <= 'f'))) return false;
    return true;
}
static void reply(const char *s) { LOCK(); snprintf(s_reply,sizeof(s_reply),"TDPAIR %s\n",s); UNLOCK(); }
/* One worker owns a reusable TLS connection; never share its handle across tasks. */
static esp_http_client_handle_t s_client;
typedef struct { int error, phase, sdk, http; uint32_t ms, bytes; } request_diag_t;
static request_diag_t s_last_status, s_last_transfer, s_fetch_diag;
static uint32_t s_status_ok, s_status_fail;
static void close_client(void) {
    if (s_client) { esp_http_client_close(s_client); esp_http_client_cleanup(s_client); s_client=NULL; }
}
static int fetch_once(const char *path, uint8_t **data, size_t limit, size_t *length) {
    int64_t started=esp_timer_get_time();
    request_diag_t d={.phase=1};
    net_snapshot_t net; net_service_get_snapshot(&net);
    int error=3; uint8_t *body=NULL;
    if (!net.connected) { error=1; goto end; }
    if (!net.time_valid) { error=2; goto end; }
    char url[128], auth[80];
    snprintf(url,sizeof(url),"https://%s:38471%s",s_config.ip,path);
    snprintf(auth,sizeof(auth),"Bearer %s",s_config.token);
    if (!s_client) {
        esp_http_client_config_t cfg={.url=url,.cert_pem=s_config.cert,.timeout_ms=6000,
            .buffer_size=4096,.buffer_size_tx=1024,.disable_auto_redirect=true,.keep_alive_enable=true};
        s_client=esp_http_client_init(&cfg);
    } else if (esp_http_client_set_url(s_client,url)!=ESP_OK) { close_client(); goto end; }
    if (!s_client) { error=4; goto end; }
    esp_http_client_set_header(s_client,"Authorization",auth);
    memset(auth,0,sizeof(auth));
    esp_http_client_set_timeout_ms(s_client,6000);
    d.phase=2;
    d.sdk=esp_http_client_open(s_client,0);
    if (d.sdk!=ESP_OK) goto end;
    d.phase=3;
    int64_t size=esp_http_client_fetch_headers(s_client);
    d.http=esp_http_client_get_status_code(s_client);
    if (size<0) { d.sdk=(int)size; goto end; }
    if (d.http!=200 || (uint64_t)size>limit || esp_http_client_is_chunked_response(s_client)) goto end;
    body=heap_caps_malloc(size+1,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if (!body) { error=4; goto end; }
    d.phase=4;
    esp_http_client_set_timeout_ms(s_client,1000);
    size_t got=0;
    int64_t deadline=esp_timer_get_time()+15000000;
    while (got<(size_t)size) {
        if (s_cancel && strcmp(path,"/v1/status")) { error=6; goto end; }
        if (esp_timer_get_time()>=deadline) goto end;
        int count=esp_http_client_read(s_client,(char *)body+got,size-got>4096?4096:size-got);
        if (count==-ESP_ERR_HTTP_EAGAIN) continue;
        if (count<=0) { d.sdk=count; goto end; }
        got+=count; d.bytes=got;
    }
    body[size]=0; *data=body; *length=size; body=NULL; error=0; d.phase=5;
end:
    free(body);
    if (error) close_client();
    d.error=error; d.ms=(esp_timer_get_time()-started)/1000;
    s_fetch_diag=d;
    return error;
}
static int fetch(const char *path,uint8_t **data,size_t limit,size_t *length) {
    bool reuse=s_client!=NULL;
    int error=fetch_once(path,data,limit,length);
    /* A peer may have closed an idle keepalive socket. Retry only this bounded,
       read-only request once; auth, malformed data and cancellation never retry. */
    if (reuse && error==3 && s_fetch_diag.phase<=3 && !s_fetch_diag.http && !s_cancel)
        error=fetch_once(path,data,limit,length);
    return error;
}
static void read_status(void) {
    uint8_t *body=NULL; size_t length=0;
    int error=fetch("/v1/status",&body,159,&length);
    LOCK(); s_last_status=s_fetch_diag; if(error) s_status_fail++; else s_status_ok++; UNLOCK();
    if (error) return;
    pi_link_snapshot_t status={0}; char ip[16], extra;
    unsigned long uptime; long temp,freq,mem,disk,load;
    int n=sscanf((char *)body,"TD2 STATUS %lu %ld %ld %15s %ld %ld %ld %c",
                 &uptime,&temp,&freq,ip,&mem,&disk,&load,&extra);
    if (n==7 && uptime <= UINT32_MAX && temp >= -1 && temp <= 200000 &&
        freq >= -1 && freq <= 10000000 && mem >= -1 && mem <= 1048576 &&
        disk >= -1 && disk <= INT32_MAX && load >= -1 && load <= 1000000 &&
        (valid_ip(ip) || !strcmp(ip,"-"))) {
        status.online=true; status.transport=2; status.uptime_s=uptime;
        status.cpu_millicelsius=temp; status.cpu_khz=freq;
        status.mem_mib=mem; status.disk_mib=disk; status.load100=load;
        if (strcmp(ip,"-")) strcpy(status.ip,ip);
        LOCK(); s_status=status; s_status_at=esp_timer_get_time(); UNLOCK();
    }
    free(body);
}
static bool status_poll_needed(void) {
    pi_link_snapshot_t serial;
    pi_link_get_snapshot(&serial);
    return !serial.online;
}
static bool parse_files(char *body, pi_share_snapshot_t *state) {
    char *save=NULL;
    for (char *line=strtok_r(body,"\n",&save); line; line=strtok_r(NULL,"\n",&save)) {
        char name[41],extra; unsigned long size;
        if (state->count>=PI_SHARE_FILES || sscanf(line,"%40s %lu %c",name,&size,&extra)!=2 ||
            !valid_name(name) || size>PI_SHARE_MAX_BYTES) return false;
        pi_share_file_t *file=&state->files[state->count++];
        strcpy(file->name,name); file->size=size;
    }
    return true;
}
static void load_config(void) {
    nvs_handle_t h; size_t n=sizeof(s_config);
    if (nvs_open("pi_share",NVS_READONLY,&h)!=ESP_OK) return;
    esp_err_t err=nvs_get_blob(h,"pair",&s_config,&n); nvs_close(h);
    bool terminated = s_config.ip[15]==0 && s_config.token[64]==0 && s_config.cert[1535]==0;
    if (err!=ESP_OK || n!=sizeof(s_config) || !terminated || s_config.version!=1 ||
        !valid_ip(s_config.ip) || !valid_token(s_config.token)) memset(&s_config,0,sizeof(s_config));
}
static void worker(void *arg) {
    (void)arg;
    static config_t draft;
    static char encoded[2049];
    size_t used=0; bool pairing=false; int64_t pairing_at=0;
    load_config(); LOCK(); s_state.configured=s_config.version==1; UNLOCK();
    int64_t poll_at=0;
    for (;;) {
        if (pairing && esp_timer_get_time()-pairing_at>30000000) {
            pairing=false; used=0; memset(&draft,0,sizeof(draft)); memset(encoded,0,sizeof(encoded));
        }
        if (atomic_exchange(&s_pair_overflow,false)) {
            pairing=false; used=0; xQueueReset(s_queue);
            memset(&draft,0,sizeof(draft)); memset(encoded,0,sizeof(encoded)); reply("ERROR");
        }
        pair_line_t frame;
        if (xQueueReceive(s_queue,&frame,pdMS_TO_TICKS(100))==pdTRUE) {
            const char *line=frame.line;
            pairing_at=esp_timer_get_time();
            if (!strncmp(line,"TDPAIR BEGIN ",13)) {
                memset(&draft,0,sizeof(draft)); used=0; pairing=false; char tail;
                if (sscanf(line+13,"%15s %64s %c",draft.ip,draft.token,&tail)==2 &&
                    valid_ip(draft.ip) && valid_token(draft.token)) { pairing=true; reply("READY"); }
                else reply("ERROR");
            } else if (!strncmp(line,"TDPAIR CERT ",12) && pairing) {
                unsigned offset; char chunk[97],tail;
                if (sscanf(line+12,"%u %96s %c",&offset,chunk,&tail)==2 && offset==used &&
                    used+strlen(chunk)<=2048) {
                    size_t n=strlen(chunk); memcpy(encoded+used,chunk,n); used+=n; encoded[used]=0;
                    reply("CHUNK");
                } else { pairing=false; reply("ERROR"); }
            } else if (!strcmp(line,"TDPAIR COMMIT") && pairing) {
                pairing=false; size_t decoded=0;
                int rc=mbedtls_base64_decode((unsigned char *)draft.cert,sizeof(draft.cert)-1,&decoded,
                                              (unsigned char *)encoded,used);
                draft.cert[decoded < sizeof(draft.cert) ? decoded : 0]=0;
                mbedtls_x509_crt crt; mbedtls_x509_crt_init(&crt);
                bool valid=rc==0 && decoded>0 && mbedtls_x509_crt_parse(&crt,
                           (unsigned char *)draft.cert,decoded+1)==0;
                mbedtls_x509_crt_free(&crt);
                nvs_handle_t h; esp_err_t err=ESP_FAIL;
                if (valid && nvs_open("pi_share",NVS_READWRITE,&h)==ESP_OK) {
                    draft.version=1; err=nvs_set_blob(h,"pair",&draft,sizeof(draft));
                    if (err==ESP_OK) err=nvs_commit(h);
                    nvs_close(h);
                }
                if (err==ESP_OK) {
                    close_client(); s_config=draft;
                    LOCK(); s_state.configured=true; s_state.error=0; s_status_at=0; UNLOCK();
                    reply("OK"); poll_at=0;
                } else reply("ERROR");
                memset(&draft,0,sizeof(draft)); memset(encoded,0,sizeof(encoded)); used=0;
            } else reply("ERROR");
            memset(&frame,0,sizeof(frame));
        }
        if (s_forget) {
            nvs_handle_t h; esp_err_t err=nvs_open("pi_share",NVS_READWRITE,&h);
            if (err==ESP_OK) { err=nvs_erase_key(h,"pair");
                if (err==ESP_ERR_NVS_NOT_FOUND) err=ESP_OK;
                if (err==ESP_OK) err=nvs_commit(h);
                nvs_close(h); }
            if (err==ESP_OK) {
                close_client(); memset(&s_config,0,sizeof(s_config));
                LOCK(); free(s_data); s_data=NULL; memset(&s_state,0,sizeof(s_state)); s_status_at=0; UNLOCK();
            } else { LOCK(); s_state.error=5; UNLOCK(); }
            s_forget=false;
        }
        LOCK(); pi_share_kind_t request=s_request; s_request=SHARE_NONE;
        char name[41]; strcpy(name,s_request_name); UNLOCK();
        if (request && s_config.version==1) {
            char path[80]; strcpy(path,request==SHARE_LIST ? "/v1/files" : "/v1/screen");
            if (request==SHARE_FILE) snprintf(path,sizeof(path),"/v1/files/%s",name);
            uint8_t *data=NULL; size_t size=0;
            int error=fetch(path,&data,request==SHARE_LIST ? 1536 : PI_SHARE_MAX_BYTES,&size);
            pi_share_snapshot_t next={.configured=true,.kind=request,.bytes=size};
            if (!error && request==SHARE_LIST && !parse_files((char *)data,&next)) error=3;
            if (!error && request==SHARE_SCREEN && size!=320*240*2) error=3;
            if (s_cancel) error=6;
            LOCK(); s_state.busy=false; s_state.error=error; s_last_transfer=s_fetch_diag;
            s_state.phase=s_fetch_diag.phase; s_state.sdk_error=s_fetch_diag.sdk;
            s_state.http_status=s_fetch_diag.http; s_state.elapsed_ms=s_fetch_diag.ms;
            if (!error) {
                free(s_data); s_data=data; data=NULL;
                if (request==SHARE_LIST) s_state=next;
                else { s_state.kind=request; s_state.bytes=size; strcpy(s_state.name,name); }
            }
            UNLOCK(); free(data);
        }
        if (!pairing && s_config.version==1 && status_poll_needed() && esp_timer_get_time()>=poll_at) {
            read_status(); poll_at=esp_timer_get_time()+2000000;
        }
    }
}
void pi_share_init(void) {
    s_lock=xSemaphoreCreateMutex(); if (!s_lock) return;
    s_queue=xQueueCreate(4,sizeof(pair_line_t)); if (!s_queue) return;
    if (xTaskCreate(worker,"pi_share",8192,NULL,2,NULL)!=pdPASS) {
        vQueueDelete(s_queue); s_queue=NULL;
    }
}
void pi_share_receive(const char *line) {
    if (!s_queue || strncmp(line,"TDPAIR ",7) || strlen(line)>=159) return;
    pair_line_t frame={0}; strcpy(frame.line,line);
    if (xQueueSend(s_queue,&frame,0)!=pdTRUE) s_pair_overflow=true;
    memset(&frame,0,sizeof(frame));
}
bool pi_share_take_tx(char *out,size_t cap) {
    if (!s_lock) return false;
    LOCK(); bool ok=s_reply[0] && strlen(s_reply)+1<=cap;
    if (ok) {strcpy(out,s_reply); s_reply[0]=0;} UNLOCK(); return ok;
}
void pi_share_get_snapshot(pi_share_snapshot_t *out) {
    memset(out,0,sizeof(*out)); if (!s_lock) return;
    LOCK(); *out=s_state; UNLOCK();
}
bool pi_share_get_status(pi_link_snapshot_t *out) {
    if (!s_lock || !out) return false;
    LOCK(); int64_t age=esp_timer_get_time()-s_status_at;
    bool fresh=s_status_at>0 && age<8000000;
    if (fresh) { *out=s_status; out->age_ms=age/1000; } UNLOCK(); return fresh;
}
void pi_share_get_preferred_status(pi_link_snapshot_t *out) {
    if (!out) return;
    pi_link_get_snapshot(out);
    if (!out->online) pi_share_get_status(out);
}
bool pi_share_request(pi_share_kind_t kind,const char *name) {
    if (!s_queue || kind<SHARE_LIST || kind>SHARE_SCREEN || (kind==SHARE_FILE && (!name || !valid_name(name)))) return false;
    LOCK(); bool ok=s_state.configured && !s_state.busy && !s_request;
    if (ok) { s_cancel=false; s_request=kind; s_state.busy=true; s_state.error=0; snprintf(s_request_name,41,"%s",name?name:""); }
    UNLOCK(); return ok;
}
void pi_share_cancel(void) { s_cancel=true; }
void pi_share_forget(void) { s_cancel=true; s_forget=true; }
size_t pi_share_copy_data(size_t offset,void *out,size_t cap) {
    if (!s_lock) return 0;
    LOCK(); size_t n=s_data && offset<s_state.bytes ? s_state.bytes-offset : 0;
    if (n>cap) n=cap;
    if (n) memcpy(out,s_data+offset,n);
    UNLOCK(); return n;
}

size_t pi_share_format_diagnostics(char *out,size_t cap) {
    if (!s_lock || !out || !cap) return 0;
    LOCK();
    long age=s_status_at ? (long)((esp_timer_get_time()-s_status_at)/1000) : -1;
    int n=snprintf(out,cap,"TD_SHARE v=1 paired=%u busy=%u kind=%u count=%u bytes=%lu error=%d phase=%d sdk=%d http=%d ms=%lu status_error=%d status_phase=%d status_ms=%lu ok=%lu fail=%lu age=%ld\r\n",
        s_state.configured,s_state.busy,s_state.kind,s_state.count,(unsigned long)s_state.bytes,
        s_state.error,s_last_transfer.phase,s_last_transfer.sdk,s_last_transfer.http,(unsigned long)s_last_transfer.ms,
        s_last_status.error,s_last_status.phase,(unsigned long)s_last_status.ms,
        (unsigned long)s_status_ok,(unsigned long)s_status_fail,age);
    UNLOCK(); return n<0?0:(size_t)n<cap?(size_t)n:cap-1;
}
