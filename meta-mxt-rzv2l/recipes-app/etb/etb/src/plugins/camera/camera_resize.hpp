#ifndef CAMERA_RESIZE_HPP
#define CAMERA_RESIZE_HPP

#ifdef __cplusplus
extern "C" {
#endif

void rgb_to_yuv422_uyvy(const cv::Mat& rgb, cv::Mat& yuv);

void resize_frame(const unsigned char* input, unsigned char* output,
                 int src_width, int src_height,
                 int dst_width, int dst_height);

#ifdef __cplusplus
}
#endif

#endif // CAMERA_RESIZE_HPP
