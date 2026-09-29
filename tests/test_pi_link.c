#include "pi_link.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint64_t now;
static uint32_t seed;
uint64_t pi_link_test_now_ms(void) { return now; }
uint32_t pi_link_test_random(void) { return ++seed; }
static char host[17], device[17];

static void connect(unsigned id)
{
    char line[160], tx[160];
    snprintf(host, sizeof(host), "%016x", id);
    snprintf(line, sizeof(line), "TD1 HELLO %s", host);
    pi_link_receive(line);
    assert(pi_link_take_tx(tx, sizeof(tx)));
    char parsed_host[17];
    assert(sscanf(tx, "TD1 WELCOME %16s %16s", parsed_host, device) == 2);
    assert(!strcmp(host, parsed_host));
}

static void heartbeat(unsigned seq, unsigned cap)
{
    char line[160];
    snprintf(line, sizeof(line), "TD1 HB %s %s %u %u 123 42000 1500000", host, device, seq, cap);
    pi_link_receive(line);
}

int main(void)
{
    pi_link_init();
    pi_link_snapshot_t s;
    pi_link_get_snapshot(&s);
    assert(!s.online && !pi_link_request_shutdown());
    connect(1);
    heartbeat(1, 0);
    pi_link_get_snapshot(&s);
    assert(s.online && !s.can_shutdown && s.cpu_khz == 1500000);
    heartbeat(1, 1); /* replay cannot grant capability */
    assert(!pi_link_request_shutdown());
    heartbeat(2, 1);
    assert(pi_link_request_shutdown());
    assert(!pi_link_request_shutdown());
    char tx[160], old[160], ack[160];
    assert(!pi_link_take_tx(tx, 4)); /* buffer too small does not lose command */
    assert(pi_link_take_tx(tx, sizeof(tx)) && strstr(tx, " 1 SHUTDOWN\n"));
    assert(!pi_link_take_tx(tx, sizeof(tx)));
    snprintf(ack, sizeof(ack), "TD1 ACK %s %s 1 ACCEPTED", host, device);
    strcpy(old, ack);
    pi_link_receive(ack);
    pi_link_get_snapshot(&s);
    assert(!s.shutdown_pending && s.shutdown_result == PI_LINK_SHUTDOWN_ACCEPTED && !s.can_shutdown);
    connect(2);
    heartbeat(1, 1);
    assert(pi_link_request_shutdown());
    pi_link_receive(old); /* previous session ACK cannot complete current command */
    pi_link_get_snapshot(&s);
    assert(s.shutdown_pending);
    now = 16000;
    pi_link_get_snapshot(&s);
    assert(!s.online && !s.shutdown_pending && s.shutdown_result == PI_LINK_SHUTDOWN_TIMEOUT);
    assert(!pi_link_take_tx(tx, sizeof(tx))); /* unsent command expires */
    heartbeat(2, 1);
    assert(!pi_link_request_shutdown()); /* no retry after timeout */
    connect(3);
    heartbeat(1, 1);
    assert(pi_link_request_shutdown());
    connect(4);
    pi_link_get_snapshot(&s);
    assert(!s.online && s.shutdown_result == PI_LINK_SHUTDOWN_DISCONNECTED);
    pi_link_receive("TD1 HB invalid invalid 999 1 1 1 1");
    assert(!pi_link_request_shutdown());
    heartbeat(1, 1);
    char sys[160];
    snprintf(sys,sizeof(sys),"TD1 SYS %s %s 1 192.0.2.10 1024 2048 52",host,device);
    pi_link_receive(sys); pi_link_get_snapshot(&s);
    assert(!strcmp(s.ip,"192.0.2.10") && s.mem_mib==1024 && s.disk_mib==2048 && s.transport==1);
    snprintf(sys,sizeof(sys),"TD1 SYS %s %s 1 999.0.2.10 1 1 1",host,device);
    pi_link_receive(sys); pi_link_get_snapshot(&s); assert(s.mem_mib==1024);
    snprintf(sys,sizeof(sys),"TD1 SYS %s %s 0 192.0.2.12 1 1 1",host,device);
    pi_link_receive(sys); pi_link_get_snapshot(&s); assert(s.mem_mib==1024);
    now += 8000;
    assert(!pi_link_request_shutdown());
    puts("pi_link: capability, replay, sessions, timeout, freshness, bounded queue passed");
}
