#include "OpenCVCam.h"

bool OpenCVCam::start() {
    if (opened) return true;
    cap.open(camId);
    if (!cap.isOpened()) {
        opened = false;
        return false;
    }
    // default resolution
    cap.set(cv::CAP_PROP_FRAME_WIDTH, width);
    cap.set(cv::CAP_PROP_FRAME_HEIGHT, height);
    opened = true;
    return true;
}

void OpenCVCam::stop() {
    if (cap.isOpened()) cap.release();
    opened = false;
}

bool OpenCVCam::grabFrame(cv::Mat& out) {
    if (!opened) return false;
    cv::Mat f;
    if (!cap.read(f) || f.empty()) return false;
    out = f;
    return true;
}

void OpenCVCam::setResolution(int w, int h) {
    width = w; height = h;
    if (cap.isOpened()) {
        cap.set(cv::CAP_PROP_FRAME_WIDTH, width);
        cap.set(cv::CAP_PROP_FRAME_HEIGHT, height);
    }
}

void OpenCVCam::setCameraId(int id) {
    if (opened) 
        stop();
    camId = id;
}

//int OpenCVCam::getAllCameraIds(std::vector<int>& out) {
//	out.clear();
//	for (int i = 0; i < 10; ++i) { // try first 10 camera ids
//		cv::VideoCapture tempCap(i);
//		if (tempCap.isOpened()) {
//			out.push_back(i);
//			tempCap.release();
//		}
//	}
//	return static_cast<int>(out.size());
//}