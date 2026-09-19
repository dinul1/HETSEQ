#pragma once
#include "Backend.h"
#include "FASTAParser.h"
#include <d3d11.h>
#include <d3dcompiler.h>
#include <malloc.h>
#include <cstring>
#include <chrono>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3dcompiler.lib")

static const char* hlsl_source =
"cbuffer Constants : register(b0) {\n"
"    uint element_count;\n"
"};\n"
"RWByteAddressBuffer InputBuffer : register(u0);\n"
"RWByteAddressBuffer OutputBuffer : register(u1);\n"
"[numthreads(64, 1, 1)]\n"
"void CSMain(uint3 DTid : SV_DispatchThreadID)\n"
"{\n"
"    uint idx = DTid.x;\n"
"    if (idx >= element_count) return;\n"
"    uint packed_bases = InputBuffer.Load(idx * 4);\n"
"    uint hash = 0;\n"
"    for (int i = 0; i < 16; ++i) {\n"
"        uint base = (packed_bases >> (i * 2)) & 0x3;\n"
"        hash = (hash * 31 + base) % 9973;\n"
"    }\n"
"    OutputBuffer.Store(idx * 4, hash);\n"
"}\n";

class D3D11Backend : public ComputeBackend {
private:
	ID3D11Device* d3d_device = nullptr;
	ID3D11DeviceContext* d3d_context = nullptr;
	ID3D11ComputeShader* d3d_compute_shader = nullptr;

public:
	bool initialize(IDXGIAdapter* pAdapter) {
		D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_11_0 };
		D3D_FEATURE_LEVEL featureLevel;
		HRESULT hr = D3D11CreateDevice(pAdapter, D3D_DRIVER_TYPE_UNKNOWN, NULL, 0, featureLevels, 1, D3D11_SDK_VERSION, &d3d_device, &featureLevel, &d3d_context);
		if (SUCCEEDED(hr)) {
			ID3DBlob* pBlob = NULL;
			ID3DBlob* pError = NULL;
			if (SUCCEEDED(D3DCompile(hlsl_source, strlen(hlsl_source), "CSMain", NULL, NULL, "CSMain", "cs_5_0", 0, 0, &pBlob, &pError))) {
				d3d_device->CreateComputeShader(pBlob->GetBufferPointer(), pBlob->GetBufferSize(), NULL, &d3d_compute_shader);
				pBlob->Release();
			}
			if (pError) pError->Release();
			return d3d_compute_shader != nullptr;
		}
		return false;
	}

	RuntimeType get_type() override { return RuntimeType::D3D11; }
	const char* get_name() override { return "D3D11 Compute Shader (HLSL)"; }

	ExecutionMetrics execute(const Workload& work) override {
		ExecutionMetrics m;
		if (!d3d_device || !d3d_compute_shader) return m;

		m.gpu_start_ns = get_ns();
		auto t_start = std::chrono::high_resolution_clock::now();
		size_t num_words;
		auto t_enc_start = std::chrono::high_resolution_clock::now();
		uint32_t* packed = FASTAParser::pack_dna_2bit(work.dna_seq + work.offset, work.size_bytes, num_words);
		auto t_enc_end = std::chrono::high_resolution_clock::now();
		m.t_encode = std::chrono::duration<double, std::milli>(t_enc_end - t_enc_start).count();

		D3D11_BUFFER_DESC buf_desc = {};
		buf_desc.ByteWidth = num_words * 4; buf_desc.Usage = D3D11_USAGE_DEFAULT;
		buf_desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS; buf_desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;

		ID3D11Buffer* input_buf = NULL; ID3D11Buffer* output_buf = NULL;
		d3d_device->CreateBuffer(&buf_desc, NULL, &input_buf);
		d3d_device->CreateBuffer(&buf_desc, NULL, &output_buf);

		auto t_h2d_start = std::chrono::high_resolution_clock::now();
		d3d_context->UpdateSubresource(input_buf, 0, NULL, packed, num_words * 4, 0);
		auto t_h2d_end = std::chrono::high_resolution_clock::now();
		m.t_h2d = std::chrono::duration<double, std::milli>(t_h2d_end - t_h2d_start).count();

		D3D11_BUFFER_DESC cb_desc = {};
		cb_desc.ByteWidth = 16; cb_desc.Usage = D3D11_USAGE_DYNAMIC;
		cb_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER; cb_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		ID3D11Buffer* pConstBuffer = NULL;
		d3d_device->CreateBuffer(&cb_desc, NULL, &pConstBuffer);

		D3D11_MAPPED_SUBRESOURCE mapped_cb;
		if (SUCCEEDED(d3d_context->Map(pConstBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_cb))) {
			*(UINT*)mapped_cb.pData = static_cast<UINT>(num_words);
			d3d_context->Unmap(pConstBuffer, 0);
		}
		d3d_context->CSSetConstantBuffers(0, 1, &pConstBuffer);

		D3D11_UNORDERED_ACCESS_VIEW_DESC uav_desc = {};
		uav_desc.Format = DXGI_FORMAT_R32_TYPELESS; uav_desc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
		uav_desc.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW; uav_desc.Buffer.NumElements = num_words;

		ID3D11UnorderedAccessView* input_uav = NULL; ID3D11UnorderedAccessView* output_uav = NULL;
		d3d_device->CreateUnorderedAccessView(input_buf, &uav_desc, &input_uav);
		d3d_device->CreateUnorderedAccessView(output_buf, &uav_desc, &output_uav);

		ID3D11UnorderedAccessView* uavs[2] = { input_uav, output_uav };
		d3d_context->CSSetUnorderedAccessViews(0, 2, uavs, NULL);
		d3d_context->CSSetShader(d3d_compute_shader, NULL, 0);

		UINT thread_groups = (num_words + 63) / 64;
		m.dispatch_x = thread_groups;

		auto t_comp_start = std::chrono::high_resolution_clock::now();
		d3d_context->Dispatch(thread_groups, 1, 1);
		auto t_comp_end = std::chrono::high_resolution_clock::now();
		m.t_compute = std::chrono::duration<double, std::milli>(t_comp_end - t_comp_start).count();

		D3D11_BUFFER_DESC staging_desc = {};
		staging_desc.ByteWidth = num_words * 4; staging_desc.Usage = D3D11_USAGE_STAGING;
		staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		ID3D11Buffer* staging = NULL;
		d3d_device->CreateBuffer(&staging_desc, NULL, &staging);

		auto t_d2h_start = std::chrono::high_resolution_clock::now();
		d3d_context->CopyResource(staging, output_buf);

		uint32_t* out = (uint32_t*)_aligned_malloc(num_words * 4, 64);
		D3D11_MAPPED_SUBRESOURCE mapped;
		if (SUCCEEDED(d3d_context->Map(staging, 0, D3D11_MAP_READ, 0, &mapped))) {
			memcpy(out, mapped.pData, num_words * 4);
			d3d_context->Unmap(staging, 0);
			m.gpu_confirmed = true;
		}
		auto t_d2h_end = std::chrono::high_resolution_clock::now();
		m.t_d2h = std::chrono::duration<double, std::milli>(t_d2h_end - t_d2h_start).count();

		m.out_bytes = num_words * 4;
		for (size_t i = 0; i < num_words; ++i) m.checksum += out[i];

		auto t_end = std::chrono::high_resolution_clock::now();
		m.t_total = std::chrono::duration<double, std::milli>(t_end - t_start).count();
		m.gpu_end_ns = get_ns();

		staging->Release(); input_uav->Release(); output_uav->Release();
		input_buf->Release(); output_buf->Release(); pConstBuffer->Release();
		_aligned_free(packed);
		m.output_buffer = out;
		return m;
	}

	void shutdown() {
		if (d3d_compute_shader) d3d_compute_shader->Release();
		if (d3d_device) d3d_device->Release();
		if (d3d_context) d3d_context->Release();
	}
};