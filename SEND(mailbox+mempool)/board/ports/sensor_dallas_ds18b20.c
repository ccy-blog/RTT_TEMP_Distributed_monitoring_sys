
#include "sensor_dallas_ds18b20.h"
#include "sensor.h"
#include <rtdbg.h>
#include "board.h"


#define SENSOR_TEMP_RANGE_MAX (125)
#define SENSOR_TEMP_RANGE_MIN (-55)

/* 高精度(微妙)延时, 通过硬件实现 */
/* @RT_WEAK 弱符号：https://itexp.blog.csdn.net/article/details/106816700 */
RT_WEAK void rt_hw_us_delay(rt_uint32_t us){

	rt_uint32_t delta;
	
	us = us * (SysTick->LOAD / (1000000 / RT_TICK_PER_SECOND));
	delta = SysTick->VAL;
	
	while(delta - SysTick->VAL < us){
	
		continue;
	}
}

static void ds18b20_reset(rt_base_t pin){
	
	/* 设置为输出模式 */
	rt_pin_mode(pin, PIN_MODE_OUTPUT);
	
	/* 1. 数据线拉到低电平 */
	rt_pin_write(pin, PIN_LOW);
	
	/* 2. 延时 480 ~ 960 微妙 */
	rt_hw_us_delay(780);
	
	/* 3. 数据线拉到高电平 */
	rt_pin_write(pin, PIN_HIGH);
	
	/* 4. 延时 40 微妙 */
	rt_hw_us_delay(40);
}

static int ds18b20_connect(rt_base_t pin){
		
	uint8_t retry = 0;
	
	/* 设置为输入模式 */
	rt_pin_mode(pin, PIN_MODE_INPUT);
	
	/* 5. 等待由ds18b20产生的一个低电平 */	
	while(rt_pin_read(pin) && retry < 16){
		
		retry++;
		rt_hw_us_delay(15);
	}
	if(retry >= 16){
	
		return CONNECT_TIMEOUT;
	}
	
	/* 6. 延时 240 微妙, 并由低电平变成高电平 */
	retry = 0;
	while(!rt_pin_read(pin) && retry < 16){
	
		retry++;
		rt_hw_us_delay(15);
	}
	if(retry >= 16){
	
		return CONNECT_FAILED;
	}
	return CONNECT_SUCCESS;
}

/* 初始化硬件ds18b20 */
uint8_t ds18b20_init(rt_base_t pin){

	uint8_t ret = 0;
	
	ds18b20_reset(pin);
	ret = ds18b20_connect(pin);
	
	return ret;
}

/* ds18b20 写时序 */
static void ds18b20_write_byte(rt_base_t pin, uint8_t dat){

	uint8_t i;
	uint8_t bit;
	
	/* 设置为输出模式 */
	rt_pin_mode(pin, PIN_MODE_OUTPUT);
	
	for(i = 0; i < 8; i++){

		/* 按从低位到高位顺序发送数据（一次只发送一位） */
		bit = dat & 0x01;
		dat >>= 1;
		
		if(bit){

			rt_pin_write(pin, PIN_LOW);
			rt_hw_us_delay(2);
			rt_pin_write(pin, PIN_HIGH);
			rt_hw_us_delay(60);
			
		}else{
		
			rt_pin_write(pin, PIN_LOW);
			rt_hw_us_delay(60);
			rt_pin_write(pin, PIN_HIGH);
			rt_hw_us_delay(2);	
		}	
	}
}

/* ds18b20 读时序 */
static uint8_t ds18b20_read_byte(rt_base_t pin){

	uint8_t i;
	uint8_t bit;
	uint8_t dat = 0;
	
	for(i = 0; i < 8; i++){
	
		bit = rt_pin_read(pin);
		dat = (dat >> 1) | (bit << 7);
	}
	
	return dat;
	
#if 0
	uint8_t i;
	uint8_t bit;
	uint8_t data = 0;
	for(i = 0; i < 8; i++){
		
		/* 设置为输出模式 */
		rt_pin_mode(pin, PIN_MODE_OUTPUT);
		
		/* 1. 将数据线拉低 */
		rt_pin_write(pin, PIN_LOW);
		
		/* 2. 延时 1 微妙 */
		rt_hw_us_delay(1);
		
		/* 3. 将数据线拉高，释放总线准备读数据 */
		rt_pin_write(pin, PIN_HIGH);
		
		/* 4. 延时小于 10 微妙 */
		rt_hw_us_delay(9);
		
		rt_pin_mode(pin, PIN_MODE_INPUT);
		/* 5. 读取数据线的状态得到一个状态位，并进行数据处理 */
		bit = rt_pin_read(pin);
		data = (data >> 1) | (bit << 7);
		
		/* 6. 延时 45 微妙 */
		rt_hw_us_delay(45);
	}
	return data;
#endif
}

/* ds18b20 寄存器操作指令 */
void ds18b20_Command(rt_base_t pin, uint8_t ROM_CMD, uint8_t RAM_CMD){

	ds18b20_init(pin);
	ds18b20_write_byte(pin, ROM_CMD);
	ds18b20_write_byte(pin, RAM_CMD);	
}

/* 从硬件ds18b20读取数据 */
int32_t ds18b20_get_temperature(rt_base_t pin){

	uint8_t TL, TH;
	int32_t tem;
	
	ds18b20_Command(pin, 0xcc, 0x44);	/* ds18b20 温度转换 命令 */
	ds18b20_Command(pin, 0xcc, 0xbe);	/* ds18b20 温度读取 命令 */
	TL = ds18b20_read_byte(pin);		/* 先读取低 8 位 */
	TH = ds18b20_read_byte(pin);		/* 再读取高 8 位 */
	tem = TH;
	tem <<= 8;
	tem |= TL;
	
	if(TH > 7){
		/* 温度为负数 */
		tem = ~(tem - 1);
		return (int32_t)(-tem * (0.0625 * 10 + 0.5));
	
	}else{
		
		return (int32_t)tem * (0.0625 * 10 + 0.5);
	}
}

static rt_size_t _ds18b20_polling_get_data(rt_sensor_t sensor, struct rt_sensor_data *data){

    rt_int32_t temperature_x10;
	
	/* 判断传感器的类型 */
    if (sensor->info.type == RT_SENSOR_CLASS_TEMP)
    {
        temperature_x10 = ds18b20_get_temperature((rt_base_t)sensor->config.intf.user_data);
        data->data.temp = temperature_x10;
        data->timestamp = rt_sensor_get_ts();
    }    
    return 1;
}

static rt_size_t ds18b20_fetch_data(struct rt_sensor_device *sensor, void *buf, rt_size_t len){
	
	RT_ASSERT(buf);	/* 使用ASSERT帮助验证buf是否为空, 需qemu开启使能 */
	
	/* 判断传感器的工作模式 */
    if (sensor->config.mode == RT_SENSOR_MODE_POLLING)
    {
        return _ds18b20_polling_get_data(sensor, buf);
    }
    else
        return 0;
}

static rt_err_t ds18b20_control(struct rt_sensor_device *sensor, int cmd, void *args){
    
    return (rt_err_t)RT_EOK;
}

static struct rt_sensor_ops sensor_ops = {

	.fetch_data = ds18b20_fetch_data,	/* 获取传感器数据 */
	.control    = ds18b20_control,
};


/* 传感器设备注册 */
int rt_hw_ds18b20_init(const char *name, struct rt_sensor_config *cfg){
	
    rt_int8_t result;
	/* 1. 创建一个 rt_sensor_t 的结构体指针 */
    rt_sensor_t sensor_temp = RT_NULL; 
    
    if (!ds18b20_init((rt_base_t)cfg->intf.user_data)){
        /* 温度传感器寄存器 */
		
		/* 2. 为结构体分配内存 */
        sensor_temp = rt_calloc(1, sizeof(struct rt_sensor_device));
        if (sensor_temp == RT_NULL){
			
            return -1;
		}
		
		/* 3. 完成相关初始化 */
        sensor_temp->info.type       = RT_SENSOR_CLASS_TEMP;
        sensor_temp->info.vendor     = RT_SENSOR_VENDOR_DALLAS;
        sensor_temp->info.model      = "ds18b20";
        sensor_temp->info.unit       = RT_SENSOR_UNIT_DCELSIUS;
        sensor_temp->info.intf_type  = RT_SENSOR_INTF_ONEWIRE;
        sensor_temp->info.range_max  = SENSOR_TEMP_RANGE_MAX;
        sensor_temp->info.range_min  = SENSOR_TEMP_RANGE_MIN;
        sensor_temp->info.period_min = 5;

        rt_memcpy(&sensor_temp->config, cfg, sizeof(struct rt_sensor_config));
        sensor_temp->ops = &sensor_ops;

        result = rt_hw_sensor_register(sensor_temp, name, RT_DEVICE_FLAG_RDONLY, RT_NULL);
        if (result != RT_EOK){
			
			LOG_E("device register err code: %d", result);
            if (sensor_temp){
				
				rt_free(sensor_temp);
			}
			return -RT_ERROR; 
        }

    }
    return RT_EOK;   
}
