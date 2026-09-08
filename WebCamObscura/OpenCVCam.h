#pragma once
#include <opencv2/opencv.hpp>
#include <iostream>
#include <vector>
#include <atomic>
#include <thread>
#include <mutex>

class OpenCVCam {
public:
    OpenCVCam(int id = 0) : camId(id), opened(false), running(false) {}
    ~OpenCVCam() { stop(); }

    bool start();
    void stop();
    bool isOpened() const { return opened; }

    // non-blocking latest frame read (thread-safe minimal)
    bool grabFrame(cv::Mat& out);

    void setResolution(int w, int h);
    void setCameraId(int id);

    bool isCameraIdExist(int id);

	void startRecording(const std::string& filename);
	void stopRecording();
    // Request stop without blocking so main can coordinate stop of both
    // audio and video as close to simultaneous as possible.
    void RequestStopRecording();
	std::string getVideoStatus() const;

private:
    int camId;
    int width = 640;
    int height = 480;
    cv::VideoCapture cap;
    bool opened;

    double fps;

    std::string videoStatus;
    mutable std::mutex statusMutex;
	std::atomic<bool> isRecording{ false };
	std::thread recordingWorker;

    // worker + synchronization for showing/grabbing frames
    std::atomic<bool> running;
    std::thread worker;
    std::mutex frameMutex;
    cv::Mat latestFrame;
    std::atomic<uint64_t> capturedFrames{ 0 };
};