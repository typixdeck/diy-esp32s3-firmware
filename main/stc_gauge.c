// SPDX-License-Identifier: MIT
// STC3117 datasheet rev 4, sections 6.2, 6.5, 7.2. Independent implementation.
// Cell curve/shared RAM ABI: TypixNode MIT firmware, commit 7c1b0d7.
// EOC policy informed by LiJinLiJin/stc3117-fuel-gauge; see docs/battery.md.
#include "stc_gauge.h"
#include "board_pins.h"
#include <stdlib.h>
#include <string.h>

enum { RUN=0x10, VMODE=1, POR=0x10, BATFAIL=8, UVLO=0x80,
       SEED_MARK=0x5d, PENDING_MARK=0xa6, FULL_SOC=51200 };
static const uint16_t ocv_mv[16] = {
    3300,3450,3568,3640,3680,3700,3730,3750,
    3790,3820,3870,3900,3940,4020,4100,4200
};
static const uint8_t soc_table[16] = {
    0,6,12,20,30,40,50,60,80,100,120,130,140,160,180,200
};
static uint16_t le16(const uint8_t *p) { return p[0] | p[1]<<8; }
static void put16(uint8_t *p, uint16_t v) { p[0]=v; p[1]=v>>8; }
static int signed14(uint16_t v) { v &= 0x3fff; return v&0x2000 ? v-0x4000 : v; }
static uint8_t crc8(const uint8_t *p, size_t n) {
    uint8_t crc=0;
    while(n--) { crc^=*p++; for(int i=0;i<8;i++) crc=crc&128 ? (crc<<1)^7 : crc<<1; }
    return crc;
}
static esp_err_t read_reg(i2c_master_dev_handle_t d, uint8_t reg, uint8_t *p, size_t n) {
    return i2c_master_transmit_receive(d,&reg,1,p,n,100);
}
static esp_err_t write_reg(i2c_master_dev_handle_t d, uint8_t reg, const uint8_t *p, size_t n) {
    uint8_t out[33];
    if(n>32) return ESP_ERR_INVALID_ARG;
    out[0]=reg; memcpy(out+1,p,n);
    return i2c_master_transmit(d,out,n+1,100);
}
static esp_err_t write16(i2c_master_dev_handle_t d, uint8_t reg, uint16_t v) {
    uint8_t p[2]; put16(p,v); return write_reg(d,reg,p,2);
}
static esp_err_t write8(i2c_master_dev_handle_t d, uint8_t reg, uint8_t v) {
    return write_reg(d,reg,&v,1);
}
static bool ram_valid(const uint8_t *ram) { return le16(ram)==0x53a9 && !crc8(ram,16); }
static bool coefficients_valid(const uint8_t *r) {
    return le16(r+15)>=50 && le16(r+15)<=4095 && le16(r+17)>=50 && le16(r+17)<=4095;
}
void stc_gauge_invalidate(stc_gauge_t *s) {
    memset(s,0,sizeof(*s)); s->eoc_since_ms=-1;
}
static esp_err_t failed(stc_gauge_t *s, stc_status_t why, esp_err_t err) {
    stc_gauge_invalidate(s); s->status=why; return err;
}

// Fail fast at each write; pending RAM precedes changes and is recoverable on
// retry/reboot. A healthy takeover never rewrites coefficients or cell tables.
static esp_err_t seed(i2c_master_dev_handle_t dev, const uint8_t *r,
                      uint8_t *ram, int ocv_code, bool resume) {
    uint16_t cc=(BOARD_BATT_CAPACITY_MAH*10*250+6194)/12389;
    uint16_t vm=(BOARD_BATT_CAPACITY_MAH*BOARD_BATT_RI_MOHM*50+24444)/48889;
    if(resume) { cc=le16(ram+4); vm=le16(ram+6); }
    uint8_t mode=r[0]&0x0a; // retain BATD pull-up / alarm enable; never force CD
    memset(ram,0,16); put16(ram,0x53a9); put16(ram+4,cc); put16(ram+6,vm);
    ram[10]=PENDING_MARK; put16(ram+11,ocv_code); ram[15]=crc8(ram,15);
#define TRY(op) do { esp_err_t e=(op); if(e!=ESP_OK) return e; } while(0)
    TRY(write_reg(dev,0x20,ram,16));
    TRY(write8(dev,0,mode));
    uint8_t table[32];
    for(int i=0;i<16;i++) put16(table+2*i,ocv_mv[i]*100/55);
    TRY(write_reg(dev,0x30,table,32));
    TRY(write_reg(dev,0x50,soc_table,16));
    uint8_t table_check[32];
    TRY(read_reg(dev,0x30,table_check,32));
    if(memcmp(table,table_check,32)) return ESP_ERR_INVALID_STATE;
    TRY(read_reg(dev,0x50,table_check,16));
    if(memcmp(soc_table,table_check,16)) return ESP_ERR_INVALID_STATE;
    TRY(write16(dev,0x0f,cc)); TRY(write16(dev,0x11,vm));
    // Clear POR indication without soft reset or resetting the conversion count.
    TRY(write8(dev,1,(r[1]&0x61)|1));
    TRY(write8(dev,0,mode|RUN));
    TRY(write16(dev,0x0d,ocv_code));
    uint8_t verify[19];
    TRY(read_reg(dev,0,verify,sizeof(verify)));
    if(!(verify[0]&RUN) || (verify[0]&VMODE) || (verify[1]&(POR|BATFAIL|UVLO)) ||
       le16(verify+15)!=cc || le16(verify+17)!=vm || le16(verify+2)>FULL_SOC)
        return ESP_ERR_INVALID_STATE;
    put16(ram+2,le16(verify+2)); ram[8]=(le16(verify+2)+256)/512;
    ram[9]=1; ram[10]=SEED_MARK; memset(ram+11,0,4); ram[15]=crc8(ram,15);
    TRY(write_reg(dev,0x20,ram,16));
    uint8_t actual[16]; TRY(read_reg(dev,0x20,actual,16));
    return memcmp(actual,ram,16) ? ESP_ERR_INVALID_STATE : ESP_OK;
#undef TRY
}

esp_err_t stc_gauge_poll(stc_gauge_t *s, i2c_master_dev_handle_t dev,
                       int cw_mv, bool usb_present, int64_t now_ms) {
    uint8_t r[25], ram[16];
    if(!dev) return failed(s,STC_IO_ERROR,ESP_ERR_INVALID_STATE);
    esp_err_t err=read_reg(dev,0,r,sizeof(r));
    if(err!=ESP_OK) return failed(s,STC_IO_ERROR,err);
    int mv=(int16_t)le16(r+8)*22/10;
    int ma=signed14(le16(r+6))*588/1000; // 10 mOhm, positive = charging
    int soc=le16(r+2), counter=le16(r+4);
    if(r[24]!=0x16 || mv<2700 || mv>4300 || soc>FULL_SOC || (r[0]&4))
        return failed(s,STC_INVALID,ESP_ERR_INVALID_STATE);
    // Battery swap / UVLO can freeze measurements. Do not conceal it by seeding
    // a fabricated percentage or repeatedly resetting the chip/charge inhibit.
    if(r[1]&(BATFAIL|UVLO)) return failed(s,STC_FAULT,ESP_ERR_INVALID_STATE);
    if(cw_mv>=2700 && cw_mv<=4300 && abs(cw_mv-mv)>200)
        return failed(s,STC_INVALID,ESP_ERR_INVALID_STATE);
    err=read_reg(dev,0x20,ram,sizeof(ram));
    if(err!=ESP_OK) return failed(s,STC_IO_ERROR,err);
    bool running=(r[0]&RUN) && !(r[0]&VMODE) && !(r[1]&POR);
    bool saved=ram_valid(ram), seeded=saved && ram[10]==SEED_MARK;
    bool pending=saved && ram[10]==PENDING_MARK;
    if(!running && !(r[1]&POR) && seeded && coefficients_valid(r)) {
        // Resume an initialized gauge without losing its coulomb count or
        // another host's learned capacity. No new OCV or table writes.
        err=write8(dev,0,(r[0]&0x0a)|RUN);
        stc_gauge_invalidate(s); s->status=err==ESP_OK ? STC_WAIT : STC_IO_ERROR;
        return err;
    }
    // Adopt a calibrated Linux/ESP gauge even if it predates the shared marker.
    // Repair the known legacy DIY configuration, including SOC that drifted to
    // 1-10% while charging from its erroneous zero origin.
    int estimate=mv-ma*BOARD_BATT_RI_MOHM/1000;
    bool legacy=le16(r+15)==605 && le16(r+17)==614;
    bool zero=!seeded && estimate>3700 && (soc<512 || (legacy && soc<5120));
    bool continuous=s->tracking && now_ms>s->last_ms && now_ms-s->last_ms<=7500;
    bool fresh=continuous && now_ms-s->last_ms>=4000 && (uint16_t)counter!=s->counter;
    if(zero && running && !pending && !fresh) {
        stc_gauge_invalidate(s);
        s->tracking=true; s->last_ms=now_ms; s->counter=counter;
        s->status=continuous ? STC_STALE : STC_WAIT;
        return ESP_ERR_INVALID_STATE;
    }
    if(!running || pending || zero) {
        if(mv<3000 || abs(ma)>2500 || estimate<3000 || estimate>4300)
            return failed(s,STC_INVALID,ESP_ERR_INVALID_STATE);
        int code=estimate*100/55;
        int por_ocv=le16(r+13)*55/100;
        if((r[1]&POR) && por_ocv>=3000 && por_ocv<=4300) code=le16(r+13);
        bool resume=pending && le16(ram+11)>=5455 && le16(ram+11)<=7818 &&
                    abs(le16(ram+11)*55/100-estimate)<=200 &&
                    le16(ram+4)>=50 && le16(ram+4)<=4095 &&
                    le16(ram+6)>=50 && le16(ram+6)<=4095;
        if(resume) code=le16(ram+11);
        err=seed(dev,r,ram,code,resume);
        stc_gauge_invalidate(s);
        s->status=err==ESP_OK ? STC_SEEDED : STC_IO_ERROR;
        return err;
    }
    if(!coefficients_valid(r)) return failed(s,STC_INVALID,ESP_ERR_INVALID_STATE);
    if(!continuous) stc_gauge_invalidate(s);
    s->counter=counter; s->last_ms=now_ms; s->tracking=true;
    if(!fresh) {
        s->valid=false; s->charged=false; s->eoc_since_ms=-1;
        s->status=continuous ? STC_STALE : STC_WAIT;
        return ESP_ERR_INVALID_STATE;
    }
    s->valid=true; s->mv=mv; s->ma=ma; s->soc_raw=soc; s->status=STC_READY;
    if(!usb_present || ma < -100 || soc<95*512) s->full_latched=false;
    if(!usb_present) { s->charged=false; s->eoc_since_ms=-1; return ESP_OK; }
    if(ma>=200) { s->charged=true; s->charge_ms=now_ms; }
    if(s->charged && now_ms-s->charge_ms>1800000) s->charged=false;
    // Require actual USB power, an observed charging phase, fresh conversions,
    // a plausible near-full SOC and normal temperature. 30 s is elapsed time,
    // not UI refresh count. A MUX/bus gap invalidates the whole observation.
    int temp=(int8_t)r[10];
    bool taper=s->charged && !s->full_latched && soc>=80*512 &&
               mv>=4150 && mv<=4250 && abs(ma)<=100 && temp>=0 && temp<=45;
    if(!taper) { s->eoc_since_ms=-1; return ESP_OK; }
    if(s->eoc_since_ms<0) s->eoc_since_ms=now_ms;
    if(now_ms-s->eoc_since_ms<30000) return ESP_OK;
    err=write16(dev,2,FULL_SOC);
    if(err!=ESP_OK) return failed(s,STC_IO_ERROR,err);
    uint8_t check[2]; err=read_reg(dev,2,check,2);
    if(err!=ESP_OK || le16(check)!=FULL_SOC)
        return failed(s,STC_IO_ERROR,err==ESP_OK ? ESP_ERR_INVALID_STATE : err);
    // Preserve another driver's RAM layout/marker and learned coefficients.
    if(saved) {
        put16(ram+2,FULL_SOC); ram[8]=100; ram[15]=crc8(ram,15);
        err=write_reg(dev,0x20,ram,16);
        if(err!=ESP_OK) return failed(s,STC_IO_ERROR,err);
    }
    s->soc_raw=FULL_SOC; s->full_latched=true; s->charged=false;
    s->eoc_since_ms=-1; s->status=STC_FULL;
    return ESP_OK;
}
