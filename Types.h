#pragma once
#include <stdint.h>
#include <string>
#include <limits>
#include <iostream>
#include <vector>
#include <algorithm>
#include <chrono>

// Enumerations
enum class RuntimeType { CPU, D3D11, CUDA, SYCL, OPENCL };
enum class GenomicTaskType { BASECALLING, ALIGNMENT, ASSEMBLY, VARIANT_CALLING };
enum class SchedulingMode { BASELINE_CPU, BASELINE_HETERO, DYNAMIC_ADAPTIVE };

// Structs
struct Workload {
	GenomicTaskType type;
	const char* dna_seq;
	size_t offset;
	size_t size_bytes;
};

struct ExecutionMetrics {
	// Core Timing
	double t_encode = 0.0;
	double t_h2d = 0.0;
	double t_compute = 0.0;
	double t_d2h = 0.0;
	double t_total = 0.0;

	// P1.1 Instrumentation
	double t_scheduler = 0.0;
	double t_telemetry = 0.0;
	double t_merge = 0.0;
	double t_sync = 0.0;

	double predicted_cpu = 0.0;
	double predicted_gpu = 0.0;
	double predicted_hetero = 0.0;

	int64_t cpu_start_ns = 0;
	int64_t cpu_end_ns = 0;
	int64_t gpu_start_ns = 0;
	int64_t gpu_end_ns = 0;

	int partition_count = 0;
	double cpu_fraction = 0.0;
	double gpu_fraction = 0.0;

	// Validation
	uint64_t checksum = 0;
	uint64_t reference_checksum = 0;
	int mismatch_count = 0;
	bool correctness = true;

	size_t out_bytes = 0;
	bool gpu_confirmed = false;
	uint32_t dispatch_x = 0;
	uint32_t* output_buffer = nullptr;
};

struct ExecutionRecord {
	GenomicTaskType type;
	size_t size_bytes;
	double actual_time;
	RuntimeType target;
};

// Utility
inline void pause_and_exit(int code) {
	printf("\n[SYSTEM] Execution finished. Press Enter to exit...\n");
	std::cin.clear();
	std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
	std::cin.get();
	exit(code);
}

inline double calculate_median(std::vector<double> times) {
	if (times.empty()) return 0.0;
	std::sort(times.begin(), times.end());
	size_t mid = times.size() / 2;
	return (times.size() % 2 == 0) ? (times[mid - 1] + times[mid]) / 2.0 : times[mid];
}

// Helper to get current time in nanoseconds
inline int64_t get_ns() {
	return std::chrono::high_resolution_clock::now().time_since_epoch().count();
}