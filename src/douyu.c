#include "douyu.h"
#include <stdlib.h>
#include <string.h>
#include <hiredis/hiredis.h>
#include <hiredis/async.h>
#include <hiredis/adapters/libevent.h>
#include "ppb_core.h"
#include "ppb_instance.h"
#include "ppb_var.h"
#include "trace.h"

static bool configured = false;
static redisAsyncContext *ctx = NULL;
static const char *redis_host;
static unsigned short redis_port;

struct PP_Var callbackFunctionName;


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

    callbackFunctionName = ppb_var_var_from_utf8_z("onDouyuSideChannelMsg");

    configured = true;
}


typedef struct {
    PP_Instance instance;
    const char *buf;
} post_message_args;


static
void
do_post_message(void *userdata)
{
    post_message_args *args = (post_message_args *)userdata;
    PP_Instance instance = args->instance;
    const char *buf = args->buf;

    struct PP_Var var = ppb_var_var_from_utf8_z(buf);
    struct PP_Var exc;

    struct PP_Var window = ppb_instance_get_window_object(instance);
    ppb_var_call(window, callbackFunctionName, 1, &var, &exc);
    ppb_var_release(var);
    ppb_var_release(exc);
    ppb_var_release(window);
    free(userdata);
}


static
void
post_message(PP_Instance instance, const char *buf)
{
    post_message_args *args = (post_message_args *)malloc(sizeof(post_message_args));
    if (!args) {
        trace_warning("%s: malloc failed\n", __func__);
        return;
    }

    args->instance = instance;
    args->buf = buf;

    ppb_core_call_on_browser_thread(instance, do_post_message, args);
}


static
void
process_one_douyu_packet(PP_Instance instance, const char *buf)
{
    DouyuPacket *pkt = (DouyuPacket *)buf;

    int err = redisAsyncCommand(
            ctx,
            NULL,
            NULL,
            "PUBLISH %s %s%b",
            "douyu",
            pkt->hdr.magic == DOUYU_CLIENT_MAGIC ? "> " : "< ",
            pkt->buf,
            strlen(pkt->buf)
            );
    if (err != REDIS_OK) {
        trace_warning("Douyu: publish to Redis failed!\n");
    }

    post_message(instance, pkt->buf);
}


void
maybe_process_douyu_packet(PP_Instance instance, const char *buf, int32_t len, bool client)
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
        process_one_douyu_packet(instance, ptr);

        ptr += packet_len;
        len -= packet_len;
    }
}
