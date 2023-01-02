#include "App.hpp"
#include <memory>

using namespace std;

int main(int argc, char** argv) {

    std::unique_ptr<AppBase> app;

#if !DX12_AVAILABLE
    app = std::make_unique<DxApp>();
#elif VK_AVAILABLE
    app = std::make_unique<VkApp>();
#elif MTL_AVAILABLE
    app = std::make_unique<MTLApp>();
#endif

    app->Run();
    return 0;
}
