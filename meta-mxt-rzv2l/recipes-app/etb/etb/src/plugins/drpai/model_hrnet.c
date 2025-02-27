#include "models.h"

#include <errno.h>
#include <math.h>
#include <float.h>

#include <libwebsockets.h>

#define HRNET_NUM_OUTPUT_C 17
#define HRNET_NUM_OUTPUT_W 48
#define HRNET_NUM_OUTPUT_H 64
#define HRNET_CROPPED_IMAGE_WIDTH 640
#define HRNET_CROPPED_IMAGE_HEIGHT 480
#define HRNET_OUTPUT_WIDTH (405 * (HRNET_CROPPED_IMAGE_WIDTH / 960.0f))
#define HRNET_OUTPUT_HEIGHT HRNET_CROPPED_IMAGE_HEIGHT
#define HRNET_OUTPUT_LEFT (276 * (HRNET_CROPPED_IMAGE_WIDTH / 960.0f))
#define HRNET_OUTPUT_TOP 0
#define HRNET_OUTPUT_ADJ_X 2
#define HRNET_OUTPUT_ADJ_Y 0
#define HRNET_TH_KPT 0.1f

struct hrnet_model_params {
    int model_in_w;
    int model_in_h;
};

struct pos {
    int x;
    int y;
    float probability;
};

static int8_t sign(int32_t x)
{
    return x > 0 ? 1 : -1;
}

static int32_t hrnet_offset(int32_t b, int32_t y, int32_t x)
{
    return b * HRNET_NUM_OUTPUT_W * HRNET_NUM_OUTPUT_H + y * HRNET_NUM_OUTPUT_W + x;
}

static int hrnet_postprocessing(void *model_params, float *data, int width, int height, json_object *result)
{
    float center[] = { HRNET_CROPPED_IMAGE_WIDTH / 2 - 1, HRNET_CROPPED_IMAGE_HEIGHT / 2 - 1 };
    float hrnet_preds[HRNET_NUM_OUTPUT_C][3];
    json_object *arr, *obj;
    int b, x, y, rc;

    obj = json_object_new_object();
    arr = json_object_new_array();
    if (!arr || !obj) {
        rc = -errno;
        goto err;
    }

    // Process each keypoint
    for (b = 0; b < HRNET_NUM_OUTPUT_C; b++) {
        float max_val = -1;
        int ind_x = -1, ind_y = -1;
        
        // Find maximum value for this keypoint
        for (y = 0; y < HRNET_NUM_OUTPUT_H; y++) {
            for (x = 0; x < HRNET_NUM_OUTPUT_W; x++) {
                int offs = hrnet_offset(b, y, x);
                if (data[offs] > max_val) {
                    max_val = data[offs];
                    ind_x = x;
                    ind_y = y;
                }
            }
        }

        if (max_val < 0) {
            rc = -1;
            goto err;
        }

        // Store predictions and refine coordinates
        hrnet_preds[b][0] = ind_x;
        hrnet_preds[b][1] = ind_y;
        hrnet_preds[b][2] = max_val;

        // Coordinate refinement
        if (ind_y > 1 && ind_y < HRNET_NUM_OUTPUT_H - 1 && 
            ind_x > 1 && ind_x < HRNET_NUM_OUTPUT_W - 1) {
            int offs = hrnet_offset(b, ind_y, ind_x);
            float diff_x = data[offs + 1] - data[offs - 1];
            float diff_y = data[offs + HRNET_NUM_OUTPUT_W] - data[offs - HRNET_NUM_OUTPUT_W];
            hrnet_preds[b][0] += sign(diff_x) * 0.25;
            hrnet_preds[b][1] += sign(diff_y) * 0.25;
        }

        // Transform to original image coordinates
        float scale[] = { HRNET_CROPPED_IMAGE_WIDTH / 200.0 * 200, 
                         HRNET_CROPPED_IMAGE_HEIGHT / 200.0 * 200 };
        float scale_x = scale[0] / HRNET_NUM_OUTPUT_W;
        float scale_y = scale[1] / HRNET_NUM_OUTPUT_H;
        float coords_x = hrnet_preds[b][0];
        float coords_y = hrnet_preds[b][1];
        
        hrnet_preds[b][0] = coords_x * scale_x + center[0] - scale[0] * 0.5;
        hrnet_preds[b][1] = coords_y * scale_y + center[1] - scale[1] * 0.5;

        // Convert to output resolution
        int posx = (int)(hrnet_preds[b][0] / HRNET_CROPPED_IMAGE_WIDTH * HRNET_OUTPUT_WIDTH + 0.5) + 
                  HRNET_OUTPUT_LEFT + HRNET_OUTPUT_ADJ_X;
        int posy = (int)(hrnet_preds[b][1] / HRNET_CROPPED_IMAGE_HEIGHT * HRNET_OUTPUT_HEIGHT + 0.5) + 
                  HRNET_OUTPUT_TOP + HRNET_OUTPUT_ADJ_Y;

        // Create JSON object for this keypoint
        json_object *jobj = json_object_new_object();
        if (!jobj) {
            rc = -errno;
            goto err;
        }

        json_object_object_add(jobj, "x", json_object_new_int(posx));
        json_object_object_add(jobj, "y", json_object_new_int(posy));
        json_object_object_add(jobj, "probability", json_object_new_double(hrnet_preds[b][2]));
        json_object_array_add(arr, jobj);
    }

    float lowest_score = 1.0f;
    for (b = 0; b < HRNET_NUM_OUTPUT_C; b++) {
        if (hrnet_preds[b][2] < lowest_score) {
            lowest_score = hrnet_preds[b][2];
        }
    }

    json_object_object_add(result, "name", json_object_new_string("drpai-pose-estimation-result"));
    
    if (lowest_score <= HRNET_TH_KPT) {
        // Release existing array and create empty one only if detection fails
        json_object_put(arr);
        if (!(arr = json_object_new_array())) {
            rc = -errno;
            goto err;
        }
    }
    
    json_object_object_add(result, "value", arr);
    return 0;

err:
    if (obj) json_object_put(obj);
    if (arr) json_object_put(arr);
    return rc;
}

static void *hrnet_init(json_object *config, int *err)
{
    struct hrnet_model_params *p = calloc(1, sizeof(*p));
    if (!p) {
        if (err)
            *err = -ENOMEM;
        return NULL;
    }

    p->model_in_w = HRNET_CROPPED_IMAGE_WIDTH;
    p->model_in_h = HRNET_CROPPED_IMAGE_HEIGHT;

    return p;
}

static void hrnet_cleanup(void *model_params)
{
    struct hrnet_model_params *p = model_params;
    free(p);
}

const struct drpai_model_ops hrnet_model_ops = {
    .init = hrnet_init,
    .cleanup = hrnet_cleanup,
    .postprocessing = hrnet_postprocessing,
};