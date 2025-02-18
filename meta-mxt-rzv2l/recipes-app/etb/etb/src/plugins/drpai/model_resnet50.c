#include "models.h"

#include <errno.h>
#include <math.h>
#include <float.h>
#include <libwebsockets.h>

#define RESNET_NUM_CLASS 1000
#define RESNET_TOP_NUM 5

struct resnet50_model_params {
    char **labels;
    int num_labels;
};

static void resnet50_free_labels(struct resnet50_model_params *p)
{
    int i;

    if (!p || !p->labels)
        return;

    for (i = 0; i < p->num_labels; i++) {
        if (p->labels[i])
            free(p->labels[i]);
    }
    free(p->labels);
    p->labels = NULL;
    p->num_labels = 0;
}

static int resnet50_load_labels(json_object *config, struct resnet50_model_params *p)
{
    json_object *jobj;
    int i;

    jobj = json_object_object_get(config, "labels");
    if (!jobj || !json_object_is_type(jobj, json_type_array))
        return -EINVAL;

    p->num_labels = json_object_array_length(jobj);
    p->labels = malloc(sizeof(char*) * p->num_labels);
    if (!p->labels)
        return -ENOMEM;

    for (i = 0; i < p->num_labels; i++) {
        json_object *label = json_object_array_get_idx(jobj, i);
        const char *str = json_object_get_string(label);
        if (!str) {
            resnet50_free_labels(p);
            return -EINVAL;
        }
        p->labels[i] = strdup(str);
        if (!p->labels[i]) {
            resnet50_free_labels(p);
            return -ENOMEM;
        }
    }

    return 0;
}

static int resnet50_postprocessing(void *model_params, float *data, int width, int height, json_object *result)
{
    struct resnet50_model_params *p = model_params;
    json_object *arr, *obj;
    int rc;

    obj = json_object_new_object();
    arr = json_object_new_array();
    if (!arr || !obj) {
        rc = -errno;
        goto err;
    }

    json_object_object_add(result, "name", json_object_new_string("drpai-classification-result"));
    json_object_object_add(result, "value", arr);

    // Find top N results
    for (int i = 0; i < RESNET_TOP_NUM; i++) {
        float max_prob = 0.0f;
        int max_idx = -1;
        
        for (int j = 0; j < RESNET_NUM_CLASS; j++) {
            if (data[j] > max_prob) {
                max_prob = data[j];
                max_idx = j;
            }
        }
        
        if (max_idx >= 0 && max_idx < p->num_labels) {
            json_object *entry = json_object_new_object();
            if (!entry) {
                rc = -errno;
                goto err;
            }
            
            json_object_object_add(entry, "label", json_object_new_string(p->labels[max_idx]));
            json_object_object_add(entry, "probability", json_object_new_double(max_prob * 100.0));
            json_object_array_add(arr, entry);
            
            data[max_idx] = -1.0f;
        }
    }

    return 0;

err:
    if (obj) json_object_put(obj);
    if (arr) json_object_put(arr);
    return rc;
}

static void *resnet50_init(json_object *config, int *err)
{
    struct resnet50_model_params *p;
    int rc;

    p = calloc(1, sizeof(*p));
    if (!p) {
        *err = -ENOMEM;
        return NULL;
    }

    rc = resnet50_load_labels(config, p);
    if (rc < 0) {
        free(p);
        *err = rc;
        return NULL;
    }

    return p;
}

static void resnet50_cleanup(void *model_params)
{
    struct resnet50_model_params *p = model_params;
    
    if (!p)
        return;

    resnet50_free_labels(p);
    free(p);
}

const struct drpai_model_ops resnet50_model_ops = {
    .init = resnet50_init,
    .cleanup = resnet50_cleanup,
    .postprocessing = resnet50_postprocessing,
};