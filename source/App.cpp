#include "App.hpp"
#include <GLFW/glfw3.h>

using namespace std;

void window_size_callback(GLFWwindow* window, int width, int height) {
	auto app = static_cast<AppBase*>(glfwGetWindowUserPointer(window));
	app->onresize(width, height);
}

void AppBase::wm_init() {
	glfwInit();
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
	window = glfwCreateWindow(WIDTH, HEIGHT, getBackendName(), nullptr, nullptr);
	glfwSetWindowUserPointer(window, this);
	glfwSetWindowSizeCallback(window, window_size_callback);
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
