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

#define URL_PREFFIX "system_monitor"

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

	char url[128];
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
	if (strcmp("succeeded", responseResult) == 0)
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

bool getCpuPerformance(json_t* buffer, CpuPerformance* cpuPerformance)
{
	json_t* cpuJson = json_object_get(buffer, "cpu");
	if (cpuJson == NULL)
		return false;

	json_t* percentJson = json_object_get(cpuJson, "percent");
	if (percentJson != NULL)
		parseJsonArray(percentJson, &cpuPerformance->percent);
	
	json_t* coreNum = json_object_get(cpuJson, "coreNum");
	if (coreNum != NULL)
		cpuPerformance->coreNum = (int)json_integer_value(coreNum);

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

	return true;
}

bool getMemPerformance(json_t* buffer, MemPerformance* memPerformance)
{
	json_t* memJson = json_object_get(buffer, "memory");
	if (memJson == NULL)
		return false;

	json_t* totalJson = json_object_get(memJson, "total");
	if (totalJson != NULL)
		memPerformance->total = (int)json_integer_value(totalJson);

	json_t* percentJson = json_object_get(memJson, "percent");
	if (percentJson != NULL)
		parseJsonArray(percentJson, &memPerformance->percent);

	json_t* usedJson = json_object_get(memJson, "used");
	if (usedJson != NULL)
		parseJsonArray(usedJson, &memPerformance->used);

	json_t* freeJson = json_object_get(memJson, "free");
	if (freeJson != NULL)
		parseJsonArray(freeJson, &memPerformance->free);

	return true;
}

bool getIoPerformance(json_t* buffer, IoPerformance* ioPerformance)
{
	json_t* ioJson = json_object_get(buffer, "io");
	if (ioJson == NULL)
		return false;

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

	return true;
}

void initCpuPerformance(CpuPerformance* o)
{
	o->coreNum = 0;
	o->corePercent.count = 0;
	o->corePercent.array = NULL;
	o->percent.count = 0;
	o->percent.array = NULL;
}

void releaseCpuPerformance(CpuPerformance* o)
{
	free(o->corePercent.array);
	free(o->percent.array);
}

void initIoPerformance(IoPerformance* o)
{
	o->readCount.count = 0;
	o->readSize.count = 0;
	o->writeCount.count = 0;
	o->writeSize.count = 0;
	o->readCount.array = NULL;
	o->readSize.array = NULL;
	o->writeCount.array = NULL;
	o->writeSize.array = NULL;
}

void releaseIoPerformance(IoPerformance* o)
{
	free(o->readCount.array);
	free(o->readSize.array);
	free(o->writeCount.array);
	free(o->writeSize.array);
}

void initMemPerformance(MemPerformance* o)
{
	o->total = 0;
	o->free.array = NULL;
	o->free.count = 0;
	o->used.array = NULL;
	o->used.count = 0;
	o->percent.array = NULL;
	o->percent.count = 0;
}

void releaseMemPerformance(MemPerformance* o)
{
	free(o->free.array);
	free(o->used.array);
	free(o->percent.array);
}
//int main(void)
//{
//	int duration = 5;
//	unsigned char hash[MD5_DIGEST_LENGTH];
//	bool result = startCollecting(URL_PREFFIX, duration, 1, "java", hash);
//	if (result)
//	{
//		sleep(duration);
//
//		json_t* buffer = stopCollecting(URL_PREFFIX, hash);
//		if (buffer == NULL)
//		{
//			printf("stopCollecting failed\n");
//			return -1;
//		}
//
//		CpuPerformance cpuPerformance;
//		initCpuPerformance(&cpuPerformance);
//		if (getCpuPerformance(buffer, &cpuPerformance))
//		{
//			printf("core num:%d\n", cpuPerformance.coreNum);
//			for (int i = 0; i < cpuPerformance.percent.count; i++)
//			{
//				double core1 = cpuPerformance.corePercent.array[i * 4];
//				double core2 = cpuPerformance.corePercent.array[i * 4 + 1];
//				double core3 = cpuPerformance.corePercent.array[i * 4 + 2];
//				double core4 = cpuPerformance.corePercent.array[i * 4 + 3];
//				printf("total:%f,core1:%f,core2:%f,core3:%f,core4:%f\n", cpuPerformance.percent.array[i], 
//						core1, core2, core3, core4);
//			}
//		}
//
//		IoPerformance ioPerformance;
//		initIoPerformance(&ioPerformance);
//		if (getIoPerformance(buffer, &ioPerformance))
//		{
//			for (int i = 0; i < ioPerformance.readSize.count; i++)
//			{
//				printf("readSize:%f, writeSize:%f, readCount:%f, writeCount:%f\n", ioPerformance.readSize.array[i], 
//						ioPerformance.writeSize.array[i], ioPerformance.readCount.array[i], ioPerformance.writeCount.array[i]);
//			}
//		}
//
//		MemPerformance memPerformance;
//		initMemPerformance(&memPerformance);
//		if (getMemPerformance(buffer, &memPerformance))
//		{
//			printf("total memory:%d\n", memPerformance.total);
//			for (int i = 0; i < memPerformance.percent.count; i++)
//			{
//				printf("used:%f, free:%f, percent:%f\n", memPerformance.used.array[i], memPerformance.free.array[i], memPerformance.percent.array[i]);
//			}
//		}
//
//		json_decref(buffer);
//	}
//	else
//		printf("startCollecting failed!\n");
//
//	return 0;
//}
