#include <opencv2/opencv.hpp>
#include <iostream>

int main() {
    // 1. Open the default system camera (ID 0)
    cv::VideoCapture cap(0);

    // Check if the camera opened successfully
    if (!cap.isOpened()) {
        std::cerr << "Error: Could not open the camera feed." << std::endl;
        return -1;
    }

    // Set custom resolution (Optional)
    cap.set(cv::CAP_PROP_FRAME_WIDTH, 640);
    cap.set(cv::CAP_PROP_FRAME_HEIGHT, 480);

    // Create a named GUI window
    std::string windowName = "C++ OpenCV Camera App";
    cv::namedWindow(windowName, cv::WINDOW_AUTOSIZE);

    std::cout << "Camera application started. Press 'ESC' to exit." << std::endl;

    // 2. Continuous frame processing loop
    cv::Mat frame;
    while (true) {
        // Capture the next image frame from the video pipeline
        cap >> frame;

        // Verify the frame is valid and not empty
        if (frame.empty()) {
            std::cerr << "Error: Blank frame grabbed." << std::endl;
            break;
        }

        // 3. Display the live feed in the window
        cv::imshow(windowName, frame);

        // Wait for 30 milliseconds and look for the 'ESC' key (ASCII 27)
        char key = (char)cv::waitKey(30);
        if (key == 27) {
            std::cout << "Exiting camera application." << std::endl;
            break;
        }
    }

    // 4. Release system assets and clean up windows
    cap.release();
    cv::destroyAllWindows();

    return 0;
}
