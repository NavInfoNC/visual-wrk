/*
   Copyright (C)2018 NavInfo Co.,Ltd. All right reserved.

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <curl/curl.h>
#include <openssl/md5.h>
#include "server_perf.h"

#define PERFORMANCE_START_COLLECTING "performance/start_collecting"
#define PERFORMANCE_STOP_COLLECTING "performance/stop_collecting"

#define URL_PREFFIX "system_monitor/api/v1"

struct string
{
	char *ptr;
	size_t len;
};

static void init_string(struct string *s)
{
	s->len = 0;
	s->ptr = malloc(s->len + 1);
	if (s->ptr == NULL) {
		fprintf(stderr, "malloc() failed\n");
		exit(EXIT_FAILURE);
	}
	s->ptr[0] = '\0';
}

static size_t write_callback(void *ptr, size_t size, size_t nmemb, struct string *s)
{
	size_t new_len = s->len + size * nmemb;
	s->ptr = realloc(s->ptr, new_len + 1);
	if (s->ptr == NULL) {
		fprintf(stderr, "realloc() failed\n");
		exit(EXIT_FAILURE);
	}
	memcpy(s->ptr + s->len, ptr, size * nmemb);
	s->ptr[new_len] = '\0';
	s->len = new_len;

	return size * nmemb;
}

static int http_request(const char *url, struct string *buffer)
{
	CURLcode res;
	long response_code = -1;
	CURL *curl = curl_easy_init();
	if (curl != NULL) {
		curl_easy_setopt(curl, CURLOPT_URL, url);
		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER,false);
		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST,false);
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, buffer);
		res = curl_easy_perform(curl);
		if (res == CURLE_OK)
			curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);

		/* always cleanup */
		curl_easy_cleanup(curl);
	}

	return response_code;
}

bool start_collecting(const char *host, int duration, int interval, char *server_name, char *hash)
{
	char src_buffer[128];
	unsigned char hash_digest[MD5_DIGEST_LENGTH];
	sprintf(src_buffer, "%s;%d;%ld", host, getpid(), time(NULL));
	MD5((unsigned char*)src_buffer, strlen(src_buffer), hash_digest);

	for(int i = 0; i < MD5_DIGEST_LENGTH; i++) {
		sprintf(hash + i*2, "%02x", hash_digest[i]);
	}

	char url[1024];
	sprintf(url, "http://%s/%s/%s?hash=%s&duration=%d&interval=%d", host, URL_PREFFIX, PERFORMANCE_START_COLLECTING, hash, duration, interval);
	fprintf(stderr, "start: %s\n", url);
	if (server_name != NULL) {
		strcat(url, "&server=");
		strcat(url, server_name);
	}
	fprintf(stderr, "server performance start collecting\n");

	struct string buffer;
	init_string(&buffer);
	int response_code = http_request(url, &buffer);
	if (response_code != 200) {
		free(buffer.ptr);
		fprintf(stderr, "server performance start collecting response_code: %d, url: %s\n", response_code, url);
		return false;
	}

	int result = false;
	json_error_t error;
	json_t* response_json = json_loadb(buffer.ptr, buffer.len, JSON_ENCODE_ANY, &error);
	const char* response_result = json_string_value(json_object_get(response_json, "result"));
	if (response_result != NULL && strcmp("succeeded", response_result) == 0)
		result = true;

	json_decref(response_json);
	free(buffer.ptr);
	return result;
}

json_t *stop_collecting(const char *host, char *hash)
{
	char url[256];

	sprintf(url, "http://%s/%s/%s?hash=%s", host, URL_PREFFIX, PERFORMANCE_STOP_COLLECTING, hash);
	fprintf(stderr, "server performance stop collecting\n");
	fprintf(stderr, "stop: %s\n", url);

	struct string buffer;
	init_string(&buffer);
	int response_code = http_request(url, &buffer);
	if (response_code != 200 || buffer.len == 0) {
		free(buffer.ptr);
		fprintf(stderr, "server performance stop collecting response_code: %d, url: %s\n", response_code, url);
		return NULL;
	}

	json_error_t error;
	json_t *response_json = json_loadb(buffer.ptr, buffer.len, JSON_ENCODE_ANY, &error);
	free(buffer.ptr);

	const char *response_result = json_string_value(json_object_get(response_json, "result"));
	if (response_result == NULL || strcmp("succeeded", response_result) != 0)
		return NULL;

	return response_json;
}

static void parse_json_array(json_t *json_array, Array *array)
{
	array->count = json_array_size(json_array);
	if (array->count <= 0)
		return;

	array->array = (double *)malloc(sizeof(double) * array->count + 1);
	for (int i = 0; i < array->count; i++)
		array->array[i] = json_number_value(json_array_get(json_array, i));
}

CpuPerformance *get_cpu_performance(json_t *buffer)
{
	json_t *cpu_json = json_object_get(buffer, "cpu");
	if (cpu_json == NULL)
		return NULL;

	CpuPerformance *cpu_performance = (CpuPerformance *)malloc(sizeof(CpuPerformance));
	memset(cpu_performance, 0, sizeof(CpuPerformance));

	json_t *percent_json = json_object_get(cpu_json, "percent");
	if (percent_json != NULL)
		parse_json_array(percent_json, &cpu_performance->percent);
	
	cpu_performance->architecture = json_string_value(json_object_get(cpu_json, "architecture"));
	cpu_performance->model = json_string_value(json_object_get(cpu_json, "model"));
	cpu_performance->MHz = json_string_value(json_object_get(cpu_json, "MHz"));
	cpu_performance->coreNum = (int)json_integer_value(json_object_get(cpu_json, "coreNum"));

	json_t *cores_percent_json = json_object_get(cpu_json, "corePercent");
	if (cores_percent_json != NULL) {
		cpu_performance->core_percent.count = json_array_size(cores_percent_json);
		cpu_performance->core_percent.array = (double*)malloc(sizeof(double) * cpu_performance->coreNum * cpu_performance->core_percent.count + 1);
		for (int i = 0; i < cpu_performance->core_percent.count; i++) {
			json_t *core_percent_json = json_array_get(cores_percent_json, i);
			for (int j = 0; j < cpu_performance->coreNum; j++) {
				int index = i * cpu_performance->coreNum + j;
				json_t *percent_json = json_array_get(core_percent_json, j);
				int percent = percent_json == NULL ? 0 : json_number_value(percent_json);
				cpu_performance->core_percent.array[index] = percent;
			}
		}
	}

	return cpu_performance;
}

MemPerformance *get_mem_performance(json_t *buffer)
{
	json_t *mem_json = json_object_get(buffer, "memory");
	if (mem_json == NULL)
		return NULL;

	MemPerformance *mem_performance = (MemPerformance*)malloc(sizeof(MemPerformance));
	memset(mem_performance, 0, sizeof(MemPerformance));

	mem_performance->total = (int)json_integer_value(json_object_get(mem_json, "total"));

	json_t *percent_json = json_object_get(mem_json, "percent");
	if (percent_json != NULL)
		parse_json_array(percent_json, &mem_performance->percent);

	json_t *used_json = json_object_get(mem_json, "used");
	if (used_json != NULL)
		parse_json_array(used_json, &mem_performance->used);

	json_t *free_json = json_object_get(mem_json, "free");
	if (free_json != NULL)
		parse_json_array(free_json, &mem_performance->free);

	return mem_performance;
}

IoPerformance *get_io_performance(json_t *buffer)
{
	json_t *io_json = json_object_get(buffer, "io");
	if (io_json == NULL)
		return NULL;

	IoPerformance *io_performance = (IoPerformance *)malloc(sizeof(IoPerformance));
	memset(io_performance, 0, sizeof(IoPerformance));

	json_t *read_size_json = json_object_get(io_json, "readSize");
	if (read_size_json != NULL)
		parse_json_array(read_size_json, &io_performance->read_size);

	json_t *write_size_json = json_object_get(io_json, "writeSize");
	if (write_size_json != NULL)
		parse_json_array(write_size_json, &io_performance->write_size);

	json_t *read_count_json = json_object_get(io_json, "readCount");
	if (read_count_json != NULL)
		parse_json_array(read_count_json, &io_performance->read_count);

	json_t *write_count_json = json_object_get(io_json, "writeCount");
	if (write_count_json != NULL)
		parse_json_array(write_count_json, &io_performance->write_count);

	return io_performance;
}

PlatformInfo* get_platform_info(json_t* buffer)
{
	json_t *platform_json = json_object_get(buffer, "platform");
	if (platform_json == NULL)
		return NULL;

	PlatformInfo *platform_info = (PlatformInfo *)malloc(sizeof(PlatformInfo));
	memset(platform_info, 0, sizeof(PlatformInfo));

	platform_info->release = json_string_value(json_object_get(platform_json, "release"));
	platform_info->distribution = json_string_value(json_object_get(platform_json, "distribution"));
	platform_info->version = json_string_value(json_object_get(platform_json, "version"));
	platform_info->system = json_string_value(json_object_get(platform_json, "system"));
	platform_info->hostname = json_string_value(json_object_get(platform_json, "hostname"));

	return platform_info;
}

DiskInfo **get_disk_info(json_t* buffer, int *disk_num)
{
	json_t *disks_json = json_object_get(buffer, "disk");
	if (disks_json == NULL)
		return NULL;

	*disk_num = json_array_size(disks_json);
	DiskInfo **disk_info = (DiskInfo **)malloc(sizeof(DiskInfo *) * (*disk_num));
	for (int i = 0; i < *disk_num; i++) {
		json_t *disk_json = json_array_get(disks_json, i);
		if (disk_json == NULL)
			continue;

		disk_info[i] = (DiskInfo *)malloc(sizeof(DiskInfo));
		disk_info[i]->device = json_string_value(json_object_get(disk_json, "device"));
		disk_info[i]->mount_point = json_string_value(json_object_get(disk_json, "mountPoint"));
		disk_info[i]->total = json_number_value(json_object_get(disk_json, "total"));
		disk_info[i]->percent = json_number_value(json_object_get(disk_json, "percent"));
	}

	return disk_info;
}

void release_cpu_performance(CpuPerformance *o)
{
	if (o != NULL) {
		free(o->core_percent.array);
		free(o->percent.array);
		free(o);
	}
}

void release_io_performance(IoPerformance *o)
{
	if (o != NULL) {
		free(o->read_count.array);
		free(o->read_size.array);
		free(o->write_count.array);
		free(o->write_size.array);
		free(o);
	}
}

void release_mem_performance(MemPerformance *o)
{
	if (o != NULL) {
		free(o->free.array);
		free(o->used.array);
		free(o->percent.array);
		free(o);
	}
}
void init_platform_info(PlatformInfo *o)
{
	o->distribution = NULL;
	o->version = NULL;
	o->release = NULL;
	o->hostname = NULL;
	o->system = NULL;
}

void release_platform_info(PlatformInfo *o)
{
	if (o != NULL)
		free(o);
}

void release_disk_info(DiskInfo **o, int disk_num)
{
	if (o != NULL) {
		for (int i = 0; i < disk_num; i++) {
			if (o[i] != NULL)
				free(o[i]);
		}
		free(o);
	}
}

