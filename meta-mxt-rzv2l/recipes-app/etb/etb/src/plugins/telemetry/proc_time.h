#ifndef __PROC_TIME_H__
#define __PROC_TIME_H__

#include <json-c/json.h>
#include <libwebsockets.h>

#define PROC_TIME_FILE "/tmp/vision_ai_proc_time.dat"

typedef struct {
    double inference_time;
    double post_proc_time;
    unsigned long timestamp;
} proc_time_data_t;

void get_proc_time(json_object *jobj);

#endif /* __PROC_TIME_H__ */
