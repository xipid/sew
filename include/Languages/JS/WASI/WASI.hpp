#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// Include standard WASI api definitions from your wasi-sdk sysroot
#include <wasi/api.h>

#ifdef __cplusplus
extern "C" {
#endif

// Define standard POSIX socket address length types missing in WASI
typedef uint32_t socklen_t;

// --- Custom Mock System Calls utilized by Host Environment ---
__attribute__((visibility("default"))) int __syscall_unlinkat(int dirfd, const char* path, int flags) {
    return 0;
}

__attribute__((visibility("default"))) int __syscall_bind(int fd, const void* addr, socklen_t addrlen) {
    return 0;
}

__attribute__((visibility("default"))) int __syscall_getsockname(int fd, void* addr, socklen_t* addrlen) {
    return 0;
}

__attribute__((visibility("default"))) int __syscall_recvfrom(int fd, void* buf, size_t len, int flags, void* src_addr, socklen_t* addrlen) {
    return 0;
}

__attribute__((visibility("default"))) int __syscall_sendto(int fd, const void* buf, size_t len, int flags, const void* dest_addr, socklen_t addrlen) {
    return 0;
}

__attribute__((visibility("default"))) int __syscall_socket(int domain, int type, int protocol) {
    return 0;
}


// ===================================================================
// --- Standard WebGPU C API Declarations (Plug-and-play for WASM) ---
// ===================================================================

// Handles (Mapped as WASM-safe integer IDs)
typedef uint32_t WGPUInstance;
typedef uint32_t WGPUAdapter;
typedef uint32_t WGPUDevice;
typedef uint32_t WGPUQueue;
typedef uint32_t WGPUBuffer;
typedef uint32_t WGPUSampler;
typedef uint32_t WGPUTexture;
typedef uint32_t WGPUTextureView;
typedef uint32_t WGPUShaderModule;
typedef uint32_t WGPUBindGroupLayout;
typedef uint32_t WGPUBindGroup;
typedef uint32_t WGPUPipelineLayout;
typedef uint32_t WGPUComputePipeline;
typedef uint32_t WGPURenderPipeline;
typedef uint32_t WGPUCommandEncoder;
typedef uint32_t WGPUCommandBuffer;
typedef uint32_t WGPUComputePassEncoder;
typedef uint32_t WGPURenderPassEncoder;

// Basic Enums & Constants
typedef uint32_t WGPUShaderStageFlags;
#define WGPUShaderStage_None 0x00000000
#define WGPUShaderStage_Vertex 0x00000001
#define WGPUShaderStage_Fragment 0x00000002
#define WGPUShaderStage_Compute 0x00000004

typedef uint32_t WGPUBufferUsageFlags;
#define WGPUBufferUsage_None 0x00000000
#define WGPUBufferUsage_MapRead 0x00000001
#define WGPUBufferUsage_MapWrite 0x00000002
#define WGPUBufferUsage_CopySrc 0x00000004
#define WGPUBufferUsage_CopyDst 0x00000008
#define WGPUBufferUsage_Index 0x00000010
#define WGPUBufferUsage_Vertex 0x00000020
#define WGPUBufferUsage_Uniform 0x00000040
#define WGPUBufferUsage_Storage 0x00000080

// Callback declarations (Compatible with both 2-arg and 4-arg layouts)
typedef void (*WGPURequestAdapterCallback2)(WGPUAdapter adapter, void* userdata);
typedef void (*WGPURequestDeviceCallback2)(WGPUDevice device, void* userdata);

typedef void (*WGPURequestAdapterCallback4)(uint32_t status, WGPUAdapter adapter, const char* message, void* userdata);
typedef void (*WGPURequestDeviceCallback4)(uint32_t status, WGPUDevice device, const char* message, void* userdata);

// Map the default signatures to 2-args so your index.cpp test compiles instantly
typedef WGPURequestAdapterCallback2 WGPURequestAdapterCallback;
typedef WGPURequestDeviceCallback2 WGPURequestDeviceCallback;
typedef void (*WGPUBufferMapCallback)(uint32_t status, void* userdata);

// Descriptor Structures
typedef struct WGPUBufferDescriptor {
    const char* label;
    WGPUBufferUsageFlags usage;
    uint64_t size;
    bool mappedAtCreation;
} WGPUBufferDescriptor;

typedef struct WGPUShaderModuleWGSLDescriptor {
    const char* code;
} WGPUShaderModuleWGSLDescriptor;

typedef struct WGPUShaderModuleDescriptor {
    const WGPUShaderModuleWGSLDescriptor* nextInChain;
    const char* label;
} WGPUShaderModuleDescriptor;

typedef struct WGPUBindGroupLayoutEntry {
    uint32_t binding;
    WGPUShaderStageFlags visibility;
} WGPUBindGroupLayoutEntry;

typedef struct WGPUBindGroupLayoutDescriptor {
    const char* label;
    uint32_t entryCount;
    const WGPUBindGroupLayoutEntry* entries;
} WGPUBindGroupLayoutDescriptor;

typedef struct WGPUBindGroupEntry {
    uint32_t binding;
    WGPUBuffer buffer;
    uint64_t offset;
    uint64_t size;
    WGPUSampler sampler;
    WGPUTextureView textureView;
} WGPUBindGroupEntry;

typedef struct WGPUBindGroupDescriptor {
    const char* label;
    WGPUBindGroupLayout layout;
    uint32_t entryCount;
    const WGPUBindGroupEntry* entries;
} WGPUBindGroupDescriptor;

typedef struct WGPUColor {
    double r;
    double g;
    double b;
    double a;
} WGPUColor;

typedef struct WGPURenderPassColorAttachment {
    WGPUTextureView view;
    WGPUTextureView resolveTarget;
    uint32_t loadOp;
    uint32_t storeOp;
    WGPUColor clearValue;
} WGPURenderPassColorAttachment;

typedef struct WGPURenderPassDescriptor {
    uint32_t colorAttachmentCount;
    const WGPURenderPassColorAttachment* colorAttachments;
} WGPURenderPassDescriptor;

// WebGPU Function Interfaces (Native C interface declarations)
WGPURenderPassEncoder wgpuCommandEncoderBeginRenderPass(WGPUCommandEncoder encoder, const WGPURenderPassDescriptor* descriptor);
void wgpuRenderPassEncoderEnd(WGPURenderPassEncoder pass);

void wgpuRequestAdapter(const char* canvasId, WGPURequestAdapterCallback2 callback, void* userdata);
void wgpuAdapterRequestDevice(WGPUAdapter adapter, WGPURequestDeviceCallback2 callback, void* userdata);

WGPUQueue wgpuDeviceGetQueue(WGPUDevice device);
WGPUShaderModule wgpuDeviceCreateShaderModule(WGPUDevice device, const WGPUShaderModuleDescriptor* descriptor);
WGPUBindGroupLayout wgpuDeviceCreateBindGroupLayout(WGPUDevice device, const WGPUBindGroupLayoutDescriptor* descriptor);
WGPUBindGroup wgpuDeviceCreateBindGroup(WGPUDevice device, const WGPUBindGroupDescriptor* descriptor);

WGPUBuffer wgpuDeviceCreateBuffer(WGPUDevice device, const WGPUBufferDescriptor* descriptor);
void wgpuBufferWrite(WGPUQueue queue, WGPUBuffer buffer, uint64_t bufferOffset, const void* data, uint64_t size);
void wgpuBufferDestroy(WGPUBuffer buffer);

WGPUCommandEncoder wgpuDeviceCreateCommandEncoder(WGPUDevice device, const char* label);
WGPUComputePassEncoder wgpuCommandEncoderBeginComputePass(WGPUCommandEncoder encoder, const char* label);
void wgpuComputePassSetPipeline(WGPUComputePassEncoder pass, WGPUComputePipeline pipeline);
void wgpuComputePassSetBindGroup(WGPUComputePassEncoder pass, uint32_t groupIndex, WGPUBindGroup bindGroup, uint32_t dynamicOffsetCount, const uint32_t* dynamicOffsets);
void wgpuComputePassDispatchWorkgroups(WGPUComputePassEncoder pass, uint32_t x, uint32_t y, uint32_t z);
void wgpuComputePassEnd(WGPUComputePassEncoder pass);

WGPUCommandBuffer wgpuCommandEncoderFinish(WGPUCommandEncoder encoder, const char* label);
void wgpuQueueSubmit(WGPUQueue queue, uint32_t commandCount, const WGPUCommandBuffer* commands);

// Web Assembly Environment Fetch & Loop Helpers
void xic_fetch_get(const char* url, void (*onSuccess)(const char* data, int len), void (*onError)(const char* err));
void xic_fetch_post(const char* url, const char* body, void (*onSuccess)(const char* data, int len), void (*onError)(const char* err));
void jsRequestAnimationFrame(void (*callback)());

// ===================================================================
// --- Standard GLFW 3 C API Declarations (Plug-and-play for WASM) ---
// ===================================================================

typedef struct GLFWwindow GLFWwindow;
typedef struct GLFWmonitor GLFWmonitor;

typedef void (*GLFWwindowposfun)(GLFWwindow*, int, int);
typedef void (*GLFWwindowsizefun)(GLFWwindow*, int, int);
typedef void (*GLFWwindowclosefun)(GLFWwindow*);
typedef void (*GLFWwindowrefreshfun)(GLFWwindow*);
typedef void (*GLFWwindowfocusfun)(GLFWwindow*, int);
typedef void (*GLFWwindowiconifyfun)(GLFWwindow*, int);
typedef void (*GLFWframebuffersizefun)(GLFWwindow*, int, int);
typedef void (*GLFWmousebuttonfun)(GLFWwindow*, int, int, int);
typedef void (*GLFWcursorposfun)(GLFWwindow*, double, double);
typedef void (*GLFWcursorenterfun)(GLFWwindow*, int);
typedef void (*GLFWscrollfun)(GLFWwindow*, double, double);
typedef void (*GLFWkeyfun)(GLFWwindow*, int, int, int, int);
typedef void (*GLFWcharfun)(GLFWwindow*, unsigned int);

#define GLFW_TRUE 1
#define GLFW_FALSE 0
#define GLFW_RELEASE 0
#define GLFW_PRESS 1
#define GLFW_REPEAT 2

#define GLFW_KEY_UNKNOWN -1
#define GLFW_KEY_SPACE 32
#define GLFW_KEY_ESCAPE 256
#define GLFW_KEY_ENTER 257
#define GLFW_KEY_TAB 258
#define GLFW_KEY_BACKSPACE 259
#define GLFW_KEY_INSERT 260
#define GLFW_KEY_DELETE 261
#define GLFW_KEY_RIGHT 262
#define GLFW_KEY_LEFT 263
#define GLFW_KEY_DOWN 264
#define GLFW_KEY_UP 265

#define GLFW_MOUSE_BUTTON_1 0
#define GLFW_MOUSE_BUTTON_2 1
#define GLFW_MOUSE_BUTTON_3 2
#define GLFW_MOUSE_BUTTON_LEFT GLFW_MOUSE_BUTTON_1
#define GLFW_MOUSE_BUTTON_RIGHT GLFW_MOUSE_BUTTON_2
#define GLFW_MOUSE_BUTTON_MIDDLE GLFW_MOUSE_BUTTON_3


// Declared as runtime imports to allow communication with the JS Host environment
int glfwInit(void);
void glfwTerminate(void);
void glfwWindowHint(int hint, int value);
void glfwDefaultWindowHints(void);
GLFWwindow* glfwCreateWindow(int width, int height, const char* title, GLFWmonitor* monitor, GLFWwindow* share);
int glfwWindowShouldClose(GLFWwindow* window);
void glfwSetWindowShouldClose(GLFWwindow* window, int value);
void glfwDestroyWindow(GLFWwindow* window);
void glfwPollEvents(void);
void glfwWaitEvents(void);

GLFWkeyfun glfwSetKeyCallback(GLFWwindow* window, GLFWkeyfun callback);
GLFWcursorposfun glfwSetCursorPosCallback(GLFWwindow* window, GLFWcursorposfun callback);
GLFWmousebuttonfun glfwSetMouseButtonCallback(GLFWwindow* window, GLFWmousebuttonfun callback);
GLFWscrollfun glfwSetScrollCallback(GLFWwindow* window, GLFWscrollfun callback);
GLFWwindowsizefun glfwSetWindowSizeCallback(GLFWwindow* window, GLFWwindowsizefun callback);
GLFWframebuffersizefun glfwSetFramebufferSizeCallback(GLFWwindow* window, GLFWframebuffersizefun callback);

void glfwGetWindowSize(GLFWwindow* window, int* width, int* height);
void glfwGetFramebufferSize(GLFWwindow* window, int* width, int* height);
int glfwGetKey(GLFWwindow* window, int key);
int glfwGetMouseButton(GLFWwindow* window, int button);
void glfwGetCursorPos(GLFWwindow* window, double* xpos, double* ypos);

void glfwMakeContextCurrent(GLFWwindow* window);
GLFWwindow* glfwGetCurrentContext(void);
void glfwSwapBuffers(GLFWwindow* window);

double glfwGetTime(void);
void glfwSetTime(double time);


#ifdef __cplusplus
} // Close extern "C"

// Inline C++ template overloads to safely handle varying callback signatures under extern "C"
template<typename T>
inline void wgpuRequestAdapter(const char* canvasId, T callback, void* userdata) {
    wgpuRequestAdapter(canvasId, reinterpret_cast<WGPURequestAdapterCallback2>(callback), userdata);
}

template<typename T>
inline void wgpuAdapterRequestDevice(WGPUAdapter adapter, T callback, void* userdata) {
    wgpuAdapterRequestDevice(adapter, reinterpret_cast<WGPURequestDeviceCallback2>(callback), userdata);
}
#endif