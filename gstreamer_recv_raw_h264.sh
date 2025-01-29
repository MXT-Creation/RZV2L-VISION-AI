#!/bin/sh

gst-launch-1.0 -e udpsrc port=9001 ! \
	'application/x-rtp' ! queue ! \
	rtph264depay ! h264parse ! avdec_h264 ! \
	queue  ! videoconvert ! queue ! \
	fpsdisplaysink text-overlay=0 video-sink=autovideosink -v
