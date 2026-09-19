#pragma once
#include <vector>
#include <cstdint>
#include <string>

class FASTAParser {
public:
	bool open_dialog_and_load(std::vector<char>& genome_buffer);
	static uint32_t* pack_dna_2bit(const char* dna_seq, size_t size, size_t& out_num_words);
};