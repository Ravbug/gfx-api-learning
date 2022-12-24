#include "App.hpp"
#if MTL_AVAILABLE
#include "AppleBridge.h"

// these defines are needed to generate the implementation
#define NS_PRIVATE_IMPLEMENTATION
#define CA_PRIVATE_IMPLEMENTATION
#define MTL_PRIVATE_IMPLEMENTATION
#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>
#include <QuartzCore/QuartzCore.hpp>

// this is sketchy
typedef uint32_t CGDirectDisplayID;

#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3.h>
#define GLFW_NATIVE_INCLUDE_NONE
#include <GLFW/glfw3native.h>

#include <iostream>

using namespace std;

NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
MTL::Device* device = nullptr;
MTL::CommandQueue* commandQueue = nullptr;
MTL::CommandBuffer* commandBuffer = nullptr;
MTL::RenderPassDescriptor* rpd = nullptr;
MTL::CommandEncoder* encoder = nullptr;

CA::MetalLayer* renderLayer;

void MTLApp::inithook(){
	device = MTL::CreateSystemDefaultDevice();
	cout << device->name()->cString(NS::StringEncoding::UTF8StringEncoding) << endl;

	renderLayer = static_cast<CA::MetalLayer*>(createMetalLayerInWindow(glfwGetCocoaWindow(window),device));
	
	commandQueue = device->newCommandQueue();
	commandBuffer = commandQueue->commandBuffer();
	rpd = MTL::RenderPassDescriptor::alloc()->init();
	
	auto firstAttachment = rpd->colorAttachments()->object(0);
	firstAttachment->setLoadAction(MTL::LoadAction::LoadActionLoad);
	firstAttachment->setStoreAction(MTL::StoreAction::StoreActionStore);
	firstAttachment->setClearColor(MTL::ClearColor::Make(0.4f, 0.6f, 0.9f, 1.0f));
	rpd->setRenderTargetWidth(WIDTH);
	rpd->setRenderTargetHeight(HEIGHT);
	rpd->setDefaultRasterSampleCount(1);
	
	encoder = commandBuffer->renderCommandEncoder(rpd);
	encoder->endEncoding();

}

void MTLApp::tickhook(){
	// wait and get the next drawable
	auto nextDrawable = static_cast<CA::MetalDrawable*>(CAMetalLayerNextDrawable(renderLayer));
	if (nextDrawable){
		rpd->colorAttachments()->object(0)->setTexture(nextDrawable->texture());
		commandBuffer->presentDrawable(nextDrawable);
		commandBuffer->commit();
		commandBuffer->waitUntilCompleted();
	}
	// if nextDrawable is null, skip rendering
}

void MTLApp::cleanuphook(){
	rpd->release();
	commandQueue->release();
	device->release();
	pool->release();
}
#endif
