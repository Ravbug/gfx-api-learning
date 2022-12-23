#pragma once
struct GLFWwindow;
#include <cstdint>

constexpr uint32_t WIDTH = 800;
constexpr uint32_t HEIGHT = 600;

struct AppBase {
	void Run() {
		wm_init();
		inithook();
		mainloop();
		cleanuphook();
		wm_cleanup();
	}

	void wm_init();
	void wm_cleanup();
	void mainloop();

	virtual void inithook() = 0;
	virtual void cleanuphook() = 0;
	virtual void tickhook() = 0;
protected:
	GLFWwindow* window;
};

struct VkApp : public AppBase {
	void inithook() final;
	void tickhook() final;
	void cleanuphook() final;
};

#define VK_AVAILABLE 1