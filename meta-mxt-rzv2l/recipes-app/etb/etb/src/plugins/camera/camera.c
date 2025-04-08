#include "camera.h"

#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <stdbool.h>

#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/videodev2.h>

#include <libwebsockets.h>

#define NUM_MAX_CAMERAS		32
#define NUM_MAX_CAPTURE_BUFS	8
#define DEV_NAME_MAX_SIZE	sizeof("/dev/video999")
#define MAX_CONTROL_QUEUE_SIZE  32

struct control_request {
    uint32_t id;
    int value;
    bool pending;
};

struct camera_entry {
	char dev_name[DEV_NAME_MAX_SIZE];
	struct camera_buffer buffers[NUM_MAX_CAPTURE_BUFS];
	int fd;
	int width;
	int height;
	char needs_resize;
	struct control_request control_queue[MAX_CONTROL_QUEUE_SIZE];
	int queue_head;
	int queue_tail;
};

/* FIXME: add mutex when adding threads */
static struct camera_entry camera_active_list[NUM_MAX_CAMERAS] = {};

static int xioctl(int fd, int request, void* arg)
{
	int r;
	do {
		r = ioctl(fd, request, arg);
	} while (r < 0 && EINTR == errno);
	return r;
}

static int camera_set_capture_parameters(struct camera_entry* cam)
{
	struct v4l2_streamparm setfps = {};
	struct v4l2_format fmt = {};
	
	fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

	if (xioctl(cam->fd, VIDIOC_G_FMT, &fmt) < 0) {
		lwsl_err("ioctl(VIDIOC_G_FMT): %s\n", strerror(errno));
		return -1;
	}

	if (fmt.fmt.pix.pixelformat != V4L2_PIX_FMT_YUYV) {
		lwsl_err("Unsupported pixel format: %d\n", fmt.fmt.pix.pixelformat);
		return -1;
	}

	/* FIXME: hard-coded for now */
	if (fmt.fmt.pix.width != 640 || fmt.fmt.pix.height != 480) {
		lwsl_info("Camera resolution %dx%d will be resized to 640x480\n", 
			fmt.fmt.pix.width, fmt.fmt.pix.height);

		cam->needs_resize = 1;
		cam->width = fmt.fmt.pix.width;
		cam->height = fmt.fmt.pix.height;
	}

	fmt.fmt.pix.field = V4L2_FIELD_NONE;

	if (xioctl(cam->fd, VIDIOC_S_FMT, &fmt) < 0) {
		lwsl_err("ioctl(VIDIOC_S_FMT): %s\n", strerror(errno));
		return -1;
	}

	setfps.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	setfps.parm.capture.timeperframe.numerator = 1;
	setfps.parm.capture.timeperframe.denominator = 30;

	if (xioctl(cam->fd, VIDIOC_S_PARM, &setfps) < 0) {
		lwsl_warn("ioctl(VIDIOC_S_PARM): %s\n", strerror(errno));
		return 0;
	}

	return 0;
}

static int camera_request_buffers(int fd, int num_bufs) {
	struct v4l2_requestbuffers req = {};

	req.count = num_bufs;
	req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	req.memory = V4L2_MEMORY_MMAP;

	if (xioctl(fd, VIDIOC_REQBUFS, &req) < 0) {
		lwsl_err("ioctl(VIDIOC_REQBUFS): %s\n", strerror(errno));
		return -1;
	}
 
	return req.count;
}

static int camera_enqueue_buffer(int fd, int index) {
	struct v4l2_buffer bufd = {};

	bufd.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	bufd.memory = V4L2_MEMORY_MMAP;
	bufd.index = index;
	if (xioctl(fd, VIDIOC_QBUF, &bufd) < 0) {
		lwsl_err("ioctl(VIDIOC_QBUF)[%d]: %s\n", index, strerror(errno));
		return -1;
	}

	return bufd.bytesused;
}

static int camera_dequeue_buffer(int fd) {
	struct v4l2_buffer buf = {};

	buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	buf.memory = V4L2_MEMORY_MMAP;
	buf.index = 0;

	if (xioctl(fd, VIDIOC_DQBUF, &buf) < 0) {
		lwsl_err("ioctl(VIDIOC_QBUF): %s\n", strerror(errno));
		return -1;
	}

	return buf.index;
}

static int camera_query_buffer(int fd, int index, struct camera_buffer *cam_buf) {
	struct v4l2_buffer buf = {};

	buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	buf.memory = V4L2_MEMORY_MMAP;
	buf.index = index;

	if (xioctl(fd, VIDIOC_QUERYBUF, &buf) < 0) {
		lwsl_err("ioctl(VIDIOC_QUERYBUF)[%d]: %s\n", index, strerror(errno));
		return -1;
	}


	cam_buf->ptr = mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, buf.m.offset);
	cam_buf->length = buf.length;
	cam_buf->id = index;

	return buf.length;
}

static int camera_streaming_set_on(int fd, bool on) {
	unsigned int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	const int vidioc = on ? VIDIOC_STREAMON : VIDIOC_STREAMOFF;

	if (xioctl(fd, vidioc, &type) < 0) {
		lwsl_err("ioctl(%s): %s\n",
			 on ? "VIDIOC_STREAMON" : "VIDIOC_STREAMOFF",
			 strerror(errno));
		return -1;
	}

	return 0;
}

static struct camera_entry *camera_find_active(const char *dev, int *first_free_idx)
{
	int i;

	if (!dev)
		return NULL;

	if (first_free_idx)
		*first_free_idx = -1;

	for (i = 0; i < NUM_MAX_CAMERAS; i++) {
		struct camera_entry *e = &camera_active_list[i];
		if (e->dev_name[0] == '\0') {
			if (first_free_idx && *first_free_idx < 0)
				*first_free_idx = i;
			continue;
		}
		if (strncmp(dev, e->dev_name, sizeof(e->dev_name) - 1) == 0)
			return e;
	}

	return NULL;
}

/* Public functions defined from here on */

int camera_devices_get(json_object *req)
{
	char dev_name[DEV_NAME_MAX_SIZE];
	struct v4l2_capability caps;
	json_object *arr;
	int i, fd, ret;
	const char *err = NULL;

	arr = json_object_new_array();
	if (!arr) {
		err = "error creating JSON array";
		goto err;
	}

	for (i = 0; i < 999; i++) {
		json_object *e;
		snprintf(dev_name, sizeof(dev_name), "/dev/video%d", i);

		fd = open(dev_name, O_RDWR);
		if (fd < 0)
			break;

		memset(&caps, 0, sizeof(caps));
		ret = xioctl(fd, VIDIOC_QUERYCAP, &caps);
		close(fd);
		if (ret < 0)
			continue;

		e = json_object_new_object();
		if (!e)
			continue;

		json_object_object_add(e, "device", json_object_new_string(dev_name));
		json_object_object_add(e, "driver", json_object_new_string((char *)caps.driver));
		json_object_object_add(e, "card", json_object_new_string((char *)caps.card));
		json_object_object_add(e, "version", json_object_new_int(caps.version));
		/* FIXME: maybe we stringify these below? */
		json_object_object_add(e, "capabilities", json_object_new_int(caps.capabilities));;
		json_object_object_add(e, "device_caps", json_object_new_int(caps.device_caps));

		json_object_array_add(arr, e);
	}

	json_object_object_add(req, "value", arr);

	return 0;
err:
	json_object_object_add(req, "error", json_object_new_string(err));
	lwsl_err("%s: %s\n", __func__, err);
	return -1;
}

int camera_dev_play_start(json_object *req)
{
	struct camera_entry *cam = NULL;
	json_object *jval;
	const char *dev;
	int ibuf, cam_id = -1;
	const char *err = NULL;

	jval = json_object_object_get(req, "value");
	dev = json_object_get_string(json_object_object_get(jval, "device"));
	if (!dev) {
		err = "no camera device provided";
		goto err_msg;
	}

	if (camera_find_active(dev, &cam_id)) {
		err = "camera is already playing";
		goto err_msg;
	}

	if (cam_id < 0) {
		err = "cannot support more than 32 cameras";
		goto err_msg;
	}

	cam = &camera_active_list[cam_id];

	cam->dev_name[0] = '\0';
	cam->fd = open(dev, O_RDWR | O_CLOEXEC);
	if (cam->fd < 0) {
		err = "error opening socket to device";
		goto err_cam_inactive;
	}

	if (camera_set_capture_parameters(cam) < 0) {
		err = "error configuring camera parameters";
		goto err_close_fd;
	}

	if (camera_request_buffers(cam->fd, NUM_MAX_CAPTURE_BUFS) < 0) {
		err = "error requesting capture buffers";
		goto err_close_fd;
	}

	for (ibuf = 0; ibuf < NUM_MAX_CAPTURE_BUFS; ibuf++) {
		int sz = camera_query_buffer(cam->fd, ibuf, &cam->buffers[ibuf]);
		if (sz < 0) {
			err = "error querying buffer";
			goto err_free_bufs;
		}
		/* For now, we assume buffers are the same size */

		if (camera_enqueue_buffer(cam->fd, ibuf) < 0) {
			err = "error enqueuing buffer";
			goto err_free_bufs;
		}
	}

	ibuf -= 1; /* in case we need to unwind */
	if (camera_streaming_set_on(cam->fd, true) < 0) {
		err = "failed to enable streaming";
		goto err_free_bufs;
	}

	strncpy(cam->dev_name, dev, sizeof(cam->dev_name) - 1);
	json_object_object_add(req, "value", json_object_new_int(cam_id));

	return cam_id;

err_free_bufs:
	for (; ibuf >= 0; ibuf--) {
		munmap(cam->buffers[ibuf].ptr, cam->buffers[ibuf].length);
	}
err_close_fd:
	close(cam->fd);
err_cam_inactive:
	cam->dev_name[0] = '\0';
err_msg:
	lwsl_err("%s: %s\n", __func__, err);
	json_object_object_add(req, "error", json_object_new_string(err));

	return -1;
}

static void camera_dev_play_stop(struct camera_entry *cam)
{
	int i;

	if (cam->dev_name[0] == '\0')
		return;

	camera_streaming_set_on(cam->fd, false);

	cam->dev_name[0] = '\0';
	close(cam->fd);
	cam->fd = -1;

	for (i = 0; i < NUM_MAX_CAPTURE_BUFS; i++)
		munmap(cam->buffers[i].ptr, cam->buffers[i].length);
}

void camera_dev_play_stop_by_id(int cam_id)
{
	struct camera_entry *cam;

	if (cam_id < 0 || cam_id >= NUM_MAX_CAMERAS)
		return;

	cam = &camera_active_list[cam_id];

	camera_dev_play_stop(cam);
}

void camera_dev_play_stop_req(json_object *req)
{
	struct camera_entry *cam;
	json_object *jval;
	const char *dev;

	jval = json_object_object_get(req, "value");
	dev = json_object_get_string(json_object_object_get(jval, "device"));
	if (!dev)
		return;

	cam = camera_find_active(dev, NULL);
	if (!cam)
		return;

	camera_dev_play_stop(cam);
}

int camera_dev_acquire_capture_buffer(int cam_id, struct camera_buffer *buf)
{
	struct camera_entry *cam;
	int buf_id;

	if (cam_id < 0 || cam_id >= NUM_MAX_CAMERAS) {
		lwsl_err("%s: camera index out of range: %d\n", __func__, cam_id);
		return -1;
	}

	cam = &camera_active_list[cam_id];
	if (cam->dev_name[0] == '\0') {
		lwsl_err("%s: inactive camera for index %d\n", __func__, cam_id);
		return -1;
	}

	buf_id = camera_dequeue_buffer(cam->fd);
	if (buf_id < 0)
		return -1;

	memcpy(buf, &cam->buffers[buf_id], sizeof(*buf));

	if (cam->needs_resize) {
		unsigned char *resized_output = (unsigned char *) malloc(640 * 480 * 2);
		
		if (!resized_output) {
			lwsl_err("Failed to allocate memory for resized buffer\n");
			return -1;
		}

		resize_frame((unsigned char *)buf->ptr, resized_output, cam->width, cam->height, 640, 480);

		memcpy(buf->ptr, resized_output, 640 * 480 * 2);
		buf->length = 640 * 480 * 2;

		free(resized_output);
	}

	return 0;
}

void camera_dev_release_capture_buffer(int cam_id, struct camera_buffer *buf)
{
	struct camera_entry *cam;

	if (cam_id < 0 || cam_id >= NUM_MAX_CAMERAS) {
		lwsl_err("%s: camera index out of range: %d\n", __func__, cam_id);
		return;
	}

	if (!buf) {
		lwsl_err("%s: NULL buffer object\n", __func__);
		return;
	}

	if (buf->id >= NUM_MAX_CAPTURE_BUFS) {
		lwsl_err("%s: buffer index out of range: %u\n", __func__, buf->id);
		return;
	}

	cam = &camera_active_list[cam_id];
	if (cam->dev_name[0] == '\0') {
		lwsl_err("%s: inactive camera for index %d\n", __func__, cam_id);
		return;
	}

	camera_enqueue_buffer(cam->fd, buf->id);
}

static int camera_queue_control(struct camera_entry *cam, uint32_t control_id, int value)
{
    int next_tail = (cam->queue_tail + 1) % MAX_CONTROL_QUEUE_SIZE;
    
    if (next_tail == cam->queue_head) {
        lwsl_err("Control queue is full, dropping request for control %u\n", control_id);
        return -1;
    }
    
    cam->control_queue[cam->queue_tail].id = control_id;
    cam->control_queue[cam->queue_tail].value = value;
    cam->control_queue[cam->queue_tail].pending = true;
    cam->queue_tail = next_tail;
    
    return 0;
}

static int camera_process_control_queue(struct camera_entry *cam, json_object *result)
{
    if (cam->queue_head == cam->queue_tail)
        return 0;
    
    struct control_request *req = &cam->control_queue[cam->queue_head];
    struct v4l2_control control = {};
    int ret;
    
    control.id = req->id;
    control.value = req->value;
    
    // First try setting the control directly
    ret = xioctl(cam->fd, VIDIOC_S_CTRL, &control);
    
    if (ret < 0 && errno == EBUSY) {
        // Need to stop and restart the camera
        lwsl_info("Control %u is busy, stopping camera and retrying\n", control.id);
        
        camera_streaming_set_on(cam->fd, false);
        
        ret = xioctl(cam->fd, VIDIOC_S_CTRL, &control);
        
        if (camera_streaming_set_on(cam->fd, true) < 0) {
            lwsl_err("Failed to restart camera streaming\n");
            cam->queue_head = (cam->queue_head + 1) % MAX_CONTROL_QUEUE_SIZE;
            
            if (result) {
                json_object_object_add(result, "success", json_object_new_boolean(0));
                json_object_object_add(result, "error", json_object_new_string("Failed to restart camera"));
            }
            
            return -1;
        }
        
        for (int i = 0; i < NUM_MAX_CAPTURE_BUFS; i++) {
            camera_enqueue_buffer(cam->fd, i);
        }
    }
    
    cam->queue_head = (cam->queue_head + 1) % MAX_CONTROL_QUEUE_SIZE;
    
    if (result) {
        json_object_object_add(result, "success", json_object_new_boolean(ret >= 0));
        json_object_object_add(result, "control_id", json_object_new_int(control.id));
        json_object_object_add(result, "value", json_object_new_int(control.value));
        
        if (ret < 0) {
            json_object_object_add(result, "error", 
                json_object_new_string(strerror(errno)));
        }
    }
    
    return ret;
}

int camera_dev_get_control(json_object *req)
{
    json_object *jval;
    const char *dev;
    const char *err = NULL;
    int fd = -1;
    json_object *controls_array;
    
    jval = json_object_object_get(req, "value");
    dev = json_object_get_string(json_object_object_get(jval, "device"));
    if (!dev) {
        err = "no camera device provided";
        goto err_msg;
    }
    
    fd = open(dev, O_RDWR | O_NONBLOCK);
    if (fd < 0) {
        err = "error opening device";
        goto err_msg;
    }
    
    controls_array = json_object_new_array();
    if (!controls_array) {
        err = "failed to create JSON array";
        goto err_close;
    }
    
    // Query all controls
    struct v4l2_queryctrl qctrl = {};
    qctrl.id = V4L2_CTRL_FLAG_NEXT_CTRL;
    
    while (xioctl(fd, VIDIOC_QUERYCTRL, &qctrl) == 0) {
        json_object *ctrl_obj = json_object_new_object();
        
        if (ctrl_obj) {
            json_object_object_add(ctrl_obj, "id", json_object_new_int(qctrl.id));
            json_object_object_add(ctrl_obj, "name", json_object_new_string((char *)qctrl.name));
            json_object_object_add(ctrl_obj, "type", json_object_new_int(qctrl.type));
            json_object_object_add(ctrl_obj, "minimum", json_object_new_int(qctrl.minimum));
            json_object_object_add(ctrl_obj, "maximum", json_object_new_int(qctrl.maximum));
            json_object_object_add(ctrl_obj, "step", json_object_new_int(qctrl.step));
            json_object_object_add(ctrl_obj, "default_value", json_object_new_int(qctrl.default_value));
            json_object_object_add(ctrl_obj, "flags", json_object_new_int(qctrl.flags));
            
            if (qctrl.type == V4L2_CTRL_TYPE_MENU) {
                json_object *menu_array = json_object_new_array();
                
                if (menu_array) {
                    struct v4l2_querymenu qmenu = {};
                    qmenu.id = qctrl.id;
                    
                    for (qmenu.index = qctrl.minimum; qmenu.index <= (uint32_t)qctrl.maximum; 
                         qmenu.index += qctrl.step) {
                        if (xioctl(fd, VIDIOC_QUERYMENU, &qmenu) == 0) {
                            json_object *menu_item = json_object_new_object();
                            if (menu_item) {
                                json_object_object_add(menu_item, "index", 
                                    json_object_new_int(qmenu.index));
                                json_object_object_add(menu_item, "name", 
                                    json_object_new_string((char *)qmenu.name));
                                json_object_array_add(menu_array, menu_item);
                            }
                        }
                    }
                    
                    json_object_object_add(ctrl_obj, "menu", menu_array);
                }
            }
            
            // Get current control value
            struct v4l2_control control = {};
            control.id = qctrl.id;
            
            if (xioctl(fd, VIDIOC_G_CTRL, &control) == 0) {
                json_object_object_add(ctrl_obj, "current_value", 
                    json_object_new_int(control.value));
            }
            
            json_object_array_add(controls_array, ctrl_obj);
        }
        
        qctrl.id |= V4L2_CTRL_FLAG_NEXT_CTRL;
    }
    
    json_object_object_add(req, "value", controls_array);
    close(fd);
    return 0;
    
err_close:
    if (fd >= 0)
        close(fd);
err_msg:
    json_object_object_add(req, "error", json_object_new_string(err));
    lwsl_err("%s: %s\n", __func__, err);
    return -1;
}

int camera_dev_set_control(json_object *req)
{
    json_object *jval, *jctrl;
    const char *dev;
    uint32_t control_id;
    int value;
    const char *err = NULL;
    struct camera_entry *cam = NULL;
    
    // Debug log to trace the input
    lwsl_info("camera_dev_set_control: Processing request %s\n", 
              json_object_to_json_string_ext(req, JSON_C_TO_STRING_SPACED));
    
    jval = json_object_object_get(req, "value");
    if (!jval) {
        err = "missing value object";
        goto err_msg;
    }
    
    dev = json_object_get_string(json_object_object_get(jval, "device"));
    if (!dev) {
        err = "no camera device provided";
        goto err_msg;
    }
    
    jctrl = json_object_object_get(jval, "control");
    if (!jctrl) {
        err = "no control data provided";
        goto err_msg;
    }
    
    json_object *id_obj = NULL;
    if (!json_object_object_get_ex(jctrl, "id", &id_obj) || !id_obj) {
        err = "missing control id";
        goto err_msg;
    }
    control_id = json_object_get_int(id_obj);
    
    json_object *value_obj = NULL;
    if (!json_object_object_get_ex(jctrl, "value", &value_obj) || !value_obj) {
        err = "missing control value";
        goto err_msg;
    }
    value = json_object_get_int(value_obj);
    
    // Log the extracted values for debugging
    lwsl_info("Setting control: device=%s, control_id=%u, value=%d\n", 
              dev, control_id, value);
    
    cam = camera_find_active(dev, NULL);
    if (!cam) {
        // Device is not active - open it temporarily
        int fd = open(dev, O_RDWR | O_NONBLOCK);
        if (fd < 0) {
            err = "error opening device";
            goto err_msg;
        }
        
        // Set control directly
        struct v4l2_control control = {};
        control.id = control_id;
        control.value = value;
        
        if (xioctl(fd, VIDIOC_S_CTRL, &control) < 0) {
            err = strerror(errno);
            close(fd);
            goto err_msg;
        }
        
        close(fd);
        
        // Successfully set the control
        json_object *result = json_object_new_object();
        if (!result) {
            err = "failed to create result object";
            goto err_msg;
        }
        
        json_object_object_add(result, "success", json_object_new_boolean(1));
        json_object_object_add(result, "control_id", json_object_new_int(control_id));
        json_object_object_add(result, "value", json_object_new_int(value));
        
        json_object_object_add(req, "value", result);
    } else {
        // Device is active, queue the control change
        if (camera_queue_control(cam, control_id, value) < 0) {
            err = "failed to queue control request";
            goto err_msg;
        }
        
        // Process the queue
        json_object *result = json_object_new_object();
        if (!result) {
            err = "failed to create result object";
            goto err_msg;
        }
        
        if (camera_process_control_queue(cam, result) < 0) {
            // Error details are already in result
        }
        
        json_object_object_add(req, "value", result);
    }
    
    return 0;
    
err_msg:
    lwsl_err("camera_dev_set_control: %s\n", err);
    json_object_object_add(req, "error", json_object_new_string(err));
    return -1;
}

