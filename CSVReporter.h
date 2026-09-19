#pragma once
#include "Types.h"
#include <fstream>
#include <vector>
#include <string>
#include <numeric>
#include <cmath>
#include <algorithm>
#include <iostream>

class CSVReporter {
private:
	std::string filename;
	std::ofstream file;
public:
	CSVReporter(const std::string& fname) : filename(fname) {
		file.open(filename);
		if (file.is_open()) {
			file << "Run_ID,Mode,Task,Input_Bytes,Total_ms,Encode_ms,H2D_ms,Compute_ms,D2H_ms,Scheduler_ms,Telemetry_ms,Merge_ms,Sync_ms,"
				<< "Predicted_CPU,Predicted_GPU,Predicted_Hetero,CPU_Start_ns,CPU_End_ns,GPU_Start_ns,GPU_End_ns,"
				<< "Partition_Count,CPU_Fraction,GPU_Fraction,Checksum,Correctness\n";
		}
	}
	~CSVReporter() { if (file.is_open()) file.close(); }

	void log_task(int rep, const std::string& mode, const std::string& task, const ExecutionMetrics& m, const std::string& backend) {
		if (!file.is_open()) return;
		file << rep << "," << mode << "," << task << "," << m.out_bytes << ","
			<< m.t_total << "," << m.t_encode << "," << m.t_h2d << ","
			<< m.t_compute << "," << m.t_d2h << "," << m.t_scheduler << "," << m.t_telemetry << ","
			<< m.t_merge << "," << m.t_sync << ","
			<< m.predicted_cpu << "," << m.predicted_gpu << "," << m.predicted_hetero << ","
			<< m.cpu_start_ns << "," << m.cpu_end_ns << "," << m.gpu_start_ns << "," << m.gpu_end_ns << ","
			<< m.partition_count << "," << m.cpu_fraction << "," << m.gpu_fraction << ","
			<< m.checksum << "," << (m.correctness ? "YES" : "NO") << "\n";
	}

	static void print_stats(const std::vector<double>& times, const std::string& name) {
		if (times.empty()) return;
		double sum = std::accumulate(times.begin(), times.end(), 0.0);
		double mean = sum / times.size();
		double sq_sum = std::inner_product(times.begin(), times.end(), times.begin(), 0.0);
		double stdev = std::sqrt(sq_sum / times.size() - mean * mean);
		double med = calculate_median(times);
		double min_v = *std::min_element(times.begin(), times.end());
		double max_v = *std::max_element(times.begin(), times.end());

		printf("  %s Statistics (n=%zu):\n", name.c_str(), times.size());
		printf("    Mean: %.4f ms | Median: %.4f ms | StdDev: %.4f ms\n", mean, med, stdev);
		printf("    Min:  %.4f ms | Max:    %.4f ms\n", min_v, max_v);
	}
};