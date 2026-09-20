# HETSEQ

**Dynamic Hardware-Aware Adaptive Framework for Heterogeneous Genome Sequencing**

A cross-vendor, adaptive heterogeneous computing runtime that dynamically schedules genome sequencing workloads across CPU and GPU resources using a "Predict, Execute, Measure, Adapt" feedback loop. Rather than statically assigning tasks to predetermined hardware, the framework measures real-time hardware state, predicts execution costs, and dynamically partitions work across CPU and GPU concurrently.

---

## Why?

The standard approach in bioinformatics is static hardware assignment: basecalling goes to the GPU, alignment goes to the CPU, and that's that. Regardless of dataset size. Regardless of hardware state. Regardless of whether it actually helps.

This framework does something different: it **measures** the hardware, **predicts** the cost of each strategy, **executes** the optimal one, and **adapts** its model for next time. And when the workload is too small for any of that to matter, it gets out of the way and runs on the CPU with zero overhead.

---

## Results

| Dataset | Source | Size (bp) | CPU-Only (ms) | Dynamic (ms) | Speedup |
|:---|:---|---:|---:|---:|---:|
| *E. coli* K-12 genome | NCBI GCA_000005845.2 | 4,641,652 | 118.56 | 68.34 | **1.73x** |
| *S. cerevisiae* genome | NCBI GCA_000146045.2 | 12,071,326 | 294.47 | 174.47 | **1.69x** |
| *D. melanogaster* proteome | UniProt UP000000803 | 3,195,466 | 75.81 | 45.60 | 1.66x |
| *T. rubripes* proteome | UniProt UP000005226 | 6,711,840 | 159.27 | 93.93 | 1.70x |
| *D. rerio* proteome | UniProt UP000000437 | 11,939,464 | 264.27 | 164.69 | 1.60x |

All results validated with byte-for-byte checksum parity across CPU, GPU, and partitioned executions. Median of 5 repetitions with standard deviation analysis.

### Scaling: The Crossover Point

| Chunk Size | Speedup | Strategy Selected |
|---:|---:|:---|
| 9 KB | 0.97x | Static Fast Path (CPU only) |
| 97 KB | 0.96x | Static Fast Path (CPU only) |
| 976 KB | 1.68x | CPU + GPU Partition |
| 2,947 KB | 1.69x | CPU + GPU Partition |

The framework correctly identifies that GPU offloading is a net loss below ~100 KB and dynamically partitions above ~1 MB.

### Ablation: What Actually Drives the Speedup

| Configuration | Time (ms) | Δ vs Full |
|:---|---:|---:|
| Full Dynamic Framework | 174.47 | — |
| − Feedback (No Learning) | 176.96 | +1.43% |
| − Telemetry (No CPU Sensing) | 173.61 | −0.49% |
| − Partitioning (Backend Select Only) | 278.28 | **+59.51%** |

Partitioning is the dominant contributor. Feedback and telemetry have marginal individual impact because the calibration phase provides sufficient initial data.

---

## Key Features

- **Cross-vendor GPU support** — OpenCL (dynamically loaded, no SDK required) and D3D11 HLSL compute shaders
- **2-bit DNA encoding** — packs 16 bases per 32-bit word, reducing memory by 75%
- **Dynamic workload partitioning** — splits data across CPU and GPU concurrently with 16-base alignment
- **Throughput-based prediction model** — learns from historical execution data
- **Static Fast Path** — bypasses all dynamic logic for workloads below 400 KB
- **Byte-for-byte correctness validation** — three-phase protocol (P0.1–P0.3)
- **Real-time CPU telemetry** — Windows PDH API integration
- **Fault tolerance** — automatic GPU-to-CPU fallback on execution failure
- **CSV instrumentation** — machine-readable output with decomposed timing
- **Interactive experiment suite** — menu-driven selection of all experiment phases

---

## Architecture
```

                 FASTA / sequencing data
                           │
                           ▼
                  ┌─────────────────┐
                  │  FASTA Parser   │
                  │ (2-bit encoding)│
                  └────────┬────────┘
                           │
                           ▼
                  ┌─────────────────┐
                  │ Workload Profile│
                  └────────┬────────┘
                           │
             ┌─────────────┴─────────────┐
             ▼                           ▼
     Hardware Telemetry          Historical Database
     (CPU load via PDH)          (throughput model)
             │                           │
             └─────────────┬─────────────┘
                           ▼
                 ┌──────────────────┐
                 │ Adaptive          │
                 │ Scheduler         │
                 └────────┬─────────┘
                          │
             ┌────────────┼────────────┐
             ▼            ▼            ▼
           CPU          GPU       CPU + GPU
         backend      backend      partition
             │            │            │
             └────────────┼────────────┘
                          ▼
                   Result Validation
                          │
                          ▼
                    CSV Output
                          │
                          ▼
                   Feedback Model
                          │
                          └──────────► Scheduler (loop)
```

---

## File Structure

```
hetseq/
├── Types.h              # Core data structures, enums, utilities
├── Backend.h            # Abstract compute backend interface
├── CPUBackend.h         # Native C++ 2-bit K-mer hash kernel
├── D3D11Backend.h       # DirectX 11 HLSL compute shader backend
├── OpenCLBackend.h      # OpenCL kernel backend (dynamic loading)
├── OneAPIBackend.h      # Intel SYCL backend (reference, requires DPC++)
├── FASTAParser.h        # FASTA loading + 2-bit DNA packing
├── Telemetry.h          # Windows PDH CPU utilization monitoring
├── ThreadPool.h         # Persistent, priority-aware thread pool
├── Scheduler.h          # Adaptive scheduler interface
├── Scheduler.cpp        # Prediction, partitioning, dispatch logic
├── Validator.h          # P0.1–P0.3 correctness validation
├── CSVReporter.h        # Machine-readable benchmark output
├── BackendFactory.h     # Multi-GPU enumeration via DXGI
└── main.cpp             # Interactive experiment orchestrator
```

**~1,655 lines across 15 files. No external SDK dependencies.**

---

## Building

### Requirements

- Windows 10/11 (64-bit)
- Visual Studio 2015 or later with C++11 support
- DirectX 11 runtime (included with Windows)
- OpenCL runtime (Intel, NVIDIA, or AMD — included with most GPU drivers)

### Steps

1. Create a new Visual Studio **Win64 Console Application** project
2. Add all `.h` and `.cpp` files to the project
3. Set configuration to **Release x64**
4. Build
5. Run the executable

That's it. The OpenCL backend dynamically loads `OpenCL.dll` at runtime—no OpenCL SDK headers or `.lib` files needed to compile.

---

## Usage

### Quick Start

```
> hetseq.exe

===================================================================
 Dynamic Hardware-Aware Adaptive Framework (Enterprise Edition)
===================================================================

[GPU ENUMERATION] Found 2 GPU(s):
  [0] Intel(R) UHD Graphics 620 (Intel) - VRAM: 128 MB
  [1] Microsoft Basic Render Driver (Unknown) - VRAM: 0 MB

Select GPU index (or -1 for all, -2 for CPU-only): 0
[SYSTEM] Initializing GPU 0...
[SYSTEM] OpenCL Kernel initialized successfully.

[SYSTEM] Opening file dialog to select genome dataset...
[WORKLOAD] Parsing FASTA dataset: GCA_000005845.2_ASM584v2_genomic.fna
[WORKLOAD] Loaded 4641652 base pairs into memory.
[SYSTEM] Thread Pool: 8 threads
[SYSTEM] Active GPUs: 1
```

### GPU Selection

| Input | Behavior |
|:---|:---|
| `-1` | Use all available GPUs (round-robin scheduling) |
| `-2` | CPU-only mode (no GPU initialization) |
| `0`, `1`, `2`, ... | Use specific GPU by index |

### Dataset Selection

A file dialog opens automatically. Select any FASTA file (`.fasta`, `.fa`, `.fna`, `.ffn`). The parser extracts A/C/G/T characters, converts to uppercase, and packs into 2-bit format.

### Experiment Menu

```
===================================================================
 1. Run All Experiments (P0, P1, P3, P16, P17)
 2. P0  — Correctness Validation Only
 3. P1  — Statistical Benchmarking (Configurable)
 4. P3  — Scaling Experiments
 5. P16 — Ablation Experiments
 6. P17 — Adaptation Experiments (CPU Load)
 7. P8  — Adaptive Granularity
 8. P13 — Energy Efficiency Estimation
 9. P14 — Full Experimental Matrix
 10. Exit
===================================================================
```

---

## Experiment Phases

### P0 — Correctness Validation

Executed before any performance benchmarking. If any phase fails, the framework aborts and refuses to report speedup.

- **P0.1** — Exact Output Validation: 1 MB sample, byte-for-byte comparison between CPU and GPU output buffers. Reports mismatch count and first mismatch index. Requires zero mismatches.
- **P0.2** — Boundary Stress Testing: Sizes 1, 3, 31, 32, 63, 64, 65, 127, 128, 129 bp. Tests 16-base word boundaries, 64-thread group boundaries, and partial workgroups. Requires checksum equality.
- **P0.3** — Dynamic Partition Validation: Full dataset CPU reference vs 50/50 split at 16-base aligned boundary. Requires merged checksum to equal reference.

### P1 — Statistical Benchmarking

Runs 5 repetitions of Baseline 1 (CPU-only), Baseline 2 (Static Heterogeneous), and Dynamic Adaptive. Reports mean, median, standard deviation, min, and max.

### P3 — Scaling Experiments

Tests chunk sizes of 9 KB, 97 KB, 976 KB, and full-dataset size. Identifies the crossover point where GPU partitioning becomes beneficial.

### P16 — Ablation Experiments

Systematically disables framework components:
- **No Feedback** — disables online learning, retains historical data from calibration
- **No Telemetry** — disables CPU load sensing
- **No Partitioning** — disables CPU+GPU splitting, allows backend selection only

### P17 — Adaptation Experiments

- **Stage 1** — Normal run with no artificial load
- **Stage 2** — CPU saturated via background stress thread (infinite tight loop)

Verifies framework correctness under load and measures performance degradation.

### P8 — Adaptive Granularity

Tests chunk divisions of 1, 2, 4, 8, 16 to discover optimal granularity.

### P13 — Energy Efficiency Estimation

Estimates energy consumption using TDP × time for CPU-only and dynamic configurations. Reports Joules and Joules per million bases.

### P14 — Full Experimental Matrix

Runs scaling + energy across multiple chunk sizes with speedup and energy reporting.

---

## How It Works

### The Prediction Model

During calibration, the framework runs a 100 KB sample on both CPU and GPU backends. It records throughput (bytes per millisecond) for each backend and task type. Future predictions are calculated as:

```
predicted_time = size_bytes / historical_throughput
```

The model persists across experiment phases, so by the time the Dynamic suite runs, it has learned from Baseline 1 and Baseline 2.

### The Partitioning Algorithm

When the scheduler predicts that partitioning is optimal:

```
p_cpu = 1 / t_cpu_predicted
p_gpu = 1 / t_gpu_predicted
gpu_ratio = p_gpu / (p_cpu + p_gpu)

gpu_size = workload_size × gpu_ratio
cpu_size = workload_size - gpu_size

// Align to 16-base boundary to prevent 2-bit packing corruption
cpu_size = (cpu_size / 16) × 16
```

Both partitions are dispatched to the thread pool concurrently. The total execution time is the maximum of the two partitions (since they run in parallel). The merged checksum is the sum of both partition checksums.

### The Static Fast Path

For workloads below 400 KB, the scheduler bypasses all dynamic logic:

```cpp
if (work.size_bytes < STATIC_THRESHOLD) {
    return cpu->execute(work);  // No prediction, no telemetry, no partitioning
}
```

This prevents the scheduler's own overhead from exceeding the compute time on small workloads.

### The 2-Bit Encoding

```
A = 00    C = 01    G = 10    T = 11

Word[0] = base[0] | (base[1] << 2) | (base[2] << 4) | ... | (base[15] << 30)
Word[1] = base[16] | (base[17] << 2) | ...
```

16 bases per 32-bit word. 75% memory reduction vs ASCII. Partition boundaries must align to 16 bases (4 bytes) to prevent bit-packing corruption.

---

## Output

All results are written to `benchmark_results.csv` with the following columns:

```
Run_ID, Mode, Task, Input_Bytes, Total_ms, Encode_ms, H2D_ms, Compute_ms, 
D2H_ms, Scheduler_ms, Telemetry_ms, Merge_ms, Sync_ms, Predicted_CPU, 
Predicted_GPU, Predicted_Hetero, CPU_Start_ns, CPU_End_ns, GPU_Start_ns, 
GPU_End_ns, Partition_Count, CPU_Fraction, GPU_Fraction, Checksum, Correctness
```

---

## Test Data Sources

| Dataset | Source | Accession |
|:---|:---|:---|
| *E. coli* K-12 MG1655 genome | NCBI RefSeq | GCA_000005845.2 |
| *S. cerevisiae* S288C genome | NCBI RefSeq | GCA_000146045.2 |
| *D. melanogaster* proteome | UniProt | UP000000803 |
| *T. rubripes* proteome | UniProt | UP000005226 |
| *D. rerio* proteome | UniProt | UP000000437 |

Download FASTA files from NCBI or UniProt and select them via the file dialog.

---

## Limitations

- **Power consumption is estimated** using TDP, not measured directly (Intel UHD 620 does not expose real-time power draw to user-mode)
- **Ambiguous bases (N, IUPAC codes) are filtered** during parsing rather than preserved as metadata
- **Evaluated on integrated GPU only** — results may differ on discrete GPUs with dedicated VRAM and PCIe Gen4/5
- **Windows-only** — uses DXGI, PDH, and D3D11 APIs

---

## License

GNU General Public License v3.0 (GPL-3.0)

---

## Citation

```
Vithanage, D. S. (2026). Dynamic Hardware-Aware Adaptive Framework 
for Heterogeneous Genome Sequencing. Science Research Projects 
Competition, Thurstan College.
```

---

## Author

**Dinul Sasnada Vithanage**
Thurstan College
Science Research Projects Competition — September 2026

