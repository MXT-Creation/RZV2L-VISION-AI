#!/bin/sh

[ -n "$1" ] || {
	echo "Need to provide a UDP destination address"
	exit 1
}

DST=$1
WIDTH=640
HEIGHT=480
TARGET_BITRATE=10485760
PORT=9001

if [ -f /tmp/app-usbcam-http-config ] ; then
	source /tmp/app-usbcam-http-config

	WIDTH=$NATIVE_CAMERA_IMAGE_WIDTH
	HEIGHT=$NATIVE_CAMERA_IMAGE_HEIGHT
fi

gst-launch-1.0 -e v4l2src device=/dev/video0 ! \
	video/x-raw, format=UYVY, width=$WIDTH, height=$HEIGHT ! \
	vspmfilter dmabuf-use=true ! video/x-raw, format=NV12 ! \
	omxh264enc control-rate=2 target-bitrate=$TARGET_BITRATE interval_intraframes=14 periodicty-idr=2 use-dmabuf=true ! \
	video/x-h264, profile=\(string\)high,level=\(string\)4.2 ! \
	h264parse ! rtph264pay ! queue ! udpsink host=$DST port=$PORT

