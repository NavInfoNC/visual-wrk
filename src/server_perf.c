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
	if (s->ptr == NULL)
	{
		fprintf(stderr, "malloc() failed\n");
		exit(EXIT_FAILURE);
	}
	s->ptr[0] = '\0';
}

static size_t writefunc(void *ptr, size_t size, size_t nmemb, struct string *s)
{
	size_t new_len = s->len + size * nmemb;
	s->ptr = realloc(s->ptr, new_len + 1);
	if (s->ptr == NULL)
	{
		fprintf(stderr, "realloc() failed\n");
		exit(EXIT_FAILURE);
	}
	memcpy(s->ptr + s->len, ptr, size * nmemb);
	s->ptr[new_len] = '\0';
	s->len = new_len;

	return size * nmemb;
}

static int httpRequest(const char* url, struct string* buffer)
{
	CURLcode res;
	long response_code = -1;
	CURL* curl = curl_easy_init();
	if (curl != NULL)
	{
		curl_easy_setopt(curl, CURLOPT_URL, url);
		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER,false);
		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST,false);
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, buffer);
		res = curl_easy_perform(curl);
		if (res == CURLE_OK)
		{
			curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
		}

		/* always cleanup */
		curl_easy_cleanup(curl);
	}

	return response_code;
}

bool startCollecting(const char* host, int duration, int interval, char* serverName, char* hash)
{
	char srcBuffer[128];
	unsigned char hashDigest[MD5_DIGEST_LENGTH];
	sprintf(srcBuffer, "%s;%d;%ld", host, getpid(), time(NULL));
	MD5((unsigned char*)srcBuffer, strlen(srcBuffer), hashDigest);

	for(int i = 0; i < MD5_DIGEST_LENGTH; i++) {
		sprintf(hash + i*2, "%02x", hashDigest[i]);
	}

	char url[1024];
	sprintf(url, "http://%s/%s/%s?hash=%s&duration=%d&interval=%d", host, URL_PREFFIX, PERFORMANCE_START_COLLECTING, hash, duration, interval);
	if (serverName != NULL) {
		strcat(url, "&server=");
		strcat(url, serverName);
	}
	fprintf(stderr, "server performance start collecting url:%s\n", url);

	struct string buffer;
	init_string(&buffer);
	int responseCode = httpRequest(url, &buffer);
	if (responseCode != 200)
	{
		free(buffer.ptr);
		fprintf(stderr, "server performance start collecting responseCode:%d\n", responseCode);
		return false;
	}

	int result = false;
	json_error_t error;
	json_t* responseJson = json_loadb(buffer.ptr, buffer.len, JSON_ENCODE_ANY, &error);
	const char* responseResult = json_string_value_of_name(responseJson, "result");
	if (responseResult != NULL && strcmp("succeeded", responseResult) == 0)
		result = true;

	json_decref(responseJson);
	free(buffer.ptr);
	return result;
}

json_t* stopCollecting(const char* host, char* hash)
{
	char url[256];

	sprintf(url, "http://%s/%s/%s?hash=%s", host, URL_PREFFIX, PERFORMANCE_STOP_COLLECTING, hash);
	fprintf(stderr, "server performance stop collecting url:%s\n", url);

	struct string buffer;
	init_string(&buffer);
	int responseCode = httpRequest(url, &buffer);
	if (responseCode != 200 || buffer.len == 0)
	{
		free(buffer.ptr);
		fprintf(stderr, "server performance stop collecting responseCode:%d\n", responseCode);
		return NULL;
	}

	json_error_t error;
	json_t* responseJson = json_loadb(buffer.ptr, buffer.len, JSON_ENCODE_ANY, &error);
	free(buffer.ptr);

	const char* responseResult = json_string_value_of_name(responseJson, "result");
	if (strcmp("succeeded", responseResult) != 0)
		return NULL;

	return responseJson;
}

static void parseJsonArray(json_t* jsonArray, Array* array)
{
	array->count = json_array_size(jsonArray);
	if (array->count <= 0)
		return;

	array->array = (double*)malloc(sizeof(double) * array->count + 1);
	for (int i = 0; i < array->count; i++)
	{
		array->array[i] = json_number_value(json_array_get(jsonArray, i));
	}
}

CpuPerformance* getCpuPerformance(json_t* buffer)
{
	json_t* cpuJson = json_object_get(buffer, "cpu");
	if (cpuJson == NULL)
		return NULL;

	CpuPerformance* cpuPerformance = (CpuPerformance*)malloc(sizeof(CpuPerformance));
	memset(cpuPerformance, 0, sizeof(CpuPerformance));

	json_t* percentJson = json_object_get(cpuJson, "percent");
	if (percentJson != NULL)
		parseJsonArray(percentJson, &cpuPerformance->percent);
	
	cpuPerformance->architecture = json_string_value_of_name(cpuJson, "architecture");
	cpuPerformance->model = json_string_value_of_name(cpuJson, "model");
	cpuPerformance->MHz = json_string_value_of_name(cpuJson, "MHz");
	cpuPerformance->coreNum = (int)json_integer_value_of_name(cpuJson, "coreNum");

	json_t* coresPercentJson = json_object_get(cpuJson, "corePercent");
	if (coresPercentJson != NULL)
	{
		cpuPerformance->corePercent.count = json_array_size(coresPercentJson);
		cpuPerformance->corePercent.array = (double*)malloc(sizeof(double) * cpuPerformance->coreNum * cpuPerformance->corePercent.count + 1);
		for (int i = 0; i < cpuPerformance->corePercent.count; i++)
		{
			json_t* corePercentJson = json_array_get(coresPercentJson, i);
			for (int j = 0; j < cpuPerformance->coreNum; j++)
			{
				int index = i * cpuPerformance->coreNum + j;
				json_t* percentJson = json_array_get(corePercentJson, j);
				int percent = percentJson == NULL ? 0 : json_number_value(percentJson);
				cpuPerformance->corePercent.array[index] = percent;
			}
		}
	}

	return cpuPerformance;
}

MemPerformance* getMemPerformance(json_t* buffer)
{
	json_t* memJson = json_object_get(buffer, "memory");
	if (memJson == NULL)
		return NULL;

	MemPerformance* memPerformance = (MemPerformance*)malloc(sizeof(MemPerformance));
	memset(memPerformance, 0, sizeof(MemPerformance));

	memPerformance->total = (int)json_integer_value_of_name(memJson, "total");

	json_t* percentJson = json_object_get(memJson, "percent");
	if (percentJson != NULL)
		parseJsonArray(percentJson, &memPerformance->percent);

	json_t* usedJson = json_object_get(memJson, "used");
	if (usedJson != NULL)
		parseJsonArray(usedJson, &memPerformance->used);

	json_t* freeJson = json_object_get(memJson, "free");
	if (freeJson != NULL)
		parseJsonArray(freeJson, &memPerformance->free);

	return memPerformance;
}

IoPerformance* getIoPerformance(json_t* buffer)
{
	json_t* ioJson = json_object_get(buffer, "io");
	if (ioJson == NULL)
		return NULL;

	IoPerformance* ioPerformance = (IoPerformance*)malloc(sizeof(IoPerformance));
	memset(ioPerformance, 0, sizeof(IoPerformance));

	json_t* readSizeJson = json_object_get(ioJson, "readSize");
	if (readSizeJson != NULL)
		parseJsonArray(readSizeJson, &ioPerformance->readSize);

	json_t* writeSizeJson = json_object_get(ioJson, "writeSize");
	if (writeSizeJson != NULL)
		parseJsonArray(writeSizeJson, &ioPerformance->writeSize);

	json_t* readCountJson = json_object_get(ioJson, "readCount");
	if (readCountJson != NULL)
		parseJsonArray(readCountJson, &ioPerformance->readCount);

	json_t* writeCountJson = json_object_get(ioJson, "writeCount");
	if (writeCountJson != NULL)
		parseJsonArray(writeCountJson, &ioPerformance->writeCount);

	return ioPerformance;
}

PlatformInfo* getPlatformInfo(json_t* buffer)
{
	json_t* platformJson = json_object_get(buffer, "platform");
	if (platformJson == NULL)
		return NULL;

	PlatformInfo* platformInfo = (PlatformInfo*)malloc(sizeof(PlatformInfo));
	memset(platformInfo, 0, sizeof(PlatformInfo));

	platformInfo->release = json_string_value_of_name(platformJson, "release");
	platformInfo->distribution = json_string_value_of_name(platformJson, "distribution");
	platformInfo->version = json_string_value_of_name(platformJson, "version");
	platformInfo->system = json_string_value_of_name(platformJson, "system");
	platformInfo->hostname = json_string_value_of_name(platformJson, "hostname");

	return platformInfo;
}

DiskInfo** getDiskInfo(json_t* buffer, int* diskNum)
{
	json_t* disksJson = json_object_get(buffer, "disk");
	if (disksJson == NULL)
		return NULL;

	*diskNum = json_array_size(disksJson);
	DiskInfo** diskInfo = (DiskInfo**)malloc(sizeof(DiskInfo*) * (*diskNum));
	for (int i = 0; i < *diskNum; i++)
	{
		json_t* diskJson = json_array_get(disksJson, i);
		if (diskJson == NULL)
			continue;

		diskInfo[i] = (DiskInfo*)malloc(sizeof(DiskInfo));
		diskInfo[i]->device = json_string_value_of_name(diskJson, "device");
		diskInfo[i]->mountPoint = json_string_value_of_name(diskJson, "mountPoint");
		diskInfo[i]->total = json_number_value_of_name(diskJson, "total");
		diskInfo[i]->percent = json_number_value_of_name(diskJson, "percent");
	}

	return diskInfo;
}

void releaseCpuPerformance(CpuPerformance* o)
{
	if (o != NULL)
	{
		free(o->corePercent.array);
		free(o->percent.array);
		free(o);
	}
}

void releaseIoPerformance(IoPerformance* o)
{
	if (o != NULL)
	{
		free(o->readCount.array);
		free(o->readSize.array);
		free(o->writeCount.array);
		free(o->writeSize.array);
		free(o);
	}
}

void releaseMemPerformance(MemPerformance* o)
{
	if (o != NULL)
	{
		free(o->free.array);
		free(o->used.array);
		free(o->percent.array);
		free(o);
	}
}
void initPlatformInfo(PlatformInfo* o)
{
	o->distribution = NULL;
	o->version = NULL;
	o->release = NULL;
	o->hostname = NULL;
	o->system = NULL;
}

void releasePlatformInfo(PlatformInfo* o)
{
	if (o != NULL)
	{
		free(o);
	}
}

void releaseDiskInfo(DiskInfo** o, int diskNum)
{
	if (o != NULL)
	{
		for (int i = 0; i < diskNum; i++)
		{
			if (o[i] != NULL)
				free(o[i]);
		}
		free(o);
	}
}

