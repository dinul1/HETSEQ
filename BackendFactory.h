#pragma once
#include "Types.h"
#include "Backend.h"
#include "CPUBackend.h"
#include "D3D11Backend.h"
#include "OpenCLBackend.h"
#include <dxgi.h>
#include <vector>
#include <string>
#include <cstdio>

#pragma comment(lib, "dxgi.lib")

struct GPUInfo {
	std::string name;
	uint32_t vendor_id;
	SIZE_T vram_bytes;
	int backend_type; // 0=D3D11, 1=OpenCL
	ComputeBackend* backend;
};

class BackendFactory {
public:
	static CPUBackend* create_cpu_backend() {
		return new CPUBackend();
	}

	static std::vector<GPUInfo> enumerate_gpus() {
		std::vector<GPUInfo> gpus;

		IDXGIFactory* pFactory = NULL;
		if (FAILED(CreateDXGIFactory(IID_IDXGIFactory, (void**)&pFactory))) {
			return gpus;
		}

		UINT i = 0;
		IDXGIAdapter* pAdapter = NULL;
		while (pFactory->EnumAdapters(i, &pAdapter) != DXGI_ERROR_NOT_FOUND) {
			DXGI_ADAPTER_DESC desc;
			if (pAdapter->GetDesc(&desc) == S_OK) {
				GPUInfo info;
				char name[256];
				WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, name, sizeof(name), NULL, NULL);
				info.name = name;
				info.vendor_id = desc.VendorId;
				info.vram_bytes = desc.DedicatedVideoMemory;
				info.backend = nullptr;
				info.backend_type = -1;
				gpus.push_back(info);
			}
			pAdapter->Release();
			i++;
		}
		pFactory->Release();

		return gpus;
	}

	static ComputeBackend* create_gpu_backend(int gpu_index) {
		std::vector<GPUInfo> gpus = enumerate_gpus();
		if (gpu_index < 0 || gpu_index >= (int)gpus.size()) return nullptr;

		// Re-enumerate to get the specific adapter
		IDXGIFactory* pFactory = NULL;
		if (FAILED(CreateDXGIFactory(IID_IDXGIFactory, (void**)&pFactory))) return nullptr;

		IDXGIAdapter* pAdapter = NULL;
		pFactory->EnumAdapters(gpu_index, &pAdapter);
		pFactory->Release();

		if (!pAdapter) return nullptr;

		// Try OpenCL first (most vendor-neutral)
		OpenCLBackend* ocl = new OpenCLBackend();
		if (ocl->initialize()) {
			pAdapter->Release();
			return ocl;
		}
		delete ocl;

		// Fall back to D3D11
		D3D11Backend* d3d = new D3D11Backend();
		if (d3d->initialize(pAdapter)) {
			pAdapter->Release();
			return d3d;
		}
		delete d3d;

		pAdapter->Release();
		return nullptr;
	}

	static void print_gpu_list() {
		std::vector<GPUInfo> gpus = enumerate_gpus();
		printf("\n[GPU ENUMERATION] Found %zu GPU(s):\n", gpus.size());
		for (size_t i = 0; i < gpus.size(); i++) {
			const char* vendor = "Unknown";
			if (gpus[i].vendor_id == 0x10DE) vendor = "NVIDIA";
			else if (gpus[i].vendor_id == 0x1002) vendor = "AMD";
			else if (gpus[i].vendor_id == 0x8086) vendor = "Intel";

			printf("  [%zu] %s (%s) - VRAM: %llu MB\n",
				i, gpus[i].name.c_str(), vendor,
				(unsigned long long)gpus[i].vram_bytes / (1024ULL * 1024ULL));
		}
	}
};
