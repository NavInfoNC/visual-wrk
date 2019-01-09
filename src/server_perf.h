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
	const char* architecture;
	const char* model;
	const char* MHz;
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

typedef struct DiskInfo
{
	double total;
	double percent;
	const char* mountPoint;
	const char* device;;
} DiskInfo;

typedef struct PlatformInfo
{
	const char* version;
	const char* hostname;
	const char* system;
	const char* release;
	const char* distribution;
} PlatformInfo;

bool startCollecting(const char* host, int duration, int interval, char* serverName, char* hash);
json_t* stopCollecting(const char* host, char* hash);

CpuPerformance* getCpuPerformance(json_t* buffer);
void releaseCpuPerformance(CpuPerformance* o);

MemPerformance* getMemPerformance(json_t* buffer);
void releaseIoPerformance(IoPerformance* o);

IoPerformance* getIoPerformance(json_t* buffer);
void releaseMemPerformance(MemPerformance* o);

PlatformInfo* getPlatformInfo(json_t* buffer);
void releasePlatformInfo(PlatformInfo* o);

DiskInfo** getDiskInfo(json_t* buffer, int* diskNum);
void releaseDiskInfo(DiskInfo** o, int diskNum);
#endif /* SERVER_PERF_H */
