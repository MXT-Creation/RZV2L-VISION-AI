
#include "proc_time.h"

void get_proc_time(json_object *jobj) {
    static proc_time_data_t proc_time = {0, 0, 0};
    struct stat file_stat;
    int fd;
    proc_time_data_t new_data;
    
    if (stat(PROC_TIME_FILE, &file_stat) == 0) {
        fd = open(PROC_TIME_FILE, O_RDONLY);
        if (fd >= 0) {
            if (read(fd, &new_data, sizeof(new_data)) == sizeof(new_data)) {
                if (new_data.timestamp > proc_time.timestamp) {
                    proc_time = new_data;
                }
            }
            close(fd);
        }
    }
    
    json_object *jdata = json_object_new_object();
    json_object_object_add(jdata, "inference", json_object_new_double(proc_time.inference_time));
    json_object_object_add(jdata, "post_processing", json_object_new_double(proc_time.post_proc_time));
    
    json_object_object_add(jobj, "type", json_object_new_string("proc_time"));
    json_object_object_add(jobj, "data", jdata);
}
