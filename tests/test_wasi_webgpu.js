import { xic_fetch_get, wgpu_request_adapter } from "../include/Languages/JS/WASI/WASI.hpp";

export function test() {
    console.log("Running WASI & WebGPU browser target test...");

    // 1. Test Network fetch
    xic_fetch_get("https://raw.githubusercontent.com/xipid/sew/main/README.md", (data, len) => {
        console.log("Fetch success! Data len:", len);
    }, (err) => {
        console.log("Fetch error:", err);
    });

    // 2. Test WebGPU adapter request
    wgpu_request_adapter((adapterId) => {
        console.log("WebGPU adapter ready! ID:", adapterId);
    });
}
