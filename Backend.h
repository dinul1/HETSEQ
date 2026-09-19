#pragma once
#include "Types.h"

class ComputeBackend {
public:
	virtual ~ComputeBackend() = default;
	virtual RuntimeType get_type() = 0;
	virtual const char* get_name() = 0;
	virtual ExecutionMetrics execute(const Workload& work) = 0;

	virtual void shutdown() {}
};