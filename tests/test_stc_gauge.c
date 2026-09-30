/* Compile the production driver against a deterministic I2C model. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "stc_gauge.h"
#include "sensors.h"
int64_t test_now_ms;
static uint8_t regs[96];
static int dev, read_fail=-1, write_fail=-1, nwrites, nsoc, nocv, io_count;
static bool readback_bad;
static struct { int reg, size; uint8_t data[32]; } writes[64];
static void put16(int reg,int value) { regs[reg]=value; regs[reg+1]=value>>8; }
static int get16(int reg) { return regs[reg] | regs[reg+1]<<8; }
static uint8_t crc(const uint8_t *p, int n) {
    unsigned x=0;
    for(int i=0;i<n;i++) { x^=p[i]; for(int j=0;j<8;j++) x=(x&128 ? (x<<1)^7 : x<<1)&255; }
    return x;
}
static void save_ram(void) {
    memset(regs+32,0,16); put16(32,0x53a9); put16(34,get16(2));
    put16(36,get16(15)); put16(38,get16(17)); regs[42]=0x5d; regs[47]=crc(regs+32,15);
}
esp_err_t i2c_master_transmit_receive(void *d,const uint8_t *tx,size_t nt,uint8_t *rx,size_t nr,int timeout) {
    assert(d==&dev && nt==1 && timeout==100 && tx[0]+nr<=sizeof(regs)); ++io_count;
    if(tx[0]==read_fail) return ESP_FAIL;
    memcpy(rx,regs+tx[0],nr);
    if(readback_bad && tx[0]==2) rx[0]^=8;
    return ESP_OK;
}
esp_err_t i2c_master_transmit(void *d,const uint8_t *tx,size_t n,int timeout) {
    assert(d==&dev && n>=2 && n<=33 && timeout==100 && tx[0]+n-1<=sizeof(regs)); ++io_count;
    int w=nwrites++; assert(w<64); writes[w].reg=tx[0]; writes[w].size=n-1;
    memcpy(writes[w].data,tx+1,n-1);
    if(w==write_fail) return ESP_FAIL;
    memcpy(regs+tx[0],tx+1,n-1);
    if(tx[0]==2) ++nsoc;
    if(tx[0]==13) { ++nocv; put16(2,65*512); } // fake chip's OCV->SOC conversion
    return ESP_OK;
}
static void fixture(stc_gauge_t *s) {
    stc_gauge_invalidate(s); memset(regs,0,sizeof(regs));
    regs[0]=0x18; regs[24]=0x16; regs[10]=25;
    put16(2,87*512); put16(4,10); put16(8,1896); // 4171 mV
    put16(13,7584); put16(15,484); put16(17,344);
    save_ram(); test_now_ms=0; nwrites=nsoc=nocv=io_count=0;
    read_fail=write_fail=-1; readback_bad=false;
}
static int poll(stc_gauge_t *s,bool usb) {
    return stc_gauge_poll(s,&dev,-1,usb,test_now_ms);
}
static int tick(stc_gauge_t *s,bool usb,int ma) {
    test_now_ms+=5000; put16(4,(uint16_t)(get16(4)+1));
    int raw=ma*1000/588; put16(6,raw&0x3fff);
    return poll(s,usb);
}
static void ready(stc_gauge_t *s) { assert(poll(s,false)!=ESP_OK); assert(tick(s,false,0)==ESP_OK && s->valid); }
int main(void) {
    stc_gauge_t s;
    fixture(&s); ready(&s); assert(nwrites==0 && s.soc_raw==87*512);
    /* Existing learned coefficients and an older Linux RAM without marker survive. */
    fixture(&s); put16(15,560); put16(17,398); memset(regs+32,0,16);
    ready(&s); assert(nwrites==0 && get16(15)==560 && get16(17)==398);
    fixture(&s); regs[0]=0x08; put16(15,560); put16(17,398); save_ram();
    assert(poll(&s,true)==ESP_OK && nwrites==1 && nocv==0 && nsoc==0);
    assert(regs[0]==0x18 && get16(2)==87*512 && get16(15)==560 && get16(17)==398);
    ready(&s);
    /* Mixed-mode reads are signed and range checked. */
    assert(tick(&s,false,-850)==ESP_OK && s.ma < -848 && s.ma > -852);
    put16(4,65535); poll(&s,false); assert(tick(&s,false,0)==ESP_OK && get16(4)==0);
    /* Stale conversions and failed reads cannot masquerade as current SOC. */
    test_now_ms+=5000; assert(poll(&s,false)!=ESP_OK && !s.valid && s.status==STC_STALE);
    assert(tick(&s,false,0)==ESP_OK); read_fail=0;
    assert(tick(&s,false,0)!=ESP_OK && !s.valid && s.status==STC_IO_ERROR);
    /* Never reset/seed a real empty pack, fault, wrong device or voltage conflict. */
    for(int bit=8;bit<=128;bit*=16) {
        fixture(&s); regs[1]=bit; assert(poll(&s,true)!=ESP_OK && s.status==STC_FAULT && nwrites==0);
    }
    fixture(&s); put16(2,0); put16(8,1273); ready(&s); assert(nwrites==0 && s.soc_raw==0);
    fixture(&s); regs[24]=0; assert(poll(&s,true)!=ESP_OK && nwrites==0);
    fixture(&s); assert(stc_gauge_poll(&s,&dev,3700,true,0)!=ESP_OK && nwrites==0);
    fixture(&s); put16(2,65535); assert(poll(&s,true)!=ESP_OK && nwrites==0);
    fixture(&s); put16(8,65535); assert(poll(&s,true)!=ESP_OK && nwrites==0);
    fixture(&s); read_fail=32; assert(poll(&s,true)!=ESP_OK && nwrites==0);
    /* Cold-start OCV seed: entire tables at disjoint addresses, shared CRC last. */
    fixture(&s); regs[0]=0x0b; regs[1]=0x10; put16(2,0); memset(regs+32,0,16);
    assert(poll(&s,true)==ESP_OK && s.status==STC_SEEDED && !s.valid);
    assert(get16(15)==484 && get16(17)==344 && nocv==1 && nsoc==0);
    assert(writes[0].reg==32 && writes[0].data[10]==0xa6);
    assert(writes[2].reg==48 && writes[2].size==32);
    assert(writes[3].reg==80 && writes[3].size==16);
    assert(get16(48)==6000 && get16(78)==7636 && regs[95]==200);
    assert(regs[42]==0x5d && crc(regs+32,16)==0 && get16(34)==65*512);
    assert(regs[0]==0x1a); // alarm + BATD pull-up preserved, no force CD
    int seed_writes=nwrites; ready(&s); assert(nwrites==seed_writes);
    /* Legacy DIY can count from zero up to a few percent without being seeded. */
    fixture(&s); memset(regs+32,0,16); put16(2,3*512); put16(15,605); put16(17,614);
    assert(poll(&s,true)!=ESP_OK && nwrites==0); // do not seed a frozen register
    test_now_ms+=5000; assert(poll(&s,true)!=ESP_OK && nwrites==0);
    assert(tick(&s,true,0)==ESP_OK && s.status==STC_SEEDED && nocv==1);
    /* Every failed write stops the transaction, no success marker, retry works. */
    for(int fail=0;fail<seed_writes;fail++) {
        fixture(&s); regs[0]=0x0b; regs[1]=0x10; put16(2,0); memset(regs+32,0,16);
        write_fail=fail; assert(poll(&s,true)!=ESP_OK && !s.valid && nwrites==fail+1);
        assert(!(regs[42]==0x5d && crc(regs+32,16)==0));
        write_fail=-1; assert(poll(&s,true)==ESP_OK && s.status==STC_SEEDED);
        assert(regs[42]==0x5d && !crc(regs+32,16));
    }
    for(int reg=48;reg<=80;reg+=32) {
        fixture(&s); regs[0]=0x0b; regs[1]=0x10; put16(2,0); memset(regs+32,0,16);
        read_fail=reg; assert(poll(&s,true)!=ESP_OK && !s.valid && regs[42]==0xa6);
        read_fail=-1; assert(poll(&s,true)==ESP_OK && regs[42]==0x5d);
    }
    /* ±100 mA taper for >=30 real seconds after charge, with verified USB. */
    fixture(&s); ready(&s); assert(tick(&s,true,300)==ESP_OK);
    for(int i=0;i<6;i++) { assert(tick(&s,true,i%2 ? -86 : 67)==ESP_OK); assert(nsoc==0); }
    assert(tick(&s,true,67)==ESP_OK && nsoc==1 && s.status==STC_FULL && s.soc_raw==51200);
    assert(get16(34)==51200 && !crc(regs+32,16));
    for(int i=0;i<15;i++) tick(&s,true,50);
    assert(nsoc==1 && get16(15)==484); // no repeated snap or capacity coefficient learning
    /* High voltage alone, no USB or no charge history must never force 100%. */
    for(int usb=0;usb<2;usb++) {
        fixture(&s); ready(&s); for(int i=0;i<30;i++) tick(&s,usb,50);
        assert(nsoc==0);
    }
    /* Bus gap, MUX, overtemperature, absent USB and below-threshold reset EOC. */
    for(int what=0;what<6;what++) {
        fixture(&s); ready(&s); tick(&s,true,300);
        for(int i=0;i<4;i++) tick(&s,true,50);
        if(what==0) { read_fail=0; tick(&s,true,50); read_fail=-1; }
        if(what==1) stc_gauge_invalidate(&s);
        if(what==2) { test_now_ms+=8000; tick(&s,true,50); }
        if(what==3) { regs[10]=55; tick(&s,true,50); regs[10]=25; }
        if(what==4) tick(&s,false,50);
        if(what==5) { put16(8,1850); tick(&s,true,50); put16(8,1896); }
        for(int i=0;i<3;i++) tick(&s,true,50);
        assert(nsoc==0);
    }
    /* A failed SOC write/readback is not declared full. */
    for(int fail=0;fail<2;fail++) {
        fixture(&s); ready(&s); tick(&s,true,300);
        for(int i=0;i<6;i++) tick(&s,true,50);
        if(fail==0) write_fail=0; else readback_bad=true;
        assert(tick(&s,true,50)!=ESP_OK && !s.valid && s.status!=STC_FULL);
    }
    /* Public adapter: cached snapshots expire; Pi ownership blocks all STC I/O. */
    fixture(&s); sensors_init(); stc3117_poll(&dev,NULL,false);
    test_now_ms+=5000; put16(4,11); stc3117_poll(&dev,NULL,false);
    float v,soc,a; assert(stc3117_read(&dev,&v,&soc)==ESP_OK && soc==87);
    assert(stc3117_read_current(&dev,&a)==ESP_OK);
    test_now_ms+=8000; assert(stc3117_read(&dev,&v,&soc)!=ESP_OK);
    assert(sensors_mux_begin()); sensors_mux_end(false); int before=io_count;
    stc3117_poll(&dev,NULL,true); assert(io_count==before);
    assert(stc3117_read(&dev,&v,&soc)!=ESP_OK);
    puts("STC: seed/takeover, fault injection, freshness, EOC, MUX ownership, cache expiry passed");
}
