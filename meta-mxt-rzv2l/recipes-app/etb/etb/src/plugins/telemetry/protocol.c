#include "protocol.h"

#define RING_DEPTH 8

static time_t last_update_time = 0;
static time_t last_proc_time_update = 0;
static int timer_scheduled = 0;

static void __destroy_message(void *_msg) {
    struct msg *msg = _msg;
    if (msg->response) {
        json_object_put(msg->response);
        msg->response = NULL;
    }
    if (msg->send_buf) {
        free(msg->send_buf);
        msg->send_buf = NULL;
    }
}

static int handle_outgoing_message(struct lws *wsi, struct per_session_data__telemetry *pss) {
    struct msg *pmsg;
    int m, n, flags;
    const char *s;
    size_t slen;

    pmsg = (struct msg*)lws_ring_get_element(pss->ring, &pss->tail);
    if (!pmsg) {
        lwsl_info(" (nothing in ring)\n");
        return -1;
    }

    s = json_object_to_json_string_length(pmsg->response, 0, &slen);
    if (!s) {
        lwsl_warn(" (invalid json response)\n");
        lws_ring_consume_single_tail(pss->ring, &pss->tail, 1);
        return -1;
    }

    n = slen;
    pmsg->send_buf = malloc(n + LWS_PRE);
    if (!pmsg->send_buf) {
        lwsl_warn(" (could not allocate send buffer)\n");
        lws_ring_consume_single_tail(pss->ring, &pss->tail, 1);
        return -1;
    }

    memcpy(pmsg->send_buf + LWS_PRE, s, n);

    flags = lws_write_ws_flags(LWS_WRITE_TEXT, 1, 1);

    m = lws_write(wsi, pmsg->send_buf + LWS_PRE, n, flags);
    if (m < n) {
        lwsl_err("ERROR %d writing to ws socket\n", m);
        lws_ring_consume_single_tail(pss->ring, &pss->tail, 1);
        return -1;
    }

    lwsl_debug(" wrote %d: flags: 0x%x\n", m, flags);

    lws_ring_consume_single_tail(pss->ring, &pss->tail, 1);

    return 0;
}

void send_cpu_usage(struct lws *wsi) {
    if (!wsi) return;

    time_t current_time = time(NULL);
    if (current_time <= last_update_time) {
        return;
    }

    last_update_time = current_time;
    
    struct per_session_data__telemetry *pss = 
        (struct per_session_data__telemetry *)lws_wsi_user(wsi);
    
    if (!pss || !pss->ring) return;
    
    if (lws_ring_get_count_waiting_elements(pss->ring, &pss->tail) > 2) {
        lwsl_warn("Too many messages in queue, skipping\n");
        return;
    }
    
    struct msg amsg;
    amsg.response = json_object_new_object();
    amsg.send_buf = NULL;
    
    get_cpu_usage(amsg.response);
    
    if (!lws_ring_insert(pss->ring, &amsg, 1)) {
        __destroy_message(&amsg);
        lwsl_warn("dropping!\n");
        return;
    }
    
    json_object_get(amsg.response);
    lws_callback_on_writable(wsi);
}

void send_proc_time(struct lws *wsi) {
    if (!wsi) return;

    time_t current_time = time(NULL);
    if (current_time <= last_proc_time_update) {
        return;
    }

    last_proc_time_update = current_time;
    
    struct per_session_data__telemetry *pss = 
        (struct per_session_data__telemetry *)lws_wsi_user(wsi);
    
    if (!pss || !pss->ring) return;
    
    if (lws_ring_get_count_waiting_elements(pss->ring, &pss->tail) > 2) {
        lwsl_warn("Too many messages in queue, skipping proc_time\n");
        return;
    }
    
    struct msg amsg;
    amsg.response = json_object_new_object();
    amsg.send_buf = NULL;
    
    get_proc_time(amsg.response);
    
    if (!lws_ring_insert(pss->ring, &amsg, 1)) {
        __destroy_message(&amsg);
        lwsl_warn("dropping proc_time data!\n");
        return;
    }
    
    json_object_get(amsg.response);
    lws_callback_on_writable(wsi);
}

static void telemetry_timer_cb(struct lws_sorted_usec_list *sul) {
    struct per_session_data__telemetry *pss = 
        lws_container_of(sul, struct per_session_data__telemetry, sul);
    
    if (!pss || !pss->wsi) {
        timer_scheduled = 0;
        return;
    }
    
    send_cpu_usage(pss->wsi);
    
    if (time(NULL) % 2 == 0) {
        send_proc_time(pss->wsi);
    }
    
    timer_scheduled = 1;
    lws_sul_schedule(lws_get_context(pss->wsi), 0, &pss->sul, telemetry_timer_cb, LWS_USEC_PER_SEC);
}

int callback_telemetry(struct lws *wsi, enum lws_callback_reasons reason,
                       void *user, void *in, size_t len) {
    struct per_session_data__telemetry *pss = (struct per_session_data__telemetry *)user;
    
    switch (reason) {
        case LWS_CALLBACK_PROTOCOL_INIT:
            lwsl_info("Telemetry protocol initialized\n");
            timer_scheduled = 0;
            break;

        case LWS_CALLBACK_ESTABLISHED:
            pss->ring = lws_ring_create(sizeof(struct msg), RING_DEPTH, __destroy_message);
            if (!pss->ring) {
                return -1;
            }
            
            pss->wsi = wsi;
            pss->tail = 0;
            pss->flow_controlled = 0;
            pss->write_consume_pending = 0;
            
            last_update_time = 0;
            
            if (!timer_scheduled) {
                timer_scheduled = 1;
                lws_sul_schedule(lws_get_context(wsi), 0, &pss->sul, telemetry_timer_cb, LWS_USEC_PER_SEC);
            }
            break;

        case LWS_CALLBACK_SERVER_WRITEABLE:
            if (pss->flow_controlled) {
                break;
            }
            
            if (lws_ring_get_count_waiting_elements(pss->ring, &pss->tail) > 0) {
                if (handle_outgoing_message(wsi, pss) < 0) {
                    break;
                }
                
                if (lws_ring_get_count_waiting_elements(pss->ring, &pss->tail) > 0) {
                    lws_callback_on_writable(wsi);
                }
            }
            
            if (pss->flow_controlled &&
                (int)lws_ring_get_count_free_elements(pss->ring) > RING_DEPTH - 2) {
                lws_rx_flow_control(wsi, 1);
                pss->flow_controlled = 0;
            }
            break;

        case LWS_CALLBACK_RECEIVE:
            break;

        case LWS_CALLBACK_CLOSED:
            if (pss->ring) {
                lws_ring_destroy(pss->ring);
                pss->ring = NULL;
            }
            pss->wsi = NULL;
            timer_scheduled = 0;
            break;

        default:
            break;
    }
    return 0;
}
