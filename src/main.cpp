#include <iostream>
#include <string>
#include "pipeline/inc/Pipeline.hpp"

int main(int argc, char* argv[])
{
    std::cout << "RK3588 sign-language async pipeline demo\n";

    const std::string camera_device = argc > 1 ? argv[1] : "/dev/video0";
    const int frame_count = argc > 2 ? std::stoi(argv[2]) : 120;

    // 默认读取 120 帧，既能验证摄像头稳定性，也避免测试程序长时间占用设备。
    SignLanguagePipeline pipeline(frame_count, camera_device);
    pipeline.run();

    return 0;
}
