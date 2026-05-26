#include <iostream>
#include <opencv2/opencv.hpp>

int main() {
    std::cout << "OpenCV Version: " << CV_VERSION << std::endl;
    std::cout << "OpenCV Include: " << cv::getBuildInformation().substr(0, 100) << "..." << std::endl;

    cv::Mat testMat = cv::Mat::zeros(300, 400, CV_8UC3);

    // Draw a rectangle (brand purple)
    cv::rectangle(testMat, cv::Point(50, 50), cv::Point(350, 250), cv::Scalar(102, 8, 116), -1);

    // Add text
    cv::putText(testMat, "OpenCV 4.13.0", cv::Point(80, 160),
                cv::FONT_HERSHEY_SIMPLEX, 1.5, cv::Scalar(255, 255, 255), 2);
    cv::putText(testMat, "MinGW Environment Test", cv::Point(70, 200),
                cv::FONT_HERSHEY_SIMPLEX, 0.8, cv::Scalar(200, 200, 200), 1);

    cv::imshow("OpenCV Test", testMat);
    std::cout << "Press any key to close the window..." << std::endl;
    cv::waitKey(3000);
    cv::destroyAllWindows();

    std::cout << "OpenCV environment test PASSED!" << std::endl;
    return 0;
}