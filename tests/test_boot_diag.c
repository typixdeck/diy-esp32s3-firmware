#include <assert.h>
#include "../main/boot_diag.c"

int main(void)
{
    memset(&s_retained,0xa5,sizeof(s_retained));
    boot_diag_init();
    char line[320];
    assert(boot_diag_format(line,sizeof(line))==strlen(line));
    assert(strstr(line,"reset=poweron retained=0 boots=1"));
    boot_diag_stage(BOOT_MAIN,BOOT_SERVICES);
    boot_diag_stage(BOOT_AUDIO,AUDIO_CODEC);
    boot_diag_stage(BOOT_MAIN,999); // invalid stage cannot poison retained record
    mock_reset=ESP_RST_PANIC;boot_diag_init();
    boot_diag_audio_result(-1);boot_diag_usb_result(0);
    boot_diag_format(line,sizeof(line));
    assert(strstr(line,"reset=panic retained=1 boots=2"));
    assert(strstr(line,"prev_main=6 prev_audio=2 main=1 audio=0"));
    assert(strstr(line,"audio_err=-1 usb_err=0 heap=4096 largest=2048 up=123"));
    s_retained.stages[BOOT_MAIN].inverse=0; // reset between checkpoint stores
    boot_diag_init();boot_diag_format(line,sizeof(line));
    assert(strstr(line,"retained=0 boots=1"));
    mock_reset=ESP_RST_BROWNOUT;boot_diag_init();boot_diag_format(line,sizeof(line));
    assert(strstr(line,"reset=brownout retained=0"));
    char guard[10];memset(guard,'x',sizeof(guard));
    assert(boot_diag_format(guard,8)==7&&guard[7]==0&&guard[8]=='x');
    assert(!boot_diag_format(NULL,0));
    puts("boot_diag: reset classification, warm checkpoints, corruption/cold reset and bounded output pass");
}
