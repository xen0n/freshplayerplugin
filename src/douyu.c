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

struct PP_Var clientCallbackFunctionName;
struct PP_Var serverCallbackFunctionName;


bool
is_douyu_enabled(void)
{
    const char *s = getenv("DOUYU_SIDE_CHANNEL");
    int v;

    if (!s) {
        return false;
    }

    v = atoi(s);
    return !!v;
}


bool
is_douyu_scraping(void)
{
    const char *s = getenv("DOUYU_IS_SCRAPING");
    int v;

    if (!s) {
        return false;
    }

    v = atoi(s);
    return !!v;
}


void
douyu_init(struct event_base *base)
{
    const char *tmp;

    if (!is_douyu_enabled()) {
        trace_info("Douyu side channel disabled");
        return;
    }

    clientCallbackFunctionName = ppb_var_var_from_utf8_z("onDouyuSideChannelMsgC");
    serverCallbackFunctionName = ppb_var_var_from_utf8_z("onDouyuSideChannelMsgS");
    configured = true;

    // redis side channel
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
}


typedef struct {
    PP_Instance instance;
    const char *buf;
    int32_t len;
    bool client;
} post_message_args;


static
void
do_post_message(void *userdata)
{
    post_message_args *args = (post_message_args *)userdata;
    PP_Instance instance = args->instance;
    const char *buf = args->buf;
    int32_t len = args->len;
    bool client = args->client;

    struct PP_Var fn = client ? clientCallbackFunctionName : serverCallbackFunctionName;
    struct PP_Var var = ppb_var_var_from_utf8(buf, len);
    struct PP_Var exc;

    struct PP_Var window = ppb_instance_get_window_object(instance);
    ppb_var_call(window, fn, 1, &var, &exc);
    ppb_var_release(var);
    ppb_var_release(exc);
    ppb_var_release(window);
    free(userdata);
}


static
void
post_message(PP_Instance instance, const char *buf, int32_t len, bool client)
{
    post_message_args *args = (post_message_args *)malloc(sizeof(post_message_args));
    if (!args) {
        trace_warning("%s: malloc failed\n", __func__);
        return;
    }

    args->instance = instance;
    args->buf = buf;
    args->len = len;
    args->client = client;

    ppb_core_call_on_browser_thread(instance, do_post_message, args);
}


static
void
process_one_douyu_packet(PP_Instance instance, const char *buf, int32_t len)
{
    DouyuPacket *pkt = (DouyuPacket *)buf;
    bool client = pkt->hdr.magic == DOUYU_CLIENT_MAGIC;

    int32_t str_len = strlen(pkt->buf);
    int32_t real_len = len > str_len ? str_len : len;

    if (len < str_len) {
        trace_warning("!!! packet may lack zero terminator! (pktlen=%d strlen=%d)\n", len, str_len);
    }

    if (ctx) {
        int err = redisAsyncCommand(
                ctx,
                NULL,
                NULL,
                "PUBLISH %s %s%b",
                "douyu",
                client ? "> " : "< ",
                pkt->buf,
                real_len
                );
        if (err != REDIS_OK) {
            trace_warning("Douyu: publish to Redis failed!\n");
        }
    }

    post_message(instance, pkt->buf, real_len, client);
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
        process_one_douyu_packet(instance, ptr, packet_len);

        ptr += packet_len;
        len -= packet_len;
    }
}
