#include "App.hpp"
#if VK_AVAILABLE
#define GLFW_INCLUDE_VULKAN
#endif
#include <GLFW/glfw3.h>

using namespace std;

void AppBase::wm_init() {
	glfwInit();
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
	window = glfwCreateWindow(WIDTH, HEIGHT, "Vulkan", nullptr, nullptr);
}

void AppBase::wm_cleanup() {
	glfwDestroyWindow(window);
	glfwTerminate();
}

void AppBase::mainloop()
{
	while (!glfwWindowShouldClose(window)) {
		glfwPollEvents();
		tickhook();
	}
}
