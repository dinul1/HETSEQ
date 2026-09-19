#include "FASTAParser.h"
#include <windows.h>
#include <commdlg.h>
#include <malloc.h>
#include <cstring>
#include <cstdio>

const char* E_COLI_FALLBACK =
"AGCTTTTCATTCTGACTGCAACGGGCAATATGTCTGTGTGGATTAAAAAAAGAGTGTCTGATAGCAGC"
"TTCTGAACTGGTTACCTGCCGTGAGTTTTTTTTTAAGAGCGTTTCCCTGTTGCTTGCATCTGAACGGTCTG"
"TCAGCTTGCATCAGGATTGGCAGTGCGTTATCGCTTCACCTGTTATCGGCGTGCCGTCAGCTTCAGAGTGA"
"GCACAGTTCATGACTGAGTGGAAGAGCGTTTCCCTGTTGCTTGCATCTGAACGGTCTGTCAGCTTGCATC";

bool FASTAParser::open_dialog_and_load(std::vector<char>& genome_buffer) {
	char fasta_path[MAX_PATH] = { 0 };
	OPENFILENAMEA ofn;
	ZeroMemory(&ofn, sizeof(ofn));
	ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = NULL; ofn.lpstrFile = fasta_path;
	fasta_path[0] = '\0'; ofn.nMaxFile = MAX_PATH;
	ofn.lpstrFilter = "FASTA Genome Files\0*.fasta;*.fa;*.fna;*.ffn\0All Files\0*.*\0";
	ofn.nFilterIndex = 1; ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_EXPLORER;

	if (!GetOpenFileNameA(&ofn)) {
		printf("[WORKLOAD] No file provided. Using embedded E. coli snippet.\n");
		size_t len = strlen(E_COLI_FALLBACK);
		genome_buffer.assign(E_COLI_FALLBACK, E_COLI_FALLBACK + len);
		return false;
	}

	FILE* file = NULL;
	if (fopen_s(&file, fasta_path, "r") != 0 || !file) {
		printf("[WORKLOAD] Failed to open '%s'. Using embedded snippet.\n", fasta_path);
		size_t len = strlen(E_COLI_FALLBACK);
		genome_buffer.assign(E_COLI_FALLBACK, E_COLI_FALLBACK + len);
		return false;
	}

	printf("[WORKLOAD] Parsing FASTA dataset: %s\n", fasta_path);
	char line[1024];
	while (fgets(line, sizeof(line), file)) {
		if (line[0] == '>' || line[0] == ';' || line[0] == '@') continue;
		for (int i = 0; line[i] != '\0'; ++i) {
			char c = line[i];
			if (c == '\n' || c == '\r' || c == ' ') continue;
			if (c == 'A' || c == 'C' || c == 'G' || c == 'T' || c == 'a' || c == 'c' || c == 'g' || c == 't') {
				if (c >= 'a' && c <= 'z') c -= 32;
				genome_buffer.push_back(c);
			}
		}
	}
	fclose(file);
	printf("[WORKLOAD] Loaded %zu base pairs into memory.\n", genome_buffer.size());
	return true;
}

uint32_t* FASTAParser::pack_dna_2bit(const char* dna_seq, size_t size, size_t& out_num_words) {
	out_num_words = (size + 15) / 16;
	uint32_t* packed = (uint32_t*)_aligned_malloc(out_num_words * 4, 64);
	memset(packed, 0, out_num_words * 4);
	for (size_t i = 0; i < size; ++i) {
		uint32_t base = 0;
		switch (dna_seq[i]) {
		case 'A': base = 0; break; case 'C': base = 1; break;
		case 'G': base = 2; break; case 'T': base = 3; break;
		}
		packed[i / 16] |= (base << ((i % 16) * 2));
	}
	return packed;
}