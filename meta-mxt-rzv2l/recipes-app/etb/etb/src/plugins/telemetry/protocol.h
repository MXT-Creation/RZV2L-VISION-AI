#ifndef __PROTOCOL_TELEMETRY_H__
#define __PROTOCOL_TELEMETRY_H__

#include <stdbool.h>
#include <libwebsockets.h>
#include <json-c/json.h>

#include "cpu.h"
#include "proc_time.h"

/* FIXME: abstract this better */

struct msg {
    json_object *response;
    uint8_t *send_buf;
};

int callback_telemetry(struct lws *wsi, enum lws_callback_reasons reason,
                       void *user, void *in, size_t len);

struct per_session_data__telemetry
{
    struct lws_ring *ring;
    uint32_t msglen;
    uint32_t tail;
    uint8_t flow_controlled : 1;
    uint8_t write_consume_pending : 1;
    struct lws *wsi;
    struct lws_sorted_usec_list sul;
};

void send_cpu_usage(struct lws *wsi);
void send_proc_time(struct lws *wsi);

#define LWS_PLUGIN_PROTOCOL_TELEMETRY               \
    {                                               \
        "telemetry",                                \
        callback_telemetry,                         \
        sizeof(struct per_session_data__telemetry), \
        1024,                                       \
        0, NULL, 0}

#endif /* __PROTOCOL_TELEMETRY_H__ */