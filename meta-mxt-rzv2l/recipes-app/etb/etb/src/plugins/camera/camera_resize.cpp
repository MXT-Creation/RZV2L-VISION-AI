#include <opencv2/opencv.hpp>
#include "camera_resize.hpp"

void rgb_to_yuv422_uyvy(const cv::Mat& rgb, cv::Mat& yuv) {
    assert(rgb.size() == yuv.size() &&
           rgb.depth() == CV_8U &&
           rgb.channels() == 3 &&
           yuv.depth() == CV_8U &&
           yuv.channels() == 2);
    for (int ih = 0; ih < rgb.rows; ih++) {
        const uint8_t* rgbRowPtr = rgb.ptr<uint8_t>(ih);
        uint8_t* yuvRowPtr = yuv.ptr<uint8_t>(ih);

        for (int iw = 0; iw < rgb.cols; iw = iw + 2) {
            const int rgbColIdxBytes = iw * rgb.elemSize();
            const int yuvColIdxBytes = iw * yuv.elemSize();

            const uint8_t R1 = rgbRowPtr[rgbColIdxBytes + 0];
            const uint8_t G1 = rgbRowPtr[rgbColIdxBytes + 1];
            const uint8_t B1 = rgbRowPtr[rgbColIdxBytes + 2];
            const uint8_t R2 = rgbRowPtr[rgbColIdxBytes + 3];
            const uint8_t G2 = rgbRowPtr[rgbColIdxBytes + 4];
            const uint8_t B2 = rgbRowPtr[rgbColIdxBytes + 5];

            const int Y  =  (0.257f * R1) + (0.504f * G1) + (0.098f * B1) + 16.0f ;
            const int U  = -(0.148f * R1) - (0.291f * G1) + (0.439f * B1) + 128.0f;
            const int V  =  (0.439f * R1) - (0.368f * G1) - (0.071f * B1) + 128.0f;
            const int Y2 =  (0.257f * R2) + (0.504f * G2) + (0.098f * B2) + 16.0f ;

            yuvRowPtr[yuvColIdxBytes + 0] = cv::saturate_cast<uint8_t>(Y );
            yuvRowPtr[yuvColIdxBytes + 1] = cv::saturate_cast<uint8_t>(U );
            yuvRowPtr[yuvColIdxBytes + 2] = cv::saturate_cast<uint8_t>(Y2);
            yuvRowPtr[yuvColIdxBytes + 3] = cv::saturate_cast<uint8_t>(V );
        }
    }
}

void resize_frame(const unsigned char *input, unsigned char *output,
	int src_width, int src_height,
	int dst_width, int dst_height)
{
	assert(input != nullptr && output != nullptr);

	cv::Mat orig(src_height, src_width, CV_8UC2, (void*)input);
	if (orig.empty() || orig.data == nullptr) {
		return;
	}

	cv::Mat origRgb;
	cv::cvtColor(orig, origRgb, cv::COLOR_YUV2RGB_YUYV);

	cv::Mat resizedRgb;
	try {
		cv::resize(origRgb, resizedRgb, cv::Size(dst_width, dst_height));
	} catch (const cv::Exception &e) {
		return;
	}

	if (resizedRgb.empty() || resizedRgb.data == nullptr) {
		return;
	}

	cv::Mat resizedYuv(dst_height, dst_width, CV_8UC2);
	rgb_to_yuv422_uyvy(resizedRgb, resizedYuv);

	if (resizedYuv.empty() || resizedYuv.data == nullptr) {
		return;
	}

	memcpy(output, resizedYuv.data, dst_width * dst_height * 2);
}
