#include <iostream>
#include "pipeline/inc/Pipeline.hpp"

int main()
{
    std::cout << "RK3588 sign-language async pipeline demo\n";

    // 当前 demo 固定跑 12 帧，验证多线程流水线的启动、传递和退出流程。
    SignLanguagePipeline pipeline(12);
    pipeline.run();

    return 0;
}
