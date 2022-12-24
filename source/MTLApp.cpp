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
#include <cassert>

#include "shaders/shader_defs.h"

using namespace std;

MTL::Device* device = nullptr;
MTL::CommandQueue* commandQueue = nullptr;
MTL::RenderPassDescriptor* rpd = nullptr;
MTL::Library* library = nullptr;
MTL::RenderPipelineState* pipelineState = nullptr;
MTL::Buffer* vertbuf = nullptr;
MTL::Buffer* uniformBuf = nullptr;

static UniformBuffer uniformData{.time = 0};

CA::MetalLayer* renderLayer;

#define MTL_CHECK(a) {NS::Error* err = nullptr; a; if(err != nullptr){ std::cerr << err->description()->cString(NS::StringEncoding::UTF8StringEncoding) << std::endl; assert(false);}}

void MTLApp::inithook(){
	
	device = MTL::CreateSystemDefaultDevice();
	cout << device->name()->cString(NS::StringEncoding::UTF8StringEncoding) << endl;
	library = device->newDefaultLibrary();

	renderLayer = static_cast<CA::MetalLayer*>(createMetalLayerInWindow(glfwGetCocoaWindow(window),device));
	
	auto pipelineDesc = MTL::RenderPipelineDescriptor::alloc()->init();
	pipelineDesc->setVertexFunction(library->newFunction(NS::String::alloc()->init("vertexShader", NS::StringEncoding::UTF8StringEncoding)));
	pipelineDesc->setFragmentFunction(library->newFunction(NS::String::alloc()->init("fragmentShader", NS::StringEncoding::UTF8StringEncoding)));
	pipelineDesc->colorAttachments()->object(0)->setPixelFormat(MTL::PixelFormat::PixelFormatBGRA8Unorm);
	
	MTL_CHECK(pipelineState = device->newRenderPipelineState(pipelineDesc, &err));
	
	static constexpr Vertex verts[] = {
		{.color = {1,0,0,1}, .pos = {-1,-1}},
		{.color = {0,1,0,1}, .pos = {0,1}},
		{.color = {0,0,1,1}, .pos = {1,-1}}
	};	
	MTL_CHECK(vertbuf = device->newBuffer(&verts, sizeof(verts), MTL::ResourceOptions{}));
	
	MTL_CHECK(uniformBuf = device->newBuffer(&uniformData, sizeof(UniformBuffer), MTL::ResourceOptions{}));
	
	commandQueue = device->newCommandQueue();
	rpd = MTL::RenderPassDescriptor::alloc()->init();
	
	auto firstAttachment = rpd->colorAttachments()->object(0);
	firstAttachment->setLoadAction(MTL::LoadAction::LoadActionClear);
	firstAttachment->setStoreAction(MTL::StoreAction::StoreActionStore);
	firstAttachment->setClearColor(MTL::ClearColor(0.4f, 0.6f, 0.9f, 1.0f));
	rpd->setDefaultRasterSampleCount(1);
}

void MTLApp::tickhook(){
	// wait and get the next drawable
	auto nextDrawable = static_cast<CA::MetalDrawable*>(CAMetalLayerNextDrawable(renderLayer));
	
	// if nextDrawable is null, skip rendering
	if (!nextDrawable){
		return;
	}
	uniformData.time++;
	std::memcpy(uniformBuf->contents(), &uniformData, sizeof(UniformBuffer));
	
	rpd->colorAttachments()->object(0)->setTexture(nextDrawable->texture());
	rpd->setRenderTargetWidth(WIDTH);
	rpd->setRenderTargetHeight(HEIGHT);
	
	auto commandBuffer = commandQueue->commandBuffer();
	auto encoder = commandBuffer->renderCommandEncoder(rpd);
	encoder->setRenderPipelineState(pipelineState);
	encoder->setVertexBuffer(vertbuf, 0, 0);
	encoder->setVertexBuffer(uniformBuf, 0, 1);
	encoder->drawPrimitives(MTL::PrimitiveType::PrimitiveTypeTriangle, 0, 3, 1);
	
	encoder->endEncoding();
		
	commandBuffer->presentDrawable(nextDrawable);
	commandBuffer->commit();
	commandBuffer->waitUntilCompleted();
	encoder->release();
	commandBuffer->release();
}

void MTLApp::cleanuphook(){
	rpd->release();
	vertbuf->release();
	pipelineState->release();
	library->release();
	commandQueue->release();
	device->release();
}
#endif
