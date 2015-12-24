#include "douyu.h"
#include <stdlib.h>
#include <string.h>
#include <hiredis/hiredis.h>
#include <hiredis/async.h>
#include <hiredis/adapters/libevent.h>
#include "trace.h"

static bool configured = false;
static redisAsyncContext *ctx = NULL;
static const char *redis_host;
static unsigned short redis_port;


void
douyu_init(struct event_base *base)
{
    const char *tmp;

    redis_host = getenv("DOUYU_SIDE_CHANNEL_REDIS_HOST");
    if (!redis_host) {
        trace_info("Douyu Redis side channel disabled\n");
        return;
    }

    tmp = getenv("DOUYU_SIDE_CHANNEL_REDIS_PORT");
    if (tmp) {
        redis_port = (unsigned short) atoi(tmp);
        if (!redis_port) {
            redis_port = 6379;
        }
    } else {
        redis_port = 6379;
    }

    trace_info("Douyu Redis side channel configured: %s:%hu\n", redis_host, redis_port);

    ctx = redisAsyncConnect(redis_host, redis_port);
    if (ctx->err) {
        trace_error("Douyu Redis side channel failed to initialize: %s\n", ctx->errstr);
        return;
    }

    redisLibeventAttach(ctx, base);
    trace_info("Douyu Redis side channel initialized!\n");

    configured = true;
}


static
void
process_one_douyu_packet(const char *buf)
{
    DouyuPacket *pkt = (DouyuPacket *)buf;

    int err = redisAsyncCommand(
            ctx,
            NULL,
            NULL,
            "PUBLISH %s %b",
            pkt->hdr.magic == DOUYU_CLIENT_MAGIC ? "douyu-client" : "douyu-server",
            pkt->buf,
            strlen(pkt->buf)
            );
    if (err != REDIS_OK) {
        trace_warning("Douyu: publish to Redis failed!\n");
    }
}


void
maybe_process_douyu_packet(const char *buf, int32_t len, bool client)
{
    if (!configured || len < sizeof(DouyuPacketHeader)) {
        return;
    }

    int32_t expected_magic = client ? DOUYU_CLIENT_MAGIC : DOUYU_SERVER_MAGIC;

    // ;-)
    // actually there maybe more than one packets
    const char *ptr = buf;
    while (len > 0) {
        DouyuPacketHeader *hdr = (DouyuPacketHeader *)ptr;

        if (hdr->next != hdr->next_2 || hdr->magic != expected_magic) {
            // this packet is malformed
            return;
        }

        // douyu packets are just zero-terminated funky-encoded clear text
        // it seems packet_len itself may be incorrect as very large numbers of
        // packets have trailing excess data... meaning it can only be used for
        // framing.
        int32_t packet_len = hdr->next + 4;
        process_one_douyu_packet(ptr);

        ptr += packet_len;
        len -= packet_len;
    }
}
