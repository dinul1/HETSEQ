#pragma once
#include "Backend.h"
#include "FASTAParser.h"
#include <malloc.h>
#include <cstring>
#include <chrono>

class CPUBackend : public ComputeBackend {
public:
	RuntimeType get_type() override { return RuntimeType::CPU; }
	const char* get_name() override { return "Internal C++ 2-bit K-mer Hash"; }

	ExecutionMetrics execute(const Workload& work) override {
		ExecutionMetrics m;
		m.cpu_start_ns = get_ns();
		auto t_start = std::chrono::high_resolution_clock::now();

		size_t num_words;
		auto t_enc_start = std::chrono::high_resolution_clock::now();
		uint32_t* packed = FASTAParser::pack_dna_2bit(work.dna_seq + work.offset, work.size_bytes, num_words);
		auto t_enc_end = std::chrono::high_resolution_clock::now();
		m.t_encode = std::chrono::duration<double, std::milli>(t_enc_end - t_enc_start).count();

		uint32_t* out = (uint32_t*)_aligned_malloc(num_words * 4, 64);
		memset(out, 0, num_words * 4);

		auto t_comp_start = std::chrono::high_resolution_clock::now();
		for (size_t i = 0; i < num_words; ++i) {
			uint32_t packed_bases = packed[i];
			uint32_t hash = 0;
			for (int j = 0; j < 16; ++j) {
				uint32_t base = (packed_bases >> (j * 2)) & 0x3;
				hash = (hash * 31 + base) % 9973;
			}
			out[i] = hash;
		}
		auto t_comp_end = std::chrono::high_resolution_clock::now();
		m.t_compute = std::chrono::duration<double, std::milli>(t_comp_end - t_comp_start).count();

		m.out_bytes = num_words * 4;
		for (size_t i = 0; i < num_words; ++i) m.checksum += out[i];

		auto t_end = std::chrono::high_resolution_clock::now();
		m.t_total = std::chrono::duration<double, std::milli>(t_end - t_start).count();
		m.cpu_end_ns = get_ns();

		m.reference_checksum = m.checksum;
		m.mismatch_count = 0;
		m.correctness = true;

		_aligned_free(packed);
		m.output_buffer = out;
		return m;
	}

	// FIX: Add empty shutdown for CPU
	void shutdown() override {}
};