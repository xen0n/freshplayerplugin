#ifndef FPP_DOUYU_H
#define FPP_DOUYU_H

#include <stdint.h>
#include <stdbool.h>
#include <event2/event.h>
#include <ppapi/c/pp_instance.h>

#define DOUYU_CLIENT_MAGIC 0x2b1
#define DOUYU_SERVER_MAGIC 0x2b2


typedef struct {
    int32_t next;
    int32_t next_2;
    int32_t magic;
} DouyuPacketHeader;


typedef struct {
    DouyuPacketHeader hdr;
    char buf[];
} DouyuPacket;


bool
is_douyu_enabled(void);


bool
is_douyu_scraping(void);


void
douyu_init(struct event_base *base);


void
maybe_process_douyu_packet(PP_Instance instance, const char *buf, int32_t len, bool client);

#endif // FPP_DOUYU_H
