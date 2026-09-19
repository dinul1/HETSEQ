#pragma once
#include <windows.h>
#include <pdh.h>
#include <pdhmsg.h>

#pragma comment(lib, "pdh.lib")

class Telemetry {
private:
	PDH_HQUERY cpuQuery = NULL, gpuQuery = NULL;
	PDH_HCOUNTER cpuTotal = NULL, gpuEngines = NULL;

public:
	bool init() {
		PDH_STATUS pdhStatus = PdhOpenQueryA(NULL, 0, &cpuQuery);
		if (pdhStatus != ERROR_SUCCESS) { cpuQuery = NULL; return false; }
		pdhStatus = PdhAddEnglishCounterA(cpuQuery, "\\Processor(_Total)\\% Processor Time", 0, &cpuTotal);
		if (pdhStatus != ERROR_SUCCESS) pdhStatus = PdhAddCounterA(cpuQuery, "\\Processor(_Total)\\% Processor Time", 0, &cpuTotal);
		if (pdhStatus != ERROR_SUCCESS) { PdhCloseQuery(cpuQuery); cpuQuery = NULL; return false; }
		PdhCollectQueryData(cpuQuery);

		pdhStatus = PdhOpenQueryA(NULL, 0, &gpuQuery);
		if (pdhStatus != ERROR_SUCCESS) { gpuQuery = NULL; return false; }
		pdhStatus = PdhAddEnglishCounterA(gpuQuery, "\\GPU Engine(*)\\Utilization Percentage", 0, &gpuEngines);
		if (pdhStatus != ERROR_SUCCESS) pdhStatus = PdhAddCounterA(gpuQuery, "\\GPU Engine(*)\\Utilization Percentage", 0, &gpuEngines);
		if (pdhStatus != ERROR_SUCCESS) { PdhCloseQuery(gpuQuery); gpuQuery = NULL; return false; }
		PdhCollectQueryData(gpuQuery);
		return true;
	}

	double get_cpu_load() {
		if (!cpuQuery) return -1.0; PDH_FMT_COUNTERVALUE counterVal;
		PDH_STATUS status = PdhCollectQueryData(cpuQuery);
		if (status != ERROR_SUCCESS && status != PDH_NO_DATA) return -1.0;
		status = PdhGetFormattedCounterValue(cpuTotal, PDH_FMT_DOUBLE, NULL, &counterVal);
		return (status == ERROR_SUCCESS) ? counterVal.doubleValue : -1.0;
	}

	void shutdown() {
		if (cpuQuery) PdhCloseQuery(cpuQuery);
		if (gpuQuery) PdhCloseQuery(gpuQuery);
	}
};