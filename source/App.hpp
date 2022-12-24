#pragma once
struct GLFWwindow;
#include <cstdint>

static uint32_t WIDTH = 800;
static uint32_t HEIGHT = 600;

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
	virtual ~AppBase(){}

	virtual void inithook() = 0;
	virtual void cleanuphook() = 0;
	virtual void tickhook() = 0;
	virtual void onresize(int newWidth, int newHeight) {}
	virtual const char* getBackendName() = 0;
	GLFWwindow* window;
};

struct VkApp : public AppBase {
	void inithook() final;
	void tickhook() final;
	void cleanuphook() final;
	const char* getBackendName() final {
		return "Vulkan";
	}
};

struct DxApp : public AppBase {
	void inithook() final;
	void tickhook() final;
	void cleanuphook() final;
	void onresize(int, int) final;
	const char* getBackendName() final {
		return "DirectX 12";
	}
};

struct MTLApp : public AppBase {
	void inithook() final;
	void tickhook() final;
	void cleanuphook() final;
	const char* getBackendName() final {
		return "Metal";
	}
};
