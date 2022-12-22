#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "App.hpp"
#include <memory>

using namespace std;

int main(int argc, char** argv) {
   
    auto app = std::make_unique<VkApp>();
    app->Run();
    return 0;
}