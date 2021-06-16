
#include "sensor_dallas_ds18b20.h"
#include "sensor.h"
#include <rtdbg.h>
#include "board.h"


#define SENSOR_TEMP_RANGE_MAX (125)
#define SENSOR_TEMP_RANGE_MIN (-55)

/* �߾���(΢��)��ʱ, ͨ��Ӳ��ʵ�� */
/* @RT_WEAK �����ţ�https://itexp.blog.csdn.net/article/details/106816700 */
RT_WEAK void rt_hw_us_delay(rt_uint32_t us){

	rt_uint32_t delta;
	
	us = us * (SysTick->LOAD / (1000000 / RT_TICK_PER_SECOND));
	delta = SysTick->VAL;
	
	while(delta - SysTick->VAL < us){
	
		continue;
	}
}

static void ds18b20_reset(rt_base_t pin){
	
	/* ����Ϊ���ģʽ */
	rt_pin_mode(pin, PIN_MODE_OUTPUT);
	
	/* 1. �����������͵�ƽ */
	rt_pin_write(pin, PIN_LOW);
	
	/* 2. ��ʱ 480 ~ 960 ΢�� */
	rt_hw_us_delay(780);
	
	/* 3. �����������ߵ�ƽ */
	rt_pin_write(pin, PIN_HIGH);
	
	/* 4. ��ʱ 40 ΢�� */
	rt_hw_us_delay(40);
}

static int ds18b20_connect(rt_base_t pin){
		
	uint8_t retry = 0;
	
	/* ����Ϊ����ģʽ */
	rt_pin_mode(pin, PIN_MODE_INPUT);
	
	/* 5. �ȴ���ds18b20������һ���͵�ƽ */	
	while(rt_pin_read(pin) && retry < 16){
		
		retry++;
		rt_hw_us_delay(15);
	}
	if(retry >= 16){
	
		return CONNECT_TIMEOUT;
	}
	
	/* 6. ��ʱ 240 ΢��, ���ɵ͵�ƽ��ɸߵ�ƽ */
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

/* ��ʼ��Ӳ��ds18b20 */
uint8_t ds18b20_init(rt_base_t pin){

	uint8_t ret = 0;
	
	ds18b20_reset(pin);
	ret = ds18b20_connect(pin);
	
	return ret;
}

/* ds18b20 дʱ�� */
static void ds18b20_write_byte(rt_base_t pin, uint8_t dat){

	uint8_t i;
	uint8_t bit;
	
	/* ����Ϊ���ģʽ */
	rt_pin_mode(pin, PIN_MODE_OUTPUT);
	
	for(i = 0; i < 8; i++){

		/* ���ӵ�λ����λ˳�������ݣ�һ��ֻ����һλ�� */
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

/* ds18b20 ��ʱ�� */
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
		
		/* ����Ϊ���ģʽ */
		rt_pin_mode(pin, PIN_MODE_OUTPUT);
		
		/* 1. ������������ */
		rt_pin_write(pin, PIN_LOW);
		
		/* 2. ��ʱ 1 ΢�� */
		rt_hw_us_delay(1);
		
		/* 3. �����������ߣ��ͷ�����׼�������� */
		rt_pin_write(pin, PIN_HIGH);
		
		/* 4. ��ʱС�� 10 ΢�� */
		rt_hw_us_delay(9);
		
		rt_pin_mode(pin, PIN_MODE_INPUT);
		/* 5. ��ȡ�����ߵ�״̬�õ�һ��״̬λ�����������ݴ��� */
		bit = rt_pin_read(pin);
		data = (data >> 1) | (bit << 7);
		
		/* 6. ��ʱ 45 ΢�� */
		rt_hw_us_delay(45);
	}
	return data;
#endif
}

/* ds18b20 �Ĵ�������ָ�� */
void ds18b20_Command(rt_base_t pin, uint8_t ROM_CMD, uint8_t RAM_CMD){

	ds18b20_init(pin);
	ds18b20_write_byte(pin, ROM_CMD);
	ds18b20_write_byte(pin, RAM_CMD);	
}

/* ��Ӳ��ds18b20��ȡ���� */
int32_t ds18b20_get_temperature(rt_base_t pin){

	uint8_t TL, TH;
	int32_t tem;
	
	ds18b20_Command(pin, 0xcc, 0x44);	/* ds18b20 �¶�ת�� ���� */
	ds18b20_Command(pin, 0xcc, 0xbe);	/* ds18b20 �¶ȶ�ȡ ���� */
	TL = ds18b20_read_byte(pin);		/* �ȶ�ȡ�� 8 λ */
	TH = ds18b20_read_byte(pin);		/* �ٶ�ȡ�� 8 λ */
	tem = TH;
	tem <<= 8;
	tem |= TL;
	
	if(TH > 7){
		/* �¶�Ϊ���� */
		tem = ~(tem - 1);
		return (int32_t)(-tem * (0.0625 * 10 + 0.5));
	
	}else{
		
		return (int32_t)tem * (0.0625 * 10 + 0.5);
	}
}

static rt_size_t _ds18b20_polling_get_data(rt_sensor_t sensor, struct rt_sensor_data *data){

    rt_int32_t temperature_x10;
	
	/* �жϴ����������� */
    if (sensor->info.type == RT_SENSOR_CLASS_TEMP)
    {
        temperature_x10 = ds18b20_get_temperature((rt_base_t)sensor->config.intf.user_data);
        data->data.temp = temperature_x10;
        data->timestamp = rt_sensor_get_ts();
    }    
    return 1;
}

static rt_size_t ds18b20_fetch_data(struct rt_sensor_device *sensor, void *buf, rt_size_t len){
	
	RT_ASSERT(buf);	/* ʹ��ASSERT������֤buf�Ƿ�Ϊ��, ��qemu����ʹ�� */
	
	/* �жϴ������Ĺ���ģʽ */
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

	.fetch_data = ds18b20_fetch_data,	/* ��ȡ���������� */
	.control    = ds18b20_control,
};


/* �������豸ע�� */
int rt_hw_ds18b20_init(const char *name, struct rt_sensor_config *cfg){
	
    rt_int8_t result;
	/* 1. ����һ�� rt_sensor_t �Ľṹ��ָ�� */
    rt_sensor_t sensor_temp = RT_NULL; 
    
    if (!ds18b20_init((rt_base_t)cfg->intf.user_data)){
        /* �¶ȴ������Ĵ��� */
		
		/* 2. Ϊ�ṹ������ڴ� */
        sensor_temp = rt_calloc(1, sizeof(struct rt_sensor_device));
        if (sensor_temp == RT_NULL){
			
            return -1;
		}
		
		/* 3. �����س�ʼ�� */
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
