#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include "server_perf.h"

int main(void)
{
	int duration = 5;
	char hash[64];
	bool result = startCollecting("192.168.146.61", duration, 1, NULL, hash);
	if (result)
	{
		sleep(duration);

		json_t* buffer = stopCollecting("192.168.146.61", hash);
		if (buffer == NULL)
		{
			printf("stopCollecting failed\n");
			return -1;
		}

		CpuPerformance* cpuPerformance = getCpuPerformance(buffer);
		if (cpuPerformance != NULL)
		{
			printf("model:%s\n", cpuPerformance->model);
			printf("architecture:%s\n", cpuPerformance->architecture);
			printf("MHz:%sMHz\n", cpuPerformance->MHz);
			printf("core num:%d\n", cpuPerformance->coreNum);
			for (int i = 0; i < cpuPerformance->percent.count; i++)
			{
				double core1 = cpuPerformance->corePercent.array[i * 4];
				double core2 = cpuPerformance->corePercent.array[i * 4 + 1];
				double core3 = cpuPerformance->corePercent.array[i * 4 + 2];
				double core4 = cpuPerformance->corePercent.array[i * 4 + 3];
				printf("total:%f,core1:%f,core2:%f,core3:%f,core4:%f\n", cpuPerformance->percent.array[i], 
						core1, core2, core3, core4);
			}
			releaseCpuPerformance(cpuPerformance);
		}

		IoPerformance* ioPerformance = getIoPerformance(buffer);
		if (ioPerformance != NULL)
		{
			for (int i = 0; i < ioPerformance->readSize.count; i++)
			{
				printf("readSize:%f, writeSize:%f, readCount:%f, writeCount:%f\n", ioPerformance->readSize.array[i], 
						ioPerformance->writeSize.array[i], ioPerformance->readCount.array[i], ioPerformance->writeCount.array[i]);
			}
			releaseIoPerformance(ioPerformance);
		}

		MemPerformance* memPerformance = getMemPerformance(buffer);
		if (memPerformance != NULL)
		{
			printf("total memory:%d\n", memPerformance->total);
			for (int i = 0; i < memPerformance->percent.count; i++)
			{
				printf("used:%f, free:%f, percent:%f\n", memPerformance->used.array[i], memPerformance->free.array[i], memPerformance->percent.array[i]);
			}
			releaseMemPerformance(memPerformance);
		}

		PlatformInfo* platformInfo = getPlatformInfo(buffer);
		if (platformInfo != NULL)
		{
			printf("hostname:%s\n", platformInfo->hostname);
			printf("system:%s\n", platformInfo->system);
			printf("release:%s\n", platformInfo->release);
			printf("version:%s\n", platformInfo->version);
			printf("distribution:%s\n", platformInfo->distribution);
			releasePlatformInfo(platformInfo);
		}

		int diskNum = 0;
		DiskInfo** diskInfo = getDiskInfo(buffer, &diskNum);
		if (diskInfo != NULL)
		{
			for (int i = 0; i < diskNum; i++)
			{
				printf("total:%f\n", diskInfo[i]->total);
				printf("percent:%f\n", diskInfo[i]->percent);
				printf("device:%s\n", diskInfo[i]->device);
				printf("mountPoint:%s\n", diskInfo[i]->mountPoint);
			}
			releaseDiskInfo(diskInfo, diskNum);
		}

		json_decref(buffer);
	}
	else
		printf("startCollecting failed!\n");

	return 0;
}
