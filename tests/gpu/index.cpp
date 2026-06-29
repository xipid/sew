#include <Languages/JS/WASI/WASI.hpp>
#include <stdio.h>
#include <cmath>

static WGPUDevice g_device = 0;
static WGPUQueue g_queue = 0;
static double g_time = 0.0;

extern "C" {
    void render_frame();
}

void render_frame() {
    g_time += 0.015;
    
    // Animate background clear colors using sine waves
    double r = (1.0 + std::sin(g_time)) * 0.5;
    double g = (1.0 + std::sin(g_time + 2.0)) * 0.5;
    double b = (1.0 + std::sin(g_time + 4.0)) * 0.5;
    
    // 1. Create a fresh command encoder for this frame
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(g_device, "Clear Encoder");
    
    // 2. Set up standard Render Pass with a Color Attachment to clear the canvas
    WGPURenderPassColorAttachment colorAttachment = {};
    colorAttachment.view = 0;           // 0 triggers JS-side getCurrentTexture().createView() automatically
    colorAttachment.resolveTarget = 0;
    colorAttachment.loadOp = 1;         // 1 = WGPULoadOp_Clear
    colorAttachment.storeOp = 1;        // 1 = WGPUStoreOp_Store
    colorAttachment.clearValue = { r, g, b, 1.0 };
    
    WGPURenderPassDescriptor renderPassDesc = {};
    renderPassDesc.colorAttachmentCount = 1;
    renderPassDesc.colorAttachments = &colorAttachment;
    
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &renderPassDesc);
    wgpuRenderPassEncoderEnd(pass);
    
    // 3. Finish and submit Command Buffer to Queue
    WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(encoder, "Frame Command Buffer");
    wgpuQueueSubmit(g_queue, 1, &cmd);
    
    // 4. Request Next Frame
    jsRequestAnimationFrame(render_frame);
}

void on_device_ready(WGPUDevice device, void* userdata) {
    if (!device) {
        fprintf(stderr, "Failed to initialize WebGPU device.\n");
        return;
    }
    g_device = device;
    g_queue = wgpuDeviceGetQueue(device);
    render_frame();
}

void on_adapter_ready(WGPUAdapter adapter, void* userdata) {
    if (adapter == 0) {
        fprintf(stderr, "Error: Failed to obtain WebGPU adapter\n");
        return;
    }
    wgpuAdapterRequestDevice(adapter, on_device_ready, nullptr);
}

int main(int argc, char* argv[]) {
    setvbuf(stdout, NULL, _IONBF, 0);
    const char* canvasId = "gpuCanvas";
    if (argc > 1 && argv[1] != nullptr) {
        canvasId = argv[1];
    }
    wgpuRequestAdapter(canvasId, on_adapter_ready, nullptr);
    return 0;
}