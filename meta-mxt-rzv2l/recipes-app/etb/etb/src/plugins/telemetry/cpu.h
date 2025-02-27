#ifndef __CPU_H__
#define __CPU_H__

#include <json-c/json.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

typedef struct {
    unsigned long long user;
    unsigned long long nice;
    unsigned long long system;
    unsigned long long idle;
    unsigned long long iowait;
    unsigned long long irq;
    unsigned long long softirq;
    unsigned long long steal;
} cpu_info_t;

void get_cpu_usage(json_object *jobj);

#endif /* __CPU_H__ */
