
#include "cpu.h"

#define MAX_CORES 32
#define BUF_SIZE 256

static cpu_info_t prev_cpu[MAX_CORES + 1];
static cpu_info_t curr_cpu[MAX_CORES + 1];
static int initialized = 0;
static int num_cores = 0;

static int get_num_cores() {
    long cores = sysconf(_SC_NPROCESSORS_ONLN);
    if (cores < 1) {
        // Fallback to a default
        return 4;
    }
    return (int)cores;
}

static int read_cpu_stats(cpu_info_t *cpu_stats) {
    FILE *fp;
    char buffer[BUF_SIZE];
    int i = 0;
    int cores = get_num_cores();

    fp = fopen("/proc/stat", "r");
    if (!fp) {
        return -1;
    }

    while (i <= cores && fgets(buffer, sizeof(buffer), fp)) {
        if (strncmp(buffer, "cpu", 3) == 0) {
            if (buffer[3] == ' ' || (buffer[3] >= '0' && buffer[3] <= '9')) {
                sscanf(buffer + (buffer[3] == ' ' ? 4 : 5), "%llu %llu %llu %llu %llu %llu %llu %llu",
                       &cpu_stats[i].user, &cpu_stats[i].nice, 
                       &cpu_stats[i].system, &cpu_stats[i].idle,
                       &cpu_stats[i].iowait, &cpu_stats[i].irq,
                       &cpu_stats[i].softirq, &cpu_stats[i].steal);
                i++;
            }
        }
    }

    fclose(fp);
    return i;
}

static int calculate_usage(cpu_info_t *prev, cpu_info_t *curr) {
    unsigned long long prev_idle = prev->idle + prev->iowait;
    unsigned long long curr_idle = curr->idle + curr->iowait;
    
    unsigned long long prev_total = prev->user + prev->nice + prev->system + 
                                  prev->idle + prev->iowait + prev->irq + 
                                  prev->softirq + prev->steal;
    
    unsigned long long curr_total = curr->user + curr->nice + curr->system + 
                                  curr->idle + curr->iowait + curr->irq + 
                                  curr->softirq + curr->steal;
    
    unsigned long long total_diff = curr_total - prev_total;
    unsigned long long idle_diff = curr_idle - prev_idle;
    
    return (total_diff > 0) ? (int)(100 * (total_diff - idle_diff) / total_diff) : 0;
}

void get_cpu_usage(json_object *jobj) {
    json_object *jarray = json_object_new_array();
    int i, count;
    
    if (num_cores == 0) {
        num_cores = get_num_cores();
    }
    
    if (!initialized) {
        count = read_cpu_stats(prev_cpu);
        if (count > 0) {
            initialized = 1;
        }
        
        for (i = 1; i <= num_cores; i++) {
            json_object_array_add(jarray, json_object_new_int(0));
        }
    } else {
        count = read_cpu_stats(curr_cpu);
        
        if (count <= 1) {  
            for (i = 0; i < num_cores; i++) {
                json_object_array_add(jarray, json_object_new_int(0));
            }
        } else {
            for (i = 1; i <= num_cores && i < count; i++) {
                int usage = calculate_usage(&prev_cpu[i], &curr_cpu[i]);
                json_object_array_add(jarray, json_object_new_int(usage));
            }
            
            memcpy(prev_cpu, curr_cpu, sizeof(cpu_info_t) * (num_cores + 1));
        }
    }
    
    json_object_object_add(jobj, "type", json_object_new_string("cpu_usage"));
    json_object_object_add(jobj, "data", jarray);
}
