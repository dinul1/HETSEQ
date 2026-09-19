#pragma once
#include "Backend.h"
#include "FASTAParser.h"

#include <windows.h>
#include <malloc.h>
#include <cstdint>
#include <cstring>
#include <chrono>
#include <cstdio>

// Define OpenCL types manually to avoid SDK dependencies
typedef struct _cl_platform_id *cl_platform_id;
typedef struct _cl_device_id *cl_device_id;
typedef struct _cl_context *cl_context;
typedef struct _cl_command_queue *cl_command_queue;
typedef struct _cl_program *cl_program;
typedef struct _cl_kernel *cl_kernel;
typedef struct _cl_mem *cl_mem;
typedef struct _cl_event *cl_event;

typedef int32_t cl_int;
typedef uint32_t cl_uint;
typedef uint64_t cl_ulong; // FIX: Must be 64-bit!
typedef cl_ulong cl_bitfield;
typedef cl_bitfield cl_device_type;
typedef cl_bitfield cl_mem_flags;
typedef cl_bitfield cl_queue_properties;
typedef cl_ulong cl_device_info;
typedef cl_ulong cl_program_build_info;

// OpenCL constants
#define CL_SUCCESS 0
#define CL_DEVICE_TYPE_GPU 4
#define CL_MEM_READ_ONLY (1 << 2)
#define CL_MEM_WRITE_ONLY (1 << 1)
#define CL_MEM_COPY_HOST_PTR (1 << 5)
#define CL_PROGRAM_BUILD_LOG 0x1083
#define CL_TRUE 1

// Callback function pointer types
typedef void(__stdcall *cl_context_callback)(const char*, const void*, size_t, void*);
typedef void(__stdcall *cl_program_callback)(cl_program, void*);

// FIX: Added __stdcall calling convention to match Windows OpenCL API
typedef cl_int(__stdcall *PFN_clGetPlatformIDs)(cl_uint, cl_platform_id*, cl_uint*);
typedef cl_int(__stdcall *PFN_clGetDeviceIDs)(cl_platform_id, cl_device_type, cl_uint, cl_device_id*, cl_uint*);
typedef cl_context(__stdcall *PFN_clCreateContext)(const void*, cl_uint, const cl_device_id*, cl_context_callback, void*, cl_int*);
typedef cl_command_queue(__stdcall *PFN_clCreateCommandQueue)(cl_context, cl_device_id, cl_queue_properties, cl_int*);
typedef cl_program(__stdcall *PFN_clCreateProgramWithSource)(cl_context, cl_uint, const char**, const size_t*, cl_int*);
typedef cl_int(__stdcall *PFN_clBuildProgram)(cl_program, cl_uint, const cl_device_id*, const char*, cl_program_callback, void*);
typedef cl_int(__stdcall *PFN_clGetProgramBuildInfo)(cl_program, cl_device_id, cl_program_build_info, size_t, void*, size_t*);
typedef cl_kernel(__stdcall *PFN_clCreateKernel)(cl_program, const char*, cl_int*);
typedef cl_mem(__stdcall *PFN_clCreateBuffer)(cl_context, cl_mem_flags, size_t, void*, cl_int*);
typedef cl_int(__stdcall *PFN_clSetKernelArg)(cl_kernel, cl_uint, size_t, const void*);
typedef cl_int(__stdcall *PFN_clEnqueueNDRangeKernel)(cl_command_queue, cl_kernel, cl_uint, const size_t*, const size_t*, const size_t*, cl_uint, const cl_event*, cl_event*);
typedef cl_int(__stdcall *PFN_clEnqueueReadBuffer)(cl_command_queue, cl_mem, cl_int, size_t, size_t, void*, cl_uint, const cl_event*, cl_event*);
typedef cl_int(__stdcall *PFN_clFinish)(cl_command_queue);
typedef void(__stdcall *PFN_clReleaseMemObject)(cl_mem);
typedef void(__stdcall *PFN_clReleaseKernel)(cl_kernel);
typedef void(__stdcall *PFN_clReleaseProgram)(cl_program);
typedef void(__stdcall *PFN_clReleaseCommandQueue)(cl_command_queue);
typedef void(__stdcall *PFN_clReleaseContext)(cl_context);

class OpenCLBackend : public ComputeBackend {
private:
	HMODULE ocl_lib = nullptr;
	cl_platform_id platform = nullptr;
	cl_device_id device = nullptr;
	cl_context context = nullptr;
	cl_command_queue queue = nullptr;
	cl_program program = nullptr;
	cl_kernel kernel = nullptr;

	PFN_clGetPlatformIDs clGetPlatformIDs = nullptr;
	PFN_clGetDeviceIDs clGetDeviceIDs = nullptr;
	PFN_clCreateContext clCreateContext = nullptr;
	PFN_clCreateCommandQueue clCreateCommandQueue = nullptr;
	PFN_clCreateProgramWithSource clCreateProgramWithSource = nullptr;
	PFN_clBuildProgram clBuildProgram = nullptr;
	PFN_clGetProgramBuildInfo clGetProgramBuildInfo = nullptr;
	PFN_clCreateKernel clCreateKernel = nullptr;
	PFN_clCreateBuffer clCreateBuffer = nullptr;
	PFN_clSetKernelArg clSetKernelArg = nullptr;
	PFN_clEnqueueNDRangeKernel clEnqueueNDRangeKernel = nullptr;
	PFN_clEnqueueReadBuffer clEnqueueReadBuffer = nullptr;
	PFN_clFinish clFinish = nullptr;
	PFN_clReleaseMemObject clReleaseMemObject = nullptr;
	PFN_clReleaseKernel clReleaseKernel = nullptr;
	PFN_clReleaseProgram clReleaseProgram = nullptr;
	PFN_clReleaseCommandQueue clReleaseCommandQueue = nullptr;
	PFN_clReleaseContext clReleaseContext = nullptr;

	const char* ocl_source =
		"__kernel void kmer_hash(__global const uint* input, __global uint* output, const uint element_count) {\n"
		"    int idx = get_global_id(0);\n"
		"    if (idx >= element_count) return;\n"
		"    uint packed_bases = input[idx];\n"
		"    uint hash = 0;\n"
		"    for (int i = 0; i < 16; ++i) {\n"
		"        uint base = (packed_bases >> (i * 2)) & 0x3;\n"
		"        hash = (hash * 31 + base) % 9973;\n"
		"    }\n"
		"    output[idx] = hash;\n"
		"}\n";

public:
	bool initialize() {
		ocl_lib = LoadLibraryA("OpenCL.dll");
		if (!ocl_lib) return false;

		clGetPlatformIDs = (PFN_clGetPlatformIDs)GetProcAddress(ocl_lib, "clGetPlatformIDs");
		clGetDeviceIDs = (PFN_clGetDeviceIDs)GetProcAddress(ocl_lib, "clGetDeviceIDs");
		clCreateContext = (PFN_clCreateContext)GetProcAddress(ocl_lib, "clCreateContext");
		clCreateCommandQueue = (PFN_clCreateCommandQueue)GetProcAddress(ocl_lib, "clCreateCommandQueue");
		clCreateProgramWithSource = (PFN_clCreateProgramWithSource)GetProcAddress(ocl_lib, "clCreateProgramWithSource");
		clBuildProgram = (PFN_clBuildProgram)GetProcAddress(ocl_lib, "clBuildProgram");
		clGetProgramBuildInfo = (PFN_clGetProgramBuildInfo)GetProcAddress(ocl_lib, "clGetProgramBuildInfo");
		clCreateKernel = (PFN_clCreateKernel)GetProcAddress(ocl_lib, "clCreateKernel");
		clCreateBuffer = (PFN_clCreateBuffer)GetProcAddress(ocl_lib, "clCreateBuffer");
		clSetKernelArg = (PFN_clSetKernelArg)GetProcAddress(ocl_lib, "clSetKernelArg");
		clEnqueueNDRangeKernel = (PFN_clEnqueueNDRangeKernel)GetProcAddress(ocl_lib, "clEnqueueNDRangeKernel");
		clEnqueueReadBuffer = (PFN_clEnqueueReadBuffer)GetProcAddress(ocl_lib, "clEnqueueReadBuffer");
		clFinish = (PFN_clFinish)GetProcAddress(ocl_lib, "clFinish");
		clReleaseMemObject = (PFN_clReleaseMemObject)GetProcAddress(ocl_lib, "clReleaseMemObject");
		clReleaseKernel = (PFN_clReleaseKernel)GetProcAddress(ocl_lib, "clReleaseKernel");
		clReleaseProgram = (PFN_clReleaseProgram)GetProcAddress(ocl_lib, "clReleaseProgram");
		clReleaseCommandQueue = (PFN_clReleaseCommandQueue)GetProcAddress(ocl_lib, "clReleaseCommandQueue");
		clReleaseContext = (PFN_clReleaseContext)GetProcAddress(ocl_lib, "clReleaseContext");

		if (!clGetPlatformIDs || !clGetDeviceIDs || !clCreateContext || !clCreateCommandQueue ||
			!clCreateProgramWithSource || !clBuildProgram || !clGetProgramBuildInfo || !clCreateKernel ||
			!clCreateBuffer || !clSetKernelArg || !clEnqueueNDRangeKernel || !clEnqueueReadBuffer ||
			!clFinish || !clReleaseMemObject || !clReleaseKernel || !clReleaseProgram ||
			!clReleaseCommandQueue || !clReleaseContext) {
			return false;
		}

		cl_int err;
		err = clGetPlatformIDs(1, &platform, nullptr);
		if (err != CL_SUCCESS) return false;

		err = clGetDeviceIDs(platform, CL_DEVICE_TYPE_GPU, 1, &device, nullptr);
		if (err != CL_SUCCESS) return false;

		context = clCreateContext(nullptr, 1, &device, nullptr, nullptr, &err);
		if (err != CL_SUCCESS) return false;

		queue = clCreateCommandQueue(context, device, 0, &err);
		if (err != CL_SUCCESS) return false;

		cl_int err2;
		program = clCreateProgramWithSource(context, 1, &ocl_source, nullptr, &err2);
		if (err2 != CL_SUCCESS) return false;

		err2 = clBuildProgram(program, 1, &device, nullptr, nullptr, nullptr);
		if (err2 != CL_SUCCESS) {
			size_t len;
			char buffer[2048];
			clGetProgramBuildInfo(program, device, CL_PROGRAM_BUILD_LOG, sizeof(buffer), buffer, &len);
			printf("OpenCL Build Error:\n%s\n", buffer);
			return false;
		}

		kernel = clCreateKernel(program, "kmer_hash", &err2);
		return (err2 == CL_SUCCESS);
	}

	RuntimeType get_type() override { return RuntimeType::OPENCL; }
	const char* get_name() override { return "OpenCL Kernel"; }

	ExecutionMetrics execute(const Workload& work) override {
		ExecutionMetrics m;
		if (!kernel) return m;

		m.gpu_start_ns = get_ns();
		auto t_start = std::chrono::high_resolution_clock::now();
		size_t num_words;
		auto t_enc_start = std::chrono::high_resolution_clock::now();
		uint32_t* packed = FASTAParser::pack_dna_2bit(work.dna_seq + work.offset, work.size_bytes, num_words);
		auto t_enc_end = std::chrono::high_resolution_clock::now();
		m.t_encode = std::chrono::duration<double, std::milli>(t_enc_end - t_enc_start).count();

		cl_int err;
		cl_mem input_buf = clCreateBuffer(context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, num_words * 4, packed, &err);
		cl_mem output_buf = clCreateBuffer(context, CL_MEM_WRITE_ONLY, num_words * 4, nullptr, &err);

		uint32_t* out = (uint32_t*)_aligned_malloc(num_words * 4, 64);

		auto t_h2d_start = std::chrono::high_resolution_clock::now();
		auto t_h2d_end = std::chrono::high_resolution_clock::now();
		m.t_h2d = std::chrono::duration<double, std::milli>(t_h2d_end - t_h2d_start).count();

		clSetKernelArg(kernel, 0, sizeof(cl_mem), &input_buf);
		clSetKernelArg(kernel, 1, sizeof(cl_mem), &output_buf);
		clSetKernelArg(kernel, 2, sizeof(cl_uint), &num_words);

		size_t global_work_size = (num_words + 63) / 64 * 64;
		size_t local_work_size = 64;

		auto t_comp_start = std::chrono::high_resolution_clock::now();
		clEnqueueNDRangeKernel(queue, kernel, 1, nullptr, &global_work_size, &local_work_size, 0, nullptr, nullptr);
		clFinish(queue);
		auto t_comp_end = std::chrono::high_resolution_clock::now();
		m.t_compute = std::chrono::duration<double, std::milli>(t_comp_end - t_comp_start).count();

		auto t_d2h_start = std::chrono::high_resolution_clock::now();
		clEnqueueReadBuffer(queue, output_buf, CL_TRUE, 0, num_words * 4, out, 0, nullptr, nullptr);
		auto t_d2h_end = std::chrono::high_resolution_clock::now();
		m.t_d2h = std::chrono::duration<double, std::milli>(t_d2h_end - t_d2h_start).count();

		m.out_bytes = num_words * 4;
		for (size_t i = 0; i < num_words; ++i) m.checksum += out[i];

		auto t_end = std::chrono::high_resolution_clock::now();
		m.t_total = std::chrono::duration<double, std::milli>(t_end - t_start).count();
		m.gpu_end_ns = get_ns();

		clReleaseMemObject(input_buf);
		clReleaseMemObject(output_buf);
		_aligned_free(packed);
		m.output_buffer = out;
		m.gpu_confirmed = true;
		return m;
	}

	void shutdown() {
		if (kernel) clReleaseKernel(kernel);
		if (program) clReleaseProgram(program);
		if (queue) clReleaseCommandQueue(queue);
		if (context) clReleaseContext(context);
		if (ocl_lib) FreeLibrary(ocl_lib);
	}
};