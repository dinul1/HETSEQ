#define NOMINMAX
#include <windows.h>
#include <initguid.h> 
#include <dxgi.h>

#include "Types.h"
#include "ThreadPool.h"
#include "Telemetry.h"
#include "FASTAParser.h"
#include "CPUBackend.h"
#include "D3D11Backend.h"
#include "OpenCLBackend.h"
#include "BackendFactory.h"
#include "Scheduler.h"
#include "Validator.h"
#include "CSVReporter.h"

#include <vector>
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>
#include <atomic>
#include <limits>

#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "ole32.lib")

void print_menu() {
	printf("\n===================================================================\n");
	printf(" Dynamic Hardware-Aware Adaptive Framework (Enterprise Suite)   \n");
	printf("===================================================================\n");
	printf(" 1. Run All Experiments (P0, P1, P3, P16, P17)\n");
	printf(" 2. P0 - Correctness Validation Only\n");
	printf(" 3. P1 - Statistical Benchmarking (Configurable)\n");
	printf(" 4. P3 - Scaling Experiments\n");
	printf(" 5. P16 - Ablation Experiments\n");
	printf(" 6. P17 - Adaptation Experiments (CPU Load)\n");
	printf(" 7. P8 - Adaptive Granularity\n");
	printf(" 8. P13 - Energy Efficiency Estimation\n");
	printf(" 9. P14 - Full Experimental Matrix\n");
	printf(" 10. Exit\n");
	printf("===================================================================\n");
	printf(" Select an option: ");
}

int main() {
	SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);

	printf("===================================================================\n");
	printf(" Dynamic Hardware-Aware Adaptive Framework (Enterprise Edition)  \n");
	printf("===================================================================\n");

	// 1. Enumerate all GPUs
	BackendFactory::print_gpu_list();

	// 2. Select GPU backend
	std::vector<ComputeBackend*> gpu_backends;
	printf("\nSelect GPU index (or -1 for all, -2 for CPU-only): ");
	int gpu_choice = -1;
	if (scanf_s("%d", &gpu_choice) != 1) gpu_choice = -1;

	if (gpu_choice == -2) {
		printf("[SYSTEM] CPU-only mode selected.\n");
	}
	else if (gpu_choice == -1) {
		// Use all GPUs
		std::vector<GPUInfo> gpus = BackendFactory::enumerate_gpus();
		for (size_t i = 0; i < gpus.size(); i++) {
			printf("[SYSTEM] Initializing GPU %zu...\n", i);
			ComputeBackend* backend = BackendFactory::create_gpu_backend((int)i);
			if (backend) {
				printf("[SYSTEM] %s initialized successfully.\n", backend->get_name());
				gpu_backends.push_back(backend);
			}
		}
	}
	else {
		// Use specific GPU
		printf("[SYSTEM] Initializing GPU %d...\n", gpu_choice);
		ComputeBackend* backend = BackendFactory::create_gpu_backend(gpu_choice);
		if (backend) {
			printf("[SYSTEM] %s initialized successfully.\n", backend->get_name());
			gpu_backends.push_back(backend);
		}
	}

	// 3. CPU Backend
	CPUBackend cpu;

	// 4. Telemetry
	Telemetry telemetry;
	if (!telemetry.init()) {
		printf("[WARN] PDH Telemetry failed. Continuing without telemetry.\n");
	}

	// 5. Dataset
	FASTAParser parser;
	std::vector<char> genome_data;
	printf("\n[SYSTEM] Opening file dialog to select genome dataset...\n");
	parser.open_dialog_and_load(genome_data);
	if (genome_data.size() < 124) { pause_and_exit(1); }

	// 6. Scalable Thread Pool
	unsigned int hw_threads = std::thread::hardware_concurrency();
	if (hw_threads == 0) hw_threads = 4;
	printf("[SYSTEM] Thread Pool: %u threads\n", hw_threads);
	ThreadPool pool(hw_threads);

	// 7. Scheduler
	Scheduler scheduler(pool, telemetry);
	scheduler.add_cpu_backend(static_cast<ComputeBackend*>(&cpu));
	for (auto* gb : gpu_backends) {
		scheduler.add_gpu_backend(gb);
	}

	printf("[SYSTEM] Active GPUs: %zu\n", scheduler.gpu_count());

	CSVReporter reporter("benchmark_results.csv");

	struct TaskDef { const char* name; GenomicTaskType type; };
	TaskDef tasks[4] = {
		{ "Basecalling", GenomicTaskType::BASECALLING },
		{ "Alignment", GenomicTaskType::ALIGNMENT },
		{ "VariantCalling", GenomicTaskType::VARIANT_CALLING },
		{ "Assembly", GenomicTaskType::ASSEMBLY }
	};

	auto run_suite = [&](SchedulingMode mode, const char* mode_name, int rep, size_t current_chunk_size) {
		auto suite_start = std::chrono::high_resolution_clock::now();
		uint64_t total_checksum = 0;

		for (int i = 0; i < 4; i++) {
			Workload w = { tasks[i].type, genome_data.data(), i * current_chunk_size, current_chunk_size };
			ExecutionMetrics m = scheduler.dispatch(w, mode);
			total_checksum += m.checksum;
			reporter.log_task(rep, mode_name, tasks[i].name, m, "Auto");
			if (m.output_buffer) _aligned_free(m.output_buffer);
		}

		auto suite_end = std::chrono::high_resolution_clock::now();
		double t = std::chrono::duration<double, std::milli>(suite_end - suite_start).count();
		printf("[SUITE RESULT] %s (Rep %d, Chunk %zuKB) Time: %.4fms (Checksum: %llu)\n",
			mode_name, rep, current_chunk_size / 1024, t, (unsigned long long)total_checksum);
		return t;
	};

	auto run_p1 = [&](int reps) {
		printf("\n===================================================================\n");
		printf(" PHASE 1: Statistical Benchmarking (%d Repetitions)\n", reps);
		printf("===================================================================\n");

		size_t chunk_size = genome_data.size() / 4;
		if (chunk_size < 31) chunk_size = 31;

		std::vector<double> t_cpu_runs, t_hetero_runs, t_dynamic_runs;

		for (int rep = 0; rep < reps; rep++) {
			printf("\n=== Repetition %d ===\n", rep + 1);
			t_cpu_runs.push_back(run_suite(SchedulingMode::BASELINE_CPU, "Baseline1", rep, chunk_size));
			t_hetero_runs.push_back(run_suite(SchedulingMode::BASELINE_HETERO, "Baseline2", rep, chunk_size));
			scheduler.set_ablation(true, true, true);
			t_dynamic_runs.push_back(run_suite(SchedulingMode::DYNAMIC_ADAPTIVE, "Dynamic", rep, chunk_size));
		}

		printf("\n===================================================================\n");
		printf(" FINAL EXPERIMENTAL RESULTS (%d Repetitions)\n", reps);
		printf("===================================================================\n");
		CSVReporter::print_stats(t_cpu_runs, "Baseline 1 (CPU Only)");
		CSVReporter::print_stats(t_hetero_runs, "Baseline 2 (Static Hetero)");
		CSVReporter::print_stats(t_dynamic_runs, "Dynamic Adaptive");

		double med_cpu = calculate_median(t_cpu_runs);
		double med_dynamic = calculate_median(t_dynamic_runs);
		printf("\n  Speedup (Median CPU / Median Dynamic): %.2fx\n", (med_dynamic > 0) ? med_cpu / med_dynamic : 0);
	};

	int choice = 0;
	do {
		print_menu();
		if (scanf_s("%d", &choice) != 1) {
			while (getchar() != '\n');
			choice = 0;
		}

		switch (choice) {
		case 1: {
			printf("\n[SYSTEM] Running All Core Experiments...\n");
			ComputeBackend* gpu = scheduler.get_gpu();
			if (gpu) {
				if (!Validator::validate_output_identity(&cpu, gpu, genome_data) ||
					!Validator::stress_test_bounds(&cpu, gpu, genome_data) ||
					!Validator::validate_dynamic_partition(&cpu, gpu, genome_data)) {
					printf("[FATAL] Validation failed. Aborting.\n");
					break;
				}
			}
			printf("\n[P0] Hardware Calibration...\n");
			size_t calib_size = std::min(genome_data.size(), (size_t)100000);
			for (int i = 0; i < 4; i++) {
				Workload calib_work = { tasks[i].type, genome_data.data(), 0, calib_size };
				ExecutionMetrics cpu_calib = cpu.execute(calib_work);
				scheduler.add_record({ calib_work.type, calib_size, cpu_calib.t_total, RuntimeType::CPU });
				if (cpu_calib.output_buffer) _aligned_free(cpu_calib.output_buffer);
				if (gpu) {
					ExecutionMetrics gpu_calib = gpu->execute(calib_work);
					scheduler.add_record({ calib_work.type, calib_size, gpu_calib.t_total, gpu->get_type() });
					if (gpu_calib.output_buffer) _aligned_free(gpu_calib.output_buffer);
				}
			}
			run_p1(5);

			// P3
			printf("\n===================================================================\n");
			printf(" PHASE 3: Scaling Experiments\n");
			printf("===================================================================\n");
			size_t test_sizes[] = { 10000, 100000, 1000000, 10000000 };
			for (size_t test_size : test_sizes) {
				size_t actual_chunk = std::min(genome_data.size() / 4, test_size);
				if (actual_chunk < 16) actual_chunk = 16;
				printf("\n--- Testing Chunk Size: %zu bytes (%zu KB) ---\n", actual_chunk, actual_chunk / 1024);
				std::vector<double> t_cpu, t_dyn;
				for (int rep = 0; rep < 3; rep++) {
					t_cpu.push_back(run_suite(SchedulingMode::BASELINE_CPU, "Scale_CPU", rep, actual_chunk));
					scheduler.set_ablation(true, true, true);
					t_dyn.push_back(run_suite(SchedulingMode::DYNAMIC_ADAPTIVE, "Scale_Dynamic", rep, actual_chunk));
				}
				printf("  Scale Results (Chunk %zuKB):\n", actual_chunk / 1024);
				CSVReporter::print_stats(t_cpu, "    CPU Only");
				CSVReporter::print_stats(t_dyn, "    Dynamic");
				double med_c = calculate_median(t_cpu);
				double med_d = calculate_median(t_dyn);
				printf("    Speedup: %.2fx\n", (med_d > 0) ? med_c / med_d : 0);
			}

			// P16
			printf("\n===================================================================\n");
			printf(" PHASE 16: Ablation Experiments\n");
			printf("===================================================================\n");
			size_t main_chunk = genome_data.size() / 4;
			if (main_chunk < 16) main_chunk = 16;

			scheduler.set_ablation(false, true, true);
			std::vector<double> t_no_fb;
			for (int rep = 0; rep < 3; rep++) t_no_fb.push_back(run_suite(SchedulingMode::DYNAMIC_ADAPTIVE, "NoFeedback", rep, main_chunk));
			CSVReporter::print_stats(t_no_fb, "Dynamic (No Feedback)");

			scheduler.set_ablation(true, false, true);
			std::vector<double> t_no_tel;
			for (int rep = 0; rep < 3; rep++) t_no_tel.push_back(run_suite(SchedulingMode::DYNAMIC_ADAPTIVE, "NoTelemetry", rep, main_chunk));
			CSVReporter::print_stats(t_no_tel, "Dynamic (No Telemetry)");

			scheduler.set_ablation(true, true, false);
			std::vector<double> t_no_part;
			for (int rep = 0; rep < 3; rep++) t_no_part.push_back(run_suite(SchedulingMode::DYNAMIC_ADAPTIVE, "NoPartition", rep, main_chunk));
			CSVReporter::print_stats(t_no_part, "Dynamic (No Partitioning)");

			// P17
			printf("\n===================================================================\n");
			printf(" PHASE 17: Adaptation Experiments (CPU Load)\n");
			printf("===================================================================\n");
			printf("\n--- Stage 1: Normal Run (No artificial load) ---\n");
			std::vector<double> t_normal;
			for (int rep = 0; rep < 3; rep++) t_normal.push_back(run_suite(SchedulingMode::DYNAMIC_ADAPTIVE, "Adapt_Normal", rep, main_chunk));
			CSVReporter::print_stats(t_normal, "  Normal Dynamic");

			printf("\n--- Stage 2: CPU Saturated (Artificial load) ---\n");
			std::atomic<bool> stress(true);
			std::thread stress_thread([&]() {
				volatile double x = 0;
				while (stress) x += 1.0;
			});
			Sleep(100);
			std::vector<double> t_stressed;
			for (int rep = 0; rep < 3; rep++) t_stressed.push_back(run_suite(SchedulingMode::DYNAMIC_ADAPTIVE, "Adapt_Stressed", rep, main_chunk));
			CSVReporter::print_stats(t_stressed, "  Stressed Dynamic");
			stress = false;
			stress_thread.join();

			printf("\n[SYSTEM] All experiments complete. Results saved to benchmark_results.csv\n");
			break;
		}
		case 2: {
			ComputeBackend* gpu = scheduler.get_gpu();
			if (gpu) {
				if (!Validator::validate_output_identity(&cpu, gpu, genome_data) ||
					!Validator::stress_test_bounds(&cpu, gpu, genome_data) ||
					!Validator::validate_dynamic_partition(&cpu, gpu, genome_data)) {
					printf("[FATAL] Validation failed.\n");
				}
			}
			break;
		}
		case 3: {
			printf("Enter number of repetitions (e.g., 5): ");
			int reps = 5;
			if (scanf_s("%d", &reps) != 1 || reps < 1) reps = 5;
			run_p1(reps);
			break;
		}
		case 4: {
			printf("\n===================================================================\n");
			printf(" PHASE 3: Scaling Experiments\n");
			printf("===================================================================\n");
			size_t test_sizes[] = { 10000, 100000, 1000000, 10000000 };
			for (size_t test_size : test_sizes) {
				size_t actual_chunk = std::min(genome_data.size() / 4, test_size);
				if (actual_chunk < 16) actual_chunk = 16;
				printf("\n--- Testing Chunk Size: %zu bytes (%zu KB) ---\n", actual_chunk, actual_chunk / 1024);
				std::vector<double> t_cpu, t_dyn;
				for (int rep = 0; rep < 3; rep++) {
					t_cpu.push_back(run_suite(SchedulingMode::BASELINE_CPU, "Scale_CPU", rep, actual_chunk));
					scheduler.set_ablation(true, true, true);
					t_dyn.push_back(run_suite(SchedulingMode::DYNAMIC_ADAPTIVE, "Scale_Dynamic", rep, actual_chunk));
				}
				printf("  Scale Results (Chunk %zuKB):\n", actual_chunk / 1024);
				CSVReporter::print_stats(t_cpu, "    CPU Only");
				CSVReporter::print_stats(t_dyn, "    Dynamic");
				double med_c = calculate_median(t_cpu);
				double med_d = calculate_median(t_dyn);
				printf("    Speedup: %.2fx\n", (med_d > 0) ? med_c / med_d : 0);
			}
			break;
		}
		case 5: {
			printf("\n===================================================================\n");
			printf(" PHASE 16: Ablation Experiments\n");
			printf("===================================================================\n");
			size_t main_chunk = genome_data.size() / 4;
			if (main_chunk < 16) main_chunk = 16;

			scheduler.set_ablation(false, true, true);
			std::vector<double> t_no_fb;
			for (int rep = 0; rep < 3; rep++) t_no_fb.push_back(run_suite(SchedulingMode::DYNAMIC_ADAPTIVE, "NoFeedback", rep, main_chunk));
			CSVReporter::print_stats(t_no_fb, "Dynamic (No Feedback)");

			scheduler.set_ablation(true, false, true);
			std::vector<double> t_no_tel;
			for (int rep = 0; rep < 3; rep++) t_no_tel.push_back(run_suite(SchedulingMode::DYNAMIC_ADAPTIVE, "NoTelemetry", rep, main_chunk));
			CSVReporter::print_stats(t_no_tel, "Dynamic (No Telemetry)");

			scheduler.set_ablation(true, true, false);
			std::vector<double> t_no_part;
			for (int rep = 0; rep < 3; rep++) t_no_part.push_back(run_suite(SchedulingMode::DYNAMIC_ADAPTIVE, "NoPartition", rep, main_chunk));
			CSVReporter::print_stats(t_no_part, "Dynamic (No Partitioning)");
			break;
		}
		case 6: {
			printf("\n===================================================================\n");
			printf(" PHASE 17: Adaptation Experiments (CPU Load)\n");
			printf("===================================================================\n");
			size_t main_chunk = genome_data.size() / 4;
			if (main_chunk < 16) main_chunk = 16;

			printf("\n--- Stage 1: Normal Run ---\n");
			std::vector<double> t_normal;
			for (int rep = 0; rep < 3; rep++) t_normal.push_back(run_suite(SchedulingMode::DYNAMIC_ADAPTIVE, "Adapt_Normal", rep, main_chunk));
			CSVReporter::print_stats(t_normal, "  Normal Dynamic");

			printf("\n--- Stage 2: CPU Saturated ---\n");
			std::atomic<bool> stress(true);
			std::thread stress_thread([&]() {
				volatile double x = 0;
				while (stress) x += 1.0;
			});
			Sleep(100);
			std::vector<double> t_stressed;
			for (int rep = 0; rep < 3; rep++) t_stressed.push_back(run_suite(SchedulingMode::DYNAMIC_ADAPTIVE, "Adapt_Stressed", rep, main_chunk));
			CSVReporter::print_stats(t_stressed, "  Stressed Dynamic");
			stress = false;
			stress_thread.join();
			break;
		}
		case 7: {
			printf("\n===================================================================\n");
			printf(" PHASE 8: Adaptive Granularity\n");
			printf("===================================================================\n");
			size_t main_chunk = genome_data.size() / 4;
			if (main_chunk < 16) main_chunk = 16;
			size_t divisors[] = { 1, 2, 4, 8, 16 };
			for (size_t div : divisors) {
				size_t chunk = main_chunk / div;
				if (chunk < 16) chunk = 16;
				printf("\n--- Granularity: %zu chunks of %zu KB ---\n", div * 4, chunk / 1024);
				std::vector<double> t_dyn;
				for (int rep = 0; rep < 3; rep++) {
					t_dyn.push_back(run_suite(SchedulingMode::DYNAMIC_ADAPTIVE, "Granularity", rep, chunk));
				}
				CSVReporter::print_stats(t_dyn, "  Dynamic");
			}
			break;
		}
		case 8: {
			printf("\n===================================================================\n");
			printf(" PHASE 13: Energy Efficiency Estimation\n");
			printf("===================================================================\n");
			size_t main_chunk = genome_data.size() / 4;
			if (main_chunk < 16) main_chunk = 16;

			std::vector<double> t_cpu;
			for (int rep = 0; rep < 3; rep++) t_cpu.push_back(run_suite(SchedulingMode::BASELINE_CPU, "Energy_CPU", rep, main_chunk));
			double med_cpu = calculate_median(t_cpu);
			double cpu_energy = (med_cpu / 1000.0) * 15.0;

			std::vector<double> t_dyn;
			for (int rep = 0; rep < 3; rep++) t_dyn.push_back(run_suite(SchedulingMode::DYNAMIC_ADAPTIVE, "Energy_Dyn", rep, main_chunk));
			double med_dyn = calculate_median(t_dyn);
			double dyn_energy = (med_dyn / 1000.0) * 30.0;

			printf("\n  CPU Energy:    %.2f Joules (%.2f J/MB)\n", cpu_energy, cpu_energy / (main_chunk * 4 / 1000000.0));
			printf("  Dynamic Energy: %.2f Joules (%.2f J/MB)\n", dyn_energy, dyn_energy / (main_chunk * 4 / 1000000.0));
			break;
		}
		case 9: {
			printf("\n===================================================================\n");
			printf(" PHASE 14: Full Experimental Matrix\n");
			printf("===================================================================\n");
			size_t test_sizes[] = { 10000, 100000, 1000000 };
			for (size_t test_size : test_sizes) {
				size_t actual_chunk = std::min(genome_data.size() / 4, test_size);
				if (actual_chunk < 16) actual_chunk = 16;
				printf("\n--- Matrix Chunk: %zu KB ---\n", actual_chunk / 1024);
				std::vector<double> t_cpu, t_dyn;
				for (int rep = 0; rep < 2; rep++) {
					t_cpu.push_back(run_suite(SchedulingMode::BASELINE_CPU, "Matrix_CPU", rep, actual_chunk));
					t_dyn.push_back(run_suite(SchedulingMode::DYNAMIC_ADAPTIVE, "Matrix_Dyn", rep, actual_chunk));
				}
				double med_c = calculate_median(t_cpu);
				double med_d = calculate_median(t_dyn);
				printf("    Speedup: %.2fx, CPU Energy: %.2fJ, Dynamic Energy: %.2fJ\n",
					(med_d > 0) ? med_c / med_d : 0, (med_c / 1000.0)*15.0, (med_d / 1000.0)*30.0);
			}
			break;
		}
		case 10: {
			printf("\nExiting...\n");
			break;
		}
		}
	} while (choice != 10);

	// Cleanup
	for (auto* gb : gpu_backends) {
		gb->shutdown();
		delete gb;
	}
	telemetry.shutdown();
	return 0;
}