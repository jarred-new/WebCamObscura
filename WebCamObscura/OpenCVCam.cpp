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
	isRecording = true;
    if (!opened) {
        videoStatus = "Camera is not opened. Cannot start recording.";
        isRecording = false;
        return;
    }

    int fourcc = 0;
    
	// get the fourcc code from the filename extension if not provided
	if (fourcc == 0) {
		std::string ext = filename.substr(filename.find_last_of(".") + 1);
		if (ext == "avi") fourcc = cv::VideoWriter::fourcc('M', 'J', 'P', 'G');
		else if (ext == "mp4") fourcc = cv::VideoWriter::fourcc('a', 'v', 'c', '1');
		else if (ext == "mov") fourcc = cv::VideoWriter::fourcc('m', 'p', '4', 'v');
		else {
			videoStatus = "Unsupported video format for recording.";
            isRecording = false;
            return;
		}
	}

	cv::VideoWriter writer(
        filename, 
        fourcc, 
        fps, 
        cv::Size(this->width, this->height), 
        true
    );

	if (!writer.isOpened()) {
		videoStatus = "Failed to open video writer for recording.";
        isRecording = false;
        return;
	}

    while (running && isRecording) {
        cv::Mat frame;
        cap >> frame;
        if (!grabFrame(frame)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }
        writer.write(frame);
        videoStatus = "Recording to " + filename + " at " + std::to_string(fps) + " FPS.";
    }

	// Release the writer when done
	if (writer.isOpened() && (isRecording == false)) {
        writer.release();
        videoStatus = "Recording stopped.";
	}
}

void OpenCVCam::stopRecording()
{
	//running = false;
	isRecording = false;
	//videoStatus = "Recording stopped.";
}

std::string OpenCVCam::getVideoStatus() const
{
	if (videoStatus.empty()) {
		return "No recording in progress.";
	}
    return videoStatus;
}
