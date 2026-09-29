#include "../main/audio.c"

int main(void)
{
    assert(audio_start(NULL)==ESP_ERR_INVALID_ARG);
    mock_absent=true;
    assert(audio_start((void *)1)==ESP_ERR_NOT_FOUND&&!mock_step&&!mock_live);
    mock_absent=false;
    // Inject failure at every I2S/codec allocation, setup, open and gain step.
    for (int fail=1;fail<=13;fail++) {
        mock_step=0;mock_fail=fail;mock_allocated=0;
        assert(audio_start((void *)1)!=ESP_OK);
        assert(mock_step==fail&&!mock_live);
        assert(!audio_codec_handle()&&!audio_codec_in_handle()&&!s_ctrl_if);
        assert(!s_tx&&!s_rx&&!s_tx_enabled&&!s_rx_enabled);
        assert(audio_set_dac_lr_swap(true)==-1);
    }
    mock_step=0;mock_fail=0;mock_allocated=0;
    assert(audio_start((void *)1)==ESP_OK&&mock_step==13);
    assert(audio_codec_handle()==audio_codec_in_handle()&&audio_codec_handle());
    assert(s_ctrl_if&&s_tx_enabled&&s_rx_enabled);
    assert(audio_start((void *)1)==ESP_OK&&mock_step==13); // idempotent
    for(int i=0;i<mock_allocated;i++)if(mock_allocations[i])release(mock_allocations[i]);
    assert(!mock_live);
    puts("audio_start: 13 injected failures release resources without abort; absent codec and retry pass");
}
