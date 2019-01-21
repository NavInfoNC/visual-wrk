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
	double *array;
	size_t count;
} Array;

typedef struct CpuPerformance
{
	Array percent;
	Array core_percent;
	int coreNum;
	const char *architecture;
	const char *model;
	const char *MHz;
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
	Array read_size;
	Array read_count;
	Array write_size;
	Array write_count;
} IoPerformance;

typedef struct DiskInfo
{
	double total;
	double percent;
	const char *mount_point;
	const char *device;;
} DiskInfo;

typedef struct PlatformInfo
{
	const char *version;
	const char *hostname;
	const char *system;
	const char *release;
	const char *distribution;
} PlatformInfo;

bool start_collecting(const char *host, int duration, int interval, char *server_name, char *hash);
json_t* stop_collecting(const char *host, char *hash);

CpuPerformance* get_cpu_performance(json_t *buffer);
void release_cpu_performance(CpuPerformance *o);

MemPerformance* get_mem_performance(json_t *buffer);
void release_io_performance(IoPerformance *o);

IoPerformance* get_io_performance(json_t *buffer);
void release_mem_performance(MemPerformance *o);

PlatformInfo* get_platform_info(json_t *buffer);
void release_platform_info(PlatformInfo *o);

DiskInfo** get_disk_info(json_t *buffer, int *disk_num);
void release_disk_info(DiskInfo **o, int disk_num);
#endif /* SERVER_PERF_H */
