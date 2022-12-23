#ifdef __APPLE__
#include "AppleBridge.h"

#import <Cocoa/Cocoa.h>
#import <QuartzCore/CAMetalLayer.h>
#import <Metal/Metal.h>

void* createMetalLayerInWindow(void* window, void* device){
	NSWindow* nswindow = (NSWindow*)(window);
	NSView* contentView = [nswindow contentView];
	[contentView setWantsLayer:YES];
	
	auto dev = (id<MTLDevice>)(device);
	
	CAMetalLayer *res = [CAMetalLayer layer];
	[res setDevice:dev];
	[contentView setLayer:res];
	[res setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
	return res;
}

void* CAMetalLayerNextDrawable(void* v_layer){
	CAMetalLayer* layer = (CAMetalLayer*)v_layer;
	return [layer nextDrawable];
}

#endif

