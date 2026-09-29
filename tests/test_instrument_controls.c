#include "../main/instrument.c"
#include "../main/instrument_engine.c"
#include <stdio.h>
int main(void){
 assert(instrument_start((void*)1)==ESP_OK);
 instrument_set_usb_volume(25);instrument_set_usb_mute(true);
 assert(codec_volume==25&&codec_mute);
 instrument_set_active(true);
 assert(instrument_active()&&codec_volume==100&&!codec_mute);
 instrument_set_volume(100);instrument_note_on(69,127);
 int16_t pcm[4800*2];instrument_engine_render(&s_engine,pcm,4800);
 int peak=0;for(unsigned i=0;i<9600;i++)if(pcm[i]>peak)peak=pcm[i];
 assert(peak>=3270&&peak<=3277); /* PCM's own -20 dB headroom, no second codec attenuation. */
 instrument_set_volume(0);instrument_engine_render(&s_engine,pcm,4800);
 for(unsigned i=0;i<9600;i++)assert(pcm[i]==0);
 instrument_set_usb_volume(42);instrument_set_usb_mute(true);
 assert(codec_volume==100&&!codec_mute);
 int writes=codec_writes;instrument_write_usb(pcm,sizeof(pcm));assert(codec_writes==writes);
 instrument_set_active(false);assert(codec_volume==42&&codec_mute);
 instrument_write_usb(pcm,sizeof(pcm));assert(codec_writes==writes+2);
 puts("instrument controls: unity local codec, digital volume/silence, USB isolation and exact restore passed");
}
