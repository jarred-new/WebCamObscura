#include "OpenCVCam.h"

bool OpenCVCam::start() {
    if (opened) return true;

    if (!cap.open(camId)) {
        opened = false;
        return false;
    }

    // requested resolution
    cap.set(cv::CAP_PROP_FRAME_WIDTH, width);
    cap.set(cv::CAP_PROP_FRAME_HEIGHT, height);

    opened = true;
    running = true;

    // Launch worker thread that reads frames and shows an OpenCV window
    worker = std::thread([this]() {
        cv::Mat frame;
        const std::string winName = "WebCamObscura - " + std::to_string(camId);
        cv::namedWindow(winName, cv::WINDOW_AUTOSIZE);

        while (running) {
            if (!cap.read(frame)) {
                // small sleep to avoid busy loop if capture temporarily fails
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                continue;
            }
            if (frame.empty()) continue;

            {
                std::lock_guard<std::mutex> lk(frameMutex);
                frame.copyTo(latestFrame);
            }

            // show frame in its own OpenCV window
            cv::imshow(winName, frame);
            // waitKey with short delay to process window events
            int k = cv::waitKey(1);
            if (k == 27) { // ESC closes camera window
                running = false;
                break;
            }
        }

        cv::destroyWindow(winName);
        });

    return true;
}

void OpenCVCam::stop() {
    running = false;
    if (worker.joinable()) worker.join();

    if (cap.isOpened()) cap.release();
    {
        std::lock_guard<std::mutex> lk(frameMutex);
        latestFrame.release();
    }
    opened = false;
}

bool OpenCVCam::grabFrame(cv::Mat& out) {
    if (!opened) return false;
    std::lock_guard<std::mutex> lk(frameMutex);
    if (latestFrame.empty()) return false;
    latestFrame.copyTo(out);
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