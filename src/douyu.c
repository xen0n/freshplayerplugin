#include "douyu.h"
#include <stdlib.h>
#include "trace.h"

static bool configured = false;
static const char *redis_host;
static unsigned short redis_port;


void
douyu_init(void)
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

    trace_info("Douyu Redis side channel enabled: %s:%hu\n", redis_host, redis_port);
    configured = true;
}


static
void
process_one_douyu_packet(const char *buf)
{
    DouyuPacket *pkt = (DouyuPacket *)buf;

    if (pkt->hdr.magic == DOUYU_CLIENT_MAGIC) {
        trace_info("~~~ Douyu > %s\n", pkt->buf);
        return;
    }

    trace_info("~~~ Douyu < %s\n", pkt->buf);
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
        process_one_douyu_packet(ptr);

        int packet_len = hdr->next + 4;
        ptr += packet_len;
        len -= packet_len;
    }
}
