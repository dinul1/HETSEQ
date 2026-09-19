#pragma once
#include "Types.h"
#include "Backend.h"
#include "FASTAParser.h"
#include <vector>
#include <string>
#include <cstdio>
#include <malloc.h>

class Validator {
public:
	static bool validate_output_identity(ComputeBackend* cpu, ComputeBackend* gpu, const std::vector<char>& genome_data) {
		printf("\n[P0.1] Exact Output Validation (1MB Sample)\n");
		if (!cpu || !gpu) return false;

		size_t val_size = std::min(genome_data.size(), (size_t)1000000);
		Workload w = { GenomicTaskType::BASECALLING, genome_data.data(), 0, val_size };

		ExecutionMetrics cpu_m = cpu->execute(w);
		ExecutionMetrics gpu_m = gpu->execute(w);

		if (!cpu_m.output_buffer || !gpu_m.output_buffer) {
			printf("  FAIL: Output buffer missing\n");
			if (cpu_m.output_buffer) _aligned_free(cpu_m.output_buffer);
			if (gpu_m.output_buffer) _aligned_free(gpu_m.output_buffer);
			return false;
		}

		size_t words = cpu_m.out_bytes / 4;
		if (cpu_m.out_bytes != gpu_m.out_bytes) {
			printf("  FAIL: Output size mismatch (CPU: %zu, GPU: %zu)\n", cpu_m.out_bytes, gpu_m.out_bytes);
			_aligned_free(cpu_m.output_buffer); _aligned_free(gpu_m.output_buffer);
			return false;
		}

		int mismatches = 0;
		size_t first_mismatch = (size_t)-1;
		for (size_t i = 0; i < words; ++i) {
			if (cpu_m.output_buffer[i] != gpu_m.output_buffer[i]) {
				mismatches++;
				if (first_mismatch == (size_t)-1) first_mismatch = i;
			}
		}

		printf("  Output bytes: %zu\n", cpu_m.out_bytes);
		printf("  Reference match: %s\n", mismatches == 0 ? "YES" : "NO");
		printf("  Mismatched elements: %d\n", mismatches);
		if (mismatches > 0) printf("  First mismatch at index: %zu\n", first_mismatch);

		_aligned_free(cpu_m.output_buffer);
		_aligned_free(gpu_m.output_buffer);
		return (mismatches == 0);
	}

	static bool stress_test_bounds(ComputeBackend* cpu, ComputeBackend* gpu, const std::vector<char>& genome_data) {
		printf("\n[P0.2] Stress-Testing Bounds (1, 3, 31, 63, 64, 65, 127, 128, 129)\n");
		if (!cpu || !gpu) return false;

		size_t test_sizes[] = { 1, 3, 31, 32, 63, 64, 65, 127, 128, 129 };
		bool all_passed = true;

		for (size_t size : test_sizes) {
			if (size > genome_data.size()) continue;
			Workload w = { GenomicTaskType::BASECALLING, genome_data.data(), 0, size };
			ExecutionMetrics cpu_m = cpu->execute(w);
			ExecutionMetrics gpu_m = gpu->execute(w);

			bool passed = (cpu_m.checksum == gpu_m.checksum && cpu_m.out_bytes == gpu_m.out_bytes);
			if (!passed) all_passed = false;

			printf("  Size %3zu: Checksum CPU[%llu] vs GPU[%llu] -> %s\n",
				size, (unsigned long long)cpu_m.checksum, (unsigned long long)gpu_m.checksum,
				passed ? "PASS" : "FAIL");

			if (cpu_m.output_buffer) _aligned_free(cpu_m.output_buffer);
			if (gpu_m.output_buffer) _aligned_free(gpu_m.output_buffer);
		}
		return all_passed;
	}

	// P0.3 Validate full dynamic partitioning output
	static bool validate_dynamic_partition(ComputeBackend* cpu, ComputeBackend* gpu, const std::vector<char>& genome_data) {
		printf("\n[P0.3] Dynamic Partition Output Validation (Full Dataset)\n");
		if (!cpu || !gpu) return false;

		// Run CPU on entire dataset to get reference
		Workload full_w = { GenomicTaskType::BASECALLING, genome_data.data(), 0, genome_data.size() };
		ExecutionMetrics cpu_ref = cpu->execute(full_w);

		// Manually partition exactly as the scheduler would (50/50 split aligned to 16)
		size_t cpu_size = ((genome_data.size() / 2) / 16) * 16;
		size_t gpu_size = genome_data.size() - cpu_size;

		Workload cpu_part = { GenomicTaskType::BASECALLING, genome_data.data(), 0, cpu_size };
		Workload gpu_part = { GenomicTaskType::BASECALLING, genome_data.data() + cpu_size, 0, gpu_size };

		ExecutionMetrics cpu_m = cpu->execute(cpu_part);
		ExecutionMetrics gpu_m = gpu->execute(gpu_part);

		// Merge checksums
		uint64_t dynamic_checksum = cpu_m.checksum + gpu_m.checksum;

		bool checksum_match = (cpu_ref.checksum == dynamic_checksum);
		printf("  CPU Reference Checksum: %llu\n", (unsigned long long)cpu_ref.checksum);
		printf("  Dynamic Merged Checksum: %llu\n", (unsigned long long)dynamic_checksum);
		printf("  Partition match: %s\n", checksum_match ? "YES" : "NO");

		if (cpu_ref.output_buffer) _aligned_free(cpu_ref.output_buffer);
		if (cpu_m.output_buffer) _aligned_free(cpu_m.output_buffer);
		if (gpu_m.output_buffer) _aligned_free(gpu_m.output_buffer);

		return checksum_match;
	}
};