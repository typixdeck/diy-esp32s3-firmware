#include "../main/pi_share.c"
static net_snapshot_t mock_net={.connected=true,.time_valid=true};
void net_service_get_snapshot(net_snapshot_t*out) { *out=mock_net; }
static pi_link_snapshot_t mock_serial;
void pi_link_get_snapshot(pi_link_snapshot_t *out) { *out=mock_serial; }
static void pump(const char *line) {
    pi_share_receive(line);
    if(!setjmp(pump_exit))worker(NULL);
}
int main(void) {
    pi_share_init();
    assert(valid_ip("192.0.2.1")&&!valid_ip("127.0.0.1")&&!valid_ip("256.1.1.1"));
    assert(valid_name("readme.txt")&&!valid_name("../x")&&!valid_name("a/b")&&!valid_name("%20"));
    uint8_t *data=NULL;size_t n=0;
    mock_body="hello";assert(fetch("/v1/files/a.txt",&data,10,&n)==0&&n==5&&!strcmp((char*)data,"hello"));free(data);data=NULL;
    int created=mock_created;mock_body="second";
    assert(fetch("/v1/files/b.txt",&data,10,&n)==0&&n==6&&mock_created==created);free(data);data=NULL;
    mock_once_fail=1;int calls=mock_open_calls;
    assert(fetch("/v1/status",&data,100,&n)==0&&mock_open_calls==calls+2);free(data);data=NULL;
    mock_eagain=2;assert(fetch("/v1/files/a.txt",&data,100,&n)==0&&n==6);free(data);data=NULL;
    mock_net.connected=false;assert(fetch("/v1/status",&data,100,&n)==1);mock_net.connected=true;
    mock_net.time_valid=false;assert(fetch("/v1/status",&data,100,&n)==2);mock_net.time_valid=true;
    mock_code=302;assert(fetch("/v1/status",&data,100,&n)==3);mock_code=200;
    mock_size=1000;assert(fetch("/v1/status",&data,100,&n)==3);mock_size=-2;
    mock_chunked=true;assert(fetch("/v1/status",&data,100,&n)==3);mock_chunked=false;
    mock_short=true;assert(fetch("/v1/status",&data,100,&n)==3);mock_short=false;
    mock_open_fail=true;assert(fetch("/v1/status",&data,100,&n)==3);mock_open_fail=false;
    mock_memory_fail=true;assert(fetch("/v1/status",&data,100,&n)==4);mock_memory_fail=false;
    s_cancel=true;assert(fetch("/v1/screen",&data,100,&n)==6);s_cancel=false;
    assert(mock_closed==mock_cleaned);
    pi_share_snapshot_t list={0};char listing[]="readme.txt 5\nb.bin 262144\n";
    assert(parse_files(listing,&list)&&list.count==2);
    char bad[]="../x 1\n";assert(!parse_files(bad,&list));
    mock_body="TD2 STATUS 42 43000 1200000 192.0.2.1 100 200 32\n";read_status();
    pi_link_snapshot_t status;assert(pi_share_get_status(&status)&&status.transport==2&&status.mem_mib==100);
    mock_serial=(pi_link_snapshot_t){.online=true,.transport=1,.cpu_khz=600000,.can_shutdown=true};
    assert(!status_poll_needed());
    pi_share_get_preferred_status(&status);assert(status.transport==1&&status.cpu_khz==600000&&status.can_shutdown);
    mock_serial.online=false;mock_serial.transport=0;
    assert(status_poll_needed());
    pi_share_get_preferred_status(&status);assert(status.transport==2&&status.mem_mib==100&&!status.can_shutdown);
    mock_now+=8000000;assert(!pi_share_get_status(&status));
    pi_share_get_preferred_status(&status);assert(!status.online&&status.transport==0);
    mock_body="TD2 STATUS 42 43000 1200000 999.0.2.1 100 200 32\n";read_status();assert(!pi_share_get_status(&status));
    pi_share_receive("TDPAIR BEGIN 192.0.2.1 aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    pi_share_receive("TDPAIR CERT 1 Q0VSVA==");pump("TDPAIR COMMIT");assert(!saved_size);
    pi_share_receive("TDPAIR BEGIN 192.0.2.1 aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    pi_share_receive("TDPAIR CERT 0 Q0VSVA==");pump("TDPAIR COMMIT");assert(saved_size==sizeof(config_t));
    pi_share_snapshot_t snap;pi_share_get_snapshot(&snap);assert(snap.configured);
    mock_nvs_fail=true;
    pi_share_receive("TDPAIR BEGIN 192.0.2.2 bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    pi_share_receive("TDPAIR CERT 0 Q0VSVA==");pump("TDPAIR COMMIT");mock_nvs_fail=false;
    load_config();assert(!strcmp(s_config.ip,"192.0.2.1"));
    assert(!pi_share_request(SHARE_FILE,"../x"));
    assert(pi_share_request(SHARE_LIST,NULL));assert(!pi_share_request(SHARE_LIST,NULL));
    close_client();free(s_queue);s_queue=NULL;
    puts("pi_share: bounded HTTPS, redirect/clock/offline errors, truncated/oversized data, cancellation, freshness, pairing offset and NVS failure passed");
}
