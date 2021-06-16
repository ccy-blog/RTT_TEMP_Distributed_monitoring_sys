#ifndef __DS18B20_H__
#define __DS18B20_H__

#include <rthw.h>
#include <rtthread.h>
#include <rtdevice.h>
#include "sensor.h"

#define CONNECT_SUCCESS 0	
#define CONNECT_FAILED  1
#define CONNECT_TIMEOUT 1


int rt_hw_ds18b20_init(const char *name, struct rt_sensor_config *cfg);

#endif
