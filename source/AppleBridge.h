#pragma once
#ifdef __APPLE__

void* createMetalLayerInWindow(void* window, void* device);

void* CAMetalLayerNextDrawable(void* layer);

#endif
