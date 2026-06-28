#pragma once

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

extern "C" {
    // ─── Network fetch API ──────────────────────────────────────────────────
    void xic_fetch_get(const char* url, void (*onSuccess)(const char* data, int len), void (*onError)(const char* err));
    void xic_fetch_post(const char* url, const char* body, void (*onSuccess)(const char* data, int len), void (*onError)(const char* err));

    // ─── WebGPU Bridge API ──────────────────────────────────────────────────
    void wgpu_request_adapter(void (*onAdapterReady)(int adapterId));
    void wgpu_adapter_request_device(int adapterId, void (*onDeviceReady)(int deviceId));
    int wgpu_device_create_shader_module(int deviceId, const char* wgslCode);
    int wgpu_device_create_pipeline(int deviceId, int shaderModuleId, const char* entryPoint);
    void wgpu_device_run_compute(int deviceId, int pipelineId, int bufferId, int workgroupCountX);
    int wgpu_configure_canvas(int deviceId, const char* canvasId);
    void wgpu_clear_canvas(int deviceId, int contextId, double r, double g, double b, double a);
    void js_request_animation_frame(void (*callback)());
}
