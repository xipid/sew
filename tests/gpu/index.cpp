#include <Languages/JS/WASI/WASI.hpp>
#include <stdio.h>
#include <cmath>

static int g_deviceId = 0;
static int g_contextId = 0;
static double g_time = 0.0;

extern "C" {
    void render_frame();
}

void render_frame() {
    g_time += 0.01;
    
    // Animate colors using sine waves
    double r = (1.0 + std::sin(g_time)) * 0.5;
    double g = (1.0 + std::sin(g_time + 2.0)) * 0.5;
    double b = (1.0 + std::sin(g_time + 4.0)) * 0.5;
    
    wgpu_clear_canvas(g_deviceId, g_contextId, r, g, b, 1.0);
    
    // Queue next frame via requestAnimationFrame callback
    js_request_animation_frame(render_frame);
}

static const char* g_canvasId = "gpuCanvas";

void on_device_ready(int deviceId) {
    g_deviceId = deviceId;
    g_contextId = wgpu_configure_canvas(deviceId, g_canvasId);
    render_frame();
}

void on_adapter_ready(int adapterId) {
    if (adapterId == 0) {
        return;
    }
    wgpu_adapter_request_device(adapterId, on_device_ready);
}

int main(int argc, char* argv[]) {
    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc > 1 && argv[1] != nullptr) {
        g_canvasId = argv[1];
    }
    wgpu_request_adapter(on_adapter_ready);
    return 0;
}
