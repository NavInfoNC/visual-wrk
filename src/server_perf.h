#ifndef SERVER_PERF_H
#define SERVER_PERF_H

#include "jansson/jansson.h"

typedef enum PerformanceType
{
	PerformanceType_cpu,
	PerformanceType_memory,
	PerformanceType_io,
	PerformanceType_maxNum
} PerformanceType;

typedef struct Array
{
	double* array;
	size_t count;
} Array;

typedef struct CpuPerformance
{
	Array percent;
	Array corePercent;
	int coreNum;
} CpuPerformance;

typedef struct MemPerformance
{
	int total;
	Array percent;
	Array free;
	Array used;
} MemPerformance;

typedef struct IoPerformance
{
	Array readSize;
	Array readCount;
	Array writeSize;
	Array writeCount;
} IoPerformance;

bool startCollecting(const char* host, int duration, int interval, char* serverName, char* hash);
json_t* stopCollecting(const char* host, char* hash);

void initCpuPerformance(CpuPerformance* o);
bool getCpuPerformance(json_t* buffer, CpuPerformance* cpuPerformance);
void releaseCpuPerformance(CpuPerformance* o);

void initIoPerformance(IoPerformance* o);
bool getMemPerformance(json_t* buffer, MemPerformance* memPerformance);
void releaseIoPerformance(IoPerformance* o);

void initMemPerformance(MemPerformance* o);
bool getIoPerformance(json_t* buffer, IoPerformance* ioPerformance);
void releaseMemPerformance(MemPerformance* o);
#endif /* SERVER_PERF_H */
