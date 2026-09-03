#include "OpenCVCam.h"

#include <algorithm>

bool OpenCVCam::start() {
    if (opened) return true;

    if (!cap.open(camId)) {
        opened = false;
        return false;
    }

    // requested resolution
    cap.set(cv::CAP_PROP_FRAME_WIDTH, width);
    cap.set(cv::CAP_PROP_FRAME_HEIGHT, height);

    // get fps from the camera
	fps = cap.get(cv::CAP_PROP_FPS);
    if (fps <= 0) {
		fps = 30.0; // default to 30 fps if camera doesn't provide it
    }

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
    stopRecording();
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

bool OpenCVCam::isCameraIdExist(int id) {
	cv::VideoCapture testCap;
	bool exists = testCap.open(id);
	if (exists) testCap.release();
	return exists;
}

void OpenCVCam::startRecording(const std::string& filename)
{
    if (!opened) {
        std::lock_guard<std::mutex> lk(statusMutex);
        videoStatus = "Camera is not opened. Cannot start recording.";
        isRecording = false;
        return;
    }

    if (isRecording.load()) return;

    if (recordingWorker.joinable()) recordingWorker.join();

    int fourcc = 0;
    
	// get the fourcc code from the filename extension if not provided
	if (fourcc == 0) {
		std::string ext = filename.substr(filename.find_last_of(".") + 1);
		if (ext == "avi") fourcc = cv::VideoWriter::fourcc('M', 'J', 'P', 'G');
		else if (ext == "mp4") fourcc = cv::VideoWriter::fourcc('a', 'v', 'c', '1');
		else if (ext == "mov") fourcc = cv::VideoWriter::fourcc('m', 'p', '4', 'v');
		else {
            std::lock_guard<std::mutex> lk(statusMutex);
            videoStatus = "Unsupported video format for recording.";
            isRecording = false;
            return;
		}
	}

    isRecording = true;
    {
        std::lock_guard<std::mutex> lk(statusMutex);
        videoStatus = "Recording to " + filename + " at " + std::to_string(fps) + " FPS.";
    }

    recordingWorker = std::thread([this, filename, fourcc]() {
        cv::VideoWriter writer(
            filename,
            fourcc,
            fps,
            cv::Size(width, height),
            true
        );

        if (!writer.isOpened()) {
            std::lock_guard<std::mutex> lk(statusMutex);
            videoStatus = "Failed to open video writer for recording.";
            isRecording = false;
            return;
        }

        const auto frameDelay = std::chrono::milliseconds(
            static_cast<int>(1000.0 / std::max(fps, 1.0))
        );

        while (running && isRecording.load()) {
            cv::Mat frame;
            if (grabFrame(frame) && !frame.empty()) {
                if (frame.size() != cv::Size(width, height)) {
                    cv::resize(frame, frame, cv::Size(width, height));
                }
                writer.write(frame);
            }
            std::this_thread::sleep_for(frameDelay);
        }

        // Attempt to grab and write one final frame to reduce chance
        // of the recorded video freezing on the last frame.
        cv::Mat finalFrame;
        if (grabFrame(finalFrame) && !finalFrame.empty()) {
            if (finalFrame.size() != cv::Size(width, height)) {
                cv::resize(finalFrame, finalFrame, cv::Size(width, height));
            }
            writer.write(finalFrame);
        }

        writer.release();
        if (!isRecording.load()) {
            std::lock_guard<std::mutex> lk(statusMutex);
            videoStatus = "Recording stopped.";
        }
    });
}

void OpenCVCam::stopRecording()
{
	isRecording = false;
    if (recordingWorker.joinable()) recordingWorker.join();
}

std::string OpenCVCam::getVideoStatus() const
{
    std::lock_guard<std::mutex> lk(statusMutex);
	if (videoStatus.empty()) {
		return "No recording in progress.";
	}
    return videoStatus;
}
