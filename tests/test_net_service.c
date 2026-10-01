// Host behavioral tests with SDK fakes; these never start a radio or access NVS.
#include "../main/net_service.c"

static void reset_case(void)
{
    s_work=(net_snapshot_t){0};
    s_snapshot=(net_snapshot_t){.state=NET_STARTING,.timezone_offset_minutes=DEFAULT_TIMEZONE_MINUTES};
    strlcpy(s_snapshot.ntp_server,"pool.ntp.org",sizeof(s_snapshot.ntp_server));
    strlcpy(s_work.ntp_server,"pool.ntp.org",sizeof(s_work.ntp_server));
    s_started=false;s_ready=false;s_setup_attempted=false;s_connecting=false;s_scan_active=false;s_sntp_active=false;
    mock_wifi_init_calls=0;mock_wifi_init_fail=false;
    s_retries=0;mock_now=0;mock_associated=false;mock_nvs_fail=false;
    mock_connect_calls=mock_ntp_starts=mock_ntp_stops=mock_nvs_saves=0;
    mock_saved_size=0;mock_enabled=0;
    mock_enabled_present=false;mock_timezone_present=false;mock_ntp_server[0]=0;
    s_reconnect_at=0;s_reconnect_delay=RECONNECT_MIN_US;s_saved_attempt=false;
    memset(&mock_config,0,sizeof(mock_config));
    clear_candidate();
}

int main(void)
{
    assert(!net_service_connect("test-network","test-password")); // not initialized
    assert(net_service_init()==ESP_OK);
    assert(net_service_init()==ESP_ERR_INVALID_STATE);
    assert(!net_service_connect("","test-password"));
    assert(!net_service_connect("test-network","short"));
    assert(!net_service_set_time_config("https://invalid/",0));
    assert(!net_service_set_time_config("pool.ntp.org",841));
    assert(net_service_connect("test-network","test-password"));
    command_t *queued=NULL;
    assert(xQueueReceive(s_commands,&queued,0));
    assert(queued->kind==CMD_CONNECT&&!strcmp(queued->password,"test-password"));
    erase_secret(queued,sizeof(*queued));free(queued);
    for(int i=0;i<4;i++)assert(net_service_scan());
    assert(!net_service_cancel()); // capacity failure reported, never block UI
    while(xQueueReceive(s_commands,&queued,0)){erase_secret(queued,sizeof(*queued));free(queued);}

    reset_case();
    load_preferences();
    assert(s_work.enabled&&s_ready&&mock_wifi_init_calls==1);
    assert(s_work.timezone_offset_minutes==480&&!s_connecting); // no factory credentials

    reset_case();mock_enabled_present=true; // explicit user choice: remain off
    load_preferences();
    assert(s_work.state==NET_OFF&&!s_ready&&!mock_wifi_init_calls);
    handle_command(&(command_t){.kind=CMD_ENABLE,.enabled=true});
    assert(s_work.enabled&&s_ready&&mock_wifi_init_calls==1);
    assert(s_work.init_stage==11&&s_work.init_error==ESP_OK);
    handle_command(&(command_t){.kind=CMD_ENABLE,.enabled=false});
    handle_command(&(command_t){.kind=CMD_ENABLE,.enabled=true});
    assert(mock_wifi_init_calls==1);

    reset_case();mock_enabled=1;mock_enabled_present=true;
    load_preferences();
    assert(s_work.enabled&&mock_wifi_init_calls==1);

    reset_case();mock_wifi_init_fail=true;
    handle_command(&(command_t){.kind=CMD_CONNECT,.ssid="test-network",.password="test-password"});
    assert(s_work.state==NET_ERROR&&s_work.last_error==NET_ERROR_INIT);
    assert(s_work.init_stage==4&&s_work.init_error==ESP_ERR_NO_MEM);
    publish();
    char diagnostic[256];
    size_t count=net_service_format_diagnostics(diagnostic,sizeof(diagnostic));
    assert(count==strlen(diagnostic)&&strstr(diagnostic,"stage=4 sdk_err="));
    assert(!strstr(diagnostic,"test-network")&&!strstr(diagnostic,"test-password"));
    char tiny[8];
    assert(net_service_format_diagnostics(tiny,sizeof(tiny))==sizeof(tiny)-1);
    assert(tiny[sizeof(tiny)-1]=='\0');
    assert(net_service_format_diagnostics(NULL,0)==0);
    assert(!s_candidate_password[0]&&!mock_config.sta.password[0]&&!mock_connect_calls);
    handle_command(&(command_t){.kind=CMD_ENABLE,.enabled=true});
    assert(mock_wifi_init_calls==1); // partial setup never repeated
    handle_command(&(command_t){.kind=CMD_ENABLE,.enabled=false});
    assert(s_work.state==NET_OFF);
    handle_command(&(command_t){.kind=CMD_TIME_CONFIG,.server="pool.ntp.org",.timezone=480});
    assert(s_work.timezone_offset_minutes==480); // optional failure does not block settings

    reset_case();
    connect_network("test-network","test-password");
    assert(s_connecting&&!mock_nvs_saves);
    handle_event(&(event_t){.kind=EVENT_DISCONNECT,.reason=WIFI_REASON_AUTH_FAIL});
    assert(!s_connecting&&s_work.last_error==NET_ERROR_AUTH&&!mock_nvs_saves);
    assert(!s_candidate_password[0]&&!mock_config.sta.password[0]);

    reset_case();
    connect_network("test-network","test-password");
    for(int i=0;i<3;i++)handle_event(&(event_t){.kind=EVENT_DISCONNECT,.reason=1});
    assert(!s_connecting&&mock_connect_calls==3&&!mock_nvs_saves);

    reset_case();
    connect_network("test-network","test-password");
    check_deadlines(CONNECT_TIMEOUT_US);
    assert(!s_connecting&&s_work.last_error==NET_ERROR_TIMEOUT&&!mock_nvs_saves);
    assert(!mock_config.sta.password[0]);

    // Model a normal reboot by discarding runtime state, retaining NVS only.
    reset_case();
    connect_network("test-network","test-password");mock_associated=true;
    handle_event(&(event_t){.kind=EVENT_IP});
    handle_command(&(command_t){.kind=CMD_TIME_CONFIG,.server="time.example.test",.timezone=-330});
    s_started=s_ready=s_setup_attempted=s_connecting=s_scan_active=s_sntp_active=false;
    mock_associated=false;
    load_preferences();
    assert(s_connecting&&s_saved_attempt&&s_work.enabled&&s_work.timezone_offset_minutes==-330);
    assert(!strcmp(s_work.ntp_server,"time.example.test"));
    mock_associated=true;handle_event(&(event_t){.kind=EVENT_IP});
    assert(s_work.connected&&mock_nvs_saves==1); // no Flash rewrite on saved reconnect
    mock_associated=false;handle_event(&(event_t){.kind=EVENT_DISCONNECT,.reason=1});
    assert(!s_work.connected&&s_reconnect_at==RECONNECT_MIN_US);
    int before=mock_connect_calls;
    mock_now=RECONNECT_MIN_US-1;check_deadlines(mock_now);assert(mock_connect_calls==before);
    mock_now++;check_deadlines(mock_now);assert(mock_connect_calls==before+1&&s_saved_attempt);
    mock_associated=true;handle_event(&(event_t){.kind=EVENT_IP});
    assert(s_work.connected&&mock_nvs_saves==1&&s_reconnect_delay==RECONNECT_MIN_US);
    mock_associated=false;handle_event(&(event_t){.kind=EVENT_DISCONNECT,.reason=1});
    handle_command(&(command_t){.kind=CMD_CANCEL});
    before=mock_connect_calls;mock_now+=RECONNECT_MAX_US;check_deadlines(mock_now);
    assert(mock_connect_calls==before&&!s_reconnect_at);
    handle_command(&(command_t){.kind=CMD_ENABLE,.enabled=true});
    handle_event(&(event_t){.kind=EVENT_DISCONNECT,.reason=WIFI_REASON_AUTH_FAIL});
    assert(!s_reconnect_at&&s_work.last_error==NET_ERROR_AUTH); // no password hammering
    handle_command(&(command_t){.kind=CMD_ENABLE,.enabled=false});
    s_started=s_ready=s_setup_attempted=false;s_snapshot.enabled=false;load_preferences();
    assert(!s_work.enabled&&!s_ready&&s_work.timezone_offset_minutes==-330);

    reset_case();mock_enabled_present=true;mock_enabled=9;
    mock_timezone_present=true;mock_timezone=900;
    strcpy(mock_ntp_server,"https://invalid/");load_preferences();
    assert(s_work.enabled&&s_work.timezone_offset_minutes==480&&!strcmp(s_work.ntp_server,"pool.ntp.org"));

    reset_case();
    connect_network("test-network","test-password");
    mock_associated=true;
    handle_event(&(event_t){.kind=EVENT_DISCONNECT,.reason=1}); // delayed old event
    assert(s_connecting);
    handle_event(&(event_t){.kind=EVENT_IP});
    assert(s_work.connected&&s_work.saved_network&&mock_nvs_saves==1);
    assert(!s_candidate_password[0]&&s_work.ntp_busy&&mock_ntp_starts==1);
    char ssid[33]={0},password[65]={0};
    assert(load_network(ssid,password)&&!strcmp(password,"test-password"));
    erase_secret(password,sizeof(password));
    handle_event(&(event_t){.kind=EVENT_SYNC,.epoch=CLOCK_EPOCH_MIN+20});
    assert(s_work.time_valid&&s_work.last_sync==CLOCK_EPOCH_MIN+20&&!s_work.ntp_busy);
    handle_command(&(command_t){.kind=CMD_FORGET});
    assert(!s_work.connected&&!s_work.saved_network&&!mock_saved_size);
    assert(!mock_config.sta.password[0]);

    reset_case();
    connect_network("test-network","test-password");mock_associated=true;mock_nvs_fail=true;
    handle_event(&(event_t){.kind=EVENT_IP});
    assert(s_work.connected&&!s_work.saved_network&&s_work.last_error==NET_ERROR_STORAGE);

    reset_case();
    start_ntp();assert(s_work.last_error==NET_ERROR_OFFLINE&&!mock_ntp_starts);
    s_work.connected=true;start_ntp();check_deadlines(NTP_TIMEOUT_US);
    assert(!s_work.ntp_busy&&s_work.last_error==NET_ERROR_TIMEOUT&&mock_ntp_stops==1);
    handle_event(&(event_t){.kind=EVENT_SYNC,.epoch=CLOCK_EPOCH_MIN+10});
    assert(!s_work.last_sync); // canceled/late sync event is ignored

    reset_case();
    handle_command(&(command_t){.kind=CMD_ENABLE,.enabled=true});
    handle_command(&(command_t){.kind=CMD_SCAN});assert(s_scan_active);
    check_deadlines(SCAN_TIMEOUT_US);assert(!s_scan_active&&s_work.last_error==NET_ERROR_TIMEOUT);
    handle_command(&(command_t){.kind=CMD_SCAN});
    mock_aps[0]=(wifi_ap_record_t){.ssid="one",.rssi=-40,.authmode=1};
    mock_aps[1]=(wifi_ap_record_t){.ssid="one",.rssi=-50,.authmode=1};
    mock_aps[2]=(wifi_ap_record_t){.ssid="two",.rssi=-70,.authmode=0};
    mock_ap_count=3;handle_event(&(event_t){.kind=EVENT_SCAN});
    assert(s_work.ap_count==2&&s_work.aps[0].secured&&!s_work.aps[1].secured);
    handle_command(&(command_t){.kind=CMD_ENABLE,.enabled=false});
    assert(s_work.state==NET_OFF&&!s_work.enabled&&!mock_enabled);
    puts("net_service: validation, queue capacity, credentials, retries, cancellation, NTP and scan tests passed");
    return 0;
}
