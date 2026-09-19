#include "Scheduler.h"
#include <stdio.h>
#include <algorithm>
#include <malloc.h>

void Scheduler::add_record(const ExecutionRecord& rec) {
	if (!enable_feedback) return;
	std::lock_guard<std::mutex> lock(mtx);
	history.push_back(rec);
}

double Scheduler::predict_time(GenomicTaskType type, RuntimeType target, size_t size_bytes) {
	std::lock_guard<std::mutex> lock(mtx);
	double total_throughput = 0.0;
	int matches = 0;
	for (const auto& rec : history) {
		if (rec.type == type && rec.target == target && rec.actual_time > 0.0) {
			total_throughput += static_cast<double>(rec.size_bytes) / rec.actual_time;
			matches++;
		}
	}
	if (matches > 0) {
		double avg = total_throughput / matches;
		if (avg > 0) return static_cast<double>(size_bytes) / avg;
	}
	if (target == RuntimeType::CPU) return static_cast<double>(size_bytes) / 50.0;
	return static_cast<double>(size_bytes) / 5000.0;
}

ExecutionMetrics Scheduler::dispatch(const Workload& work, SchedulingMode mode) {
	ExecutionMetrics m;
	ComputeBackend* cpu = get_cpu();
	ComputeBackend* gpu = get_gpu();

	if (mode == SchedulingMode::BASELINE_CPU) {
		if (cpu) return cpu->execute(work);
		return m;
	}

	if (mode == SchedulingMode::BASELINE_HETERO) {
		if (work.type == GenomicTaskType::BASECALLING || work.type == GenomicTaskType::ASSEMBLY) {
			if (gpu) return gpu->execute(work);
		}
		if (cpu) return cpu->execute(work);
		return m;
	}

	// DYNAMIC_ADAPTIVE
	const size_t STATIC_THRESHOLD = 400000;
	if (work.size_bytes < STATIC_THRESHOLD) {
		if (cpu) return cpu->execute(work);
		return m;
	}

	if (!gpu) {
		if (cpu) return cpu->execute(work);
		return m;
	}

	double t_cpu = predict_time(work.type, RuntimeType::CPU, work.size_bytes);
	double t_gpu = predict_time(work.type, gpu->get_type(), work.size_bytes);

	double cpu_load = enable_telemetry ? telemetry.get_cpu_load() : 0.0;
	if (cpu_load > 75.0) t_cpu *= 1.5;

	double p_cpu = (t_cpu > 0) ? 1.0 / t_cpu : 0.0;
	double p_gpu = (t_gpu > 0) ? 1.0 / t_gpu : 0.0;
	double total_p = p_cpu + p_gpu;
	double t_split = 0.0;

	if (total_p > 0) {
		double gpu_ratio = p_gpu / total_p;
		size_t gpu_size = static_cast<size_t>(work.size_bytes * gpu_ratio);
		size_t cpu_size = work.size_bytes - gpu_size;
		double t_cpu_part = (cpu_size > 0) ? predict_time(work.type, RuntimeType::CPU, cpu_size) : 0.0;
		double t_gpu_part = (gpu_size > 0) ? predict_time(work.type, gpu->get_type(), gpu_size) : 0.0;
		t_split = (t_cpu_part > t_gpu_part) ? t_cpu_part : t_gpu_part;
	}

	double min_time = t_cpu;
	std::string strategy = "CPU Only";
	if (t_gpu < min_time) { min_time = t_gpu; strategy = "GPU Only"; }
	if (enable_partitioning && t_split < min_time) { min_time = t_split; strategy = "CPU+GPU Partition"; }

	printf("[ADAPT] Workload: %zu bytes | Strategy: %s\n", work.size_bytes, strategy.c_str());

	if (strategy == "CPU Only") return cpu->execute(work);
	if (strategy == "GPU Only") {
		ExecutionMetrics gpu_m = gpu->execute(work);
		if (!gpu_m.gpu_confirmed && gpu_m.output_buffer) {
			_aligned_free(gpu_m.output_buffer);
			return cpu->execute(work);
		}
		return gpu_m;
	}

	// PARTITIONING
	double gpu_ratio = p_gpu / total_p;
	size_t gpu_size = static_cast<size_t>(work.size_bytes * gpu_ratio);
	size_t cpu_size = work.size_bytes - gpu_size;
	cpu_size = (cpu_size / 16) * 16;
	gpu_size = work.size_bytes - cpu_size;

	Workload cpu_work = work; cpu_work.size_bytes = cpu_size;
	Workload gpu_work = work; gpu_work.offset = work.offset + cpu_size; gpu_work.size_bytes = gpu_size;

	auto cpu_future = pool.enqueue([&]() { return cpu->execute(cpu_work); });
	auto gpu_future = pool.enqueue([&]() { return gpu->execute(gpu_work); });

	ExecutionMetrics cpu_m = cpu_future.get();
	ExecutionMetrics gpu_m = gpu_future.get();

	if (!gpu_m.gpu_confirmed) {
		if (gpu_m.output_buffer) _aligned_free(gpu_m.output_buffer);
		gpu_m = cpu->execute(gpu_work);
	}

	m.t_encode = (cpu_m.t_encode > gpu_m.t_encode) ? cpu_m.t_encode : gpu_m.t_encode;
	m.t_h2d = gpu_m.t_h2d;
	m.t_compute = (cpu_m.t_compute > gpu_m.t_compute) ? cpu_m.t_compute : gpu_m.t_compute;
	m.t_d2h = gpu_m.t_d2h;
	m.t_total = (cpu_m.t_total > gpu_m.t_total) ? cpu_m.t_total : gpu_m.t_total;
	m.checksum = cpu_m.checksum + gpu_m.checksum;
	m.out_bytes = cpu_m.out_bytes + gpu_m.out_bytes;
	m.gpu_confirmed = gpu_m.gpu_confirmed;
	m.dispatch_x = gpu_m.dispatch_x;
	m.cpu_start_ns = cpu_m.cpu_start_ns;
	m.cpu_end_ns = cpu_m.cpu_end_ns;
	m.gpu_start_ns = gpu_m.gpu_start_ns;
	m.gpu_end_ns = gpu_m.gpu_end_ns;
	m.partition_count = 2;
	m.cpu_fraction = (double)cpu_size / work.size_bytes;
	m.gpu_fraction = (double)gpu_size / work.size_bytes;

	if (cpu_m.output_buffer) _aligned_free(cpu_m.output_buffer);
	if (gpu_m.output_buffer) _aligned_free(gpu_m.output_buffer);

	return m;
}