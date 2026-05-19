#include <iostream>
#include "Pipeline.hpp"

int main()
{
    std::cout << "RK3588 sign-language async pipeline demo\n";

    SignLanguagePipeline pipeline(12);
    pipeline.run();

    return 0;
}
