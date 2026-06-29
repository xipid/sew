
#pragma once
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif


// --- Standard WebGPU Handles (Mapped as WASM-safe integer IDs) ---
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

// --- Basic Enums & Constants ---
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

// --- Callback Type Definitions ---
typedef void (*WGPURequestAdapterCallback)(WGPUAdapter adapter, void* userdata);
typedef void (*WGPURequestDeviceCallback)(WGPUDevice device, void* userdata);
typedef void (*WGPUBufferMapCallback)(uint32_t status, void* userdata);

// --- Standard Descriptor Structures ---
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
    // Binding types can be added here or inferred
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


// --- Render Pass Specifications ---
typedef struct WGPUColor {
    double r;
    double g;
    double b;
    double a;
} WGPUColor;

typedef struct WGPURenderPassColorAttachment {
    WGPUTextureView view;           // 0 internally signals the canvas texture view
    WGPUTextureView resolveTarget;
    uint32_t loadOp;                // 1 = WGPULoadOp_Clear
    uint32_t storeOp;               // 1 = WGPUStoreOp_Store
    WGPUColor clearValue;
} WGPURenderPassColorAttachment;

typedef struct WGPURenderPassDescriptor {
    uint32_t colorAttachmentCount;
    const WGPURenderPassColorAttachment* colorAttachments;
} WGPURenderPassDescriptor;

// Render Pass Methods
WGPURenderPassEncoder wgpuCommandEncoderBeginRenderPass(WGPUCommandEncoder encoder, const WGPURenderPassDescriptor* descriptor);
void wgpuRenderPassEncoderEnd(WGPURenderPassEncoder pass);

void xic_fetch_get(const char* url, void (*onSuccess)(const char* data, int len), void (*onError)(const char* err));
void xic_fetch_post(const char* url, const char* body, void (*onSuccess)(const char* data, int len), void (*onError)(const char* err));

// --- Function Declarations (Imports) ---

// Instance & Adapters
void wgpuRequestAdapter(const char* canvasId, WGPURequestAdapterCallback callback, void* userdata);
void wgpuAdapterRequestDevice(WGPUAdapter adapter, WGPURequestDeviceCallback callback, void* userdata);

// Device & Queue
WGPUQueue wgpuDeviceGetQueue(WGPUDevice device);
WGPUShaderModule wgpuDeviceCreateShaderModule(WGPUDevice device, const WGPUShaderModuleDescriptor* descriptor);
WGPUBindGroupLayout wgpuDeviceCreateBindGroupLayout(WGPUDevice device, const WGPUBindGroupLayoutDescriptor* descriptor);
WGPUBindGroup wgpuDeviceCreateBindGroup(WGPUDevice device, const WGPUBindGroupDescriptor* descriptor);

// Buffers
WGPUBuffer wgpuDeviceCreateBuffer(WGPUDevice device, const WGPUBufferDescriptor* descriptor);
void wgpuBufferWrite(WGPUQueue queue, WGPUBuffer buffer, uint64_t bufferOffset, const void* data, uint64_t size);
void wgpuBufferDestroy(WGPUBuffer buffer);

// Command Encoding
WGPUCommandEncoder wgpuDeviceCreateCommandEncoder(WGPUDevice device, const char* label);
WGPUComputePassEncoder wgpuCommandEncoderBeginComputePass(WGPUCommandEncoder encoder, const char* label);
void wgpuComputePassSetPipeline(WGPUComputePassEncoder pass, WGPUComputePipeline pipeline);
void wgpuComputePassSetBindGroup(WGPUComputePassEncoder pass, uint32_t groupIndex, WGPUBindGroup bindGroup, uint32_t dynamicOffsetCount, const uint32_t* dynamicOffsets);
void wgpuComputePassDispatchWorkgroups(WGPUComputePassEncoder pass, uint32_t workgroupCountX, uint32_t workgroupCountY, uint32_t workgroupCountZ);
void wgpuComputePassEnd(WGPUComputePassEncoder pass);

WGPUCommandBuffer wgpuCommandEncoderFinish(WGPUCommandEncoder encoder, const char* label);
void wgpuQueueSubmit(WGPUQueue queue, uint32_t commandCount, const WGPUCommandBuffer* commands);

// Animation Frame Request helper
void jsRequestAnimationFrame(void (*callback)());

#ifdef __cplusplus
}
#endif