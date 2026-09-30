"""Exercise the unchanged production read function with I2C outcomes, no device."""
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from host_build import sanitizer_flags
import subprocess,tempfile
root=Path(__file__).parents[1]
source=(root/'main/gt911.c').read_text()
function=source[source.index('esp_err_t gt911_read('):]
harness=r'''
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
typedef int esp_err_t;typedef void *i2c_master_dev_handle_t;
typedef struct {int count,x,y;} gt911_touch_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_ARG -2
#define ESP_ERR_INVALID_RESPONSE -3
#define ESP_ERR_NOT_FOUND -4
#define REG_STATUS 0x814e
#define REG_POINT0 0x814f
static int ack,failed,ack_fail;static uint8_t report;
static int reg_read(void*d,uint16_t r,uint8_t*b,size_t n) {
 (void)d;if(r==REG_STATUS){*b=report;return ESP_OK;}
 assert(r==REG_POINT0&&n==8);if(failed)return ESP_FAIL;
 memcpy(b,(uint8_t[]){0,32,0,144,1,0,0,0},8);return ESP_OK;
}
static int reg_write_u8(void*d,uint16_t r,uint8_t v) {(void)d;assert(r==REG_STATUS&&v==0);ack++;return ack_fail?ESP_FAIL:ESP_OK;}
'''
main=r'''
int main(void) {
 gt911_touch_t touch={.count=4,.x=999,.y=999};
 report=0;assert(gt911_read(NULL,&touch)==ESP_ERR_NOT_FOUND&&ack==0);
 report=0x81;assert(gt911_read(NULL,&touch)==ESP_OK&&touch.count==1&&touch.x==32&&touch.y==400&&ack==1);
 report=0;assert(gt911_read(NULL,&touch)==ESP_ERR_NOT_FOUND&&ack==1);
 report=0x80;assert(gt911_read(NULL,&touch)==ESP_OK&&touch.count==0&&ack==2);
 report=0x86;assert(gt911_read(NULL,&touch)==ESP_ERR_INVALID_RESPONSE&&touch.count==0);
 report=0x81;failed=1;assert(gt911_read(NULL,&touch)==ESP_FAIL&&touch.x==0&&touch.y==0);
 failed=0;ack_fail=1;assert(gt911_read(NULL,&touch)==ESP_FAIL);
 puts("gt911: no-report is not release, no premature ACK, valid press/release, bad count and I2C failure passed");
}
'''
with tempfile.TemporaryDirectory(prefix='typix-touch-test-') as work:
    work=Path(work);(work/'test.c').write_text(harness+function+main)
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror',*sanitizer_flags(),str(work/'test.c'),'-o',str(work/'test')],check=True)
    subprocess.run([str(work/'test')],check=True)
