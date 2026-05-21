// Copyright (c) Acconeer AB, 2022-2025
// All rights reserved
// This file is subject to the terms and conditions defined in the file
// 'LICENSES/license_acconeer.txt', (BSD 3-Clause License) which is part
// of this source code package.

#include "acc_integration.h"
#include "freertos/FreeRTOS.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

extern void ets_delay_us(uint32_t);

extern int64_t esp_timer_get_time(void);

void acc_integration_sleep_us(uint32_t time_usec)
{
	ets_delay_us(time_usec);
}

void acc_integration_sleep_ms(uint32_t time_msec)
{
	ets_delay_us(time_msec * 1000);
}

uint32_t acc_integration_get_time(void)
{
	return esp_timer_get_time() / 1000;
}

void *acc_integration_mem_alloc(size_t size)
{
	return pvPortMalloc(size);
}

void *acc_integration_mem_calloc(size_t nmemb, size_t size)
{
	void *memory = pvPortMalloc(nmemb * size);

	if (memory != NULL)
	{
		memset(memory, 0, nmemb * size);
	}

	return memory;
}

void acc_integration_mem_free(void *ptr)
{
	vPortFree(ptr);
}
