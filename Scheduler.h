#pragma once
#include "Types.h"
#include "ThreadPool.h"
#include "Backend.h"
#include "Telemetry.h"
#include <vector>
#include <mutex>

class Scheduler {
private:
	std::vector<ComputeBackend*> cpu_backends;  // Multiple CPU backends for multi-socket
	std::vector<ComputeBackend*> gpu_backends; // Multiple GPUs
	ThreadPool& pool;
	Telemetry& telemetry;
	std::mutex mtx;
	std::vector<ExecutionRecord> history;

	bool enable_feedback = true;
	bool enable_telemetry = true;
	bool enable_partitioning = true;
	int next_gpu = 0; // Round-robin GPU selection

public:
	Scheduler(ThreadPool& p, Telemetry& t) : pool(p), telemetry(t) {}

	void add_cpu_backend(ComputeBackend* c) { cpu_backends.push_back(c); }
	void add_gpu_backend(ComputeBackend* g) { gpu_backends.push_back(g); }

	ComputeBackend* get_cpu() {
		return cpu_backends.empty() ? nullptr : cpu_backends[0];
	}

	ComputeBackend* get_gpu() {
		if (gpu_backends.empty()) return nullptr;
		// Round-robin across GPUs
		ComputeBackend* g = gpu_backends[next_gpu % gpu_backends.size()];
		next_gpu++;
		return g;
	}

	void add_record(const ExecutionRecord& rec);
	double predict_time(GenomicTaskType type, RuntimeType target, size_t size_bytes);
	ExecutionMetrics dispatch(const Workload& work, SchedulingMode mode);

	void set_ablation(bool feedback, bool telemetry, bool partitioning) {
		enable_feedback = feedback;
		enable_telemetry = telemetry;
		enable_partitioning = partitioning;
	}

	size_t gpu_count() { return gpu_backends.size(); }
};