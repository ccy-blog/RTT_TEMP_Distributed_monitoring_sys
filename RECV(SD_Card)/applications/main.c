/*
 * Copyright (c) 2006-2018, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2018-11-06     SummerGift   first version
 * 2018-11-19     flybreak     add stm32f407-atk-explorer bsp
 * 2019-07-15     WillianCham  DIY Demo1(First week mission)
 * 2019-07-24     WillianChan  DIY Demo2 use mailbox and mempool(Second week mission)
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>
#include <string.h>
#include <stdio.h>
#include <rtdbg.h>
#include "drv_spi.h"
#include "nrf24l01.h"
#include "onenet.h"


/* defined the LED0 pin: PF9 */
#define LED0_PIN          GET_PIN(E, 7)
#define NRF24L01_CE_PIN   GET_PIN(D, 4)

#define SEND_DEVICE_NUM     2						/* ���ͽڵ�ĸ��� */
#define RINGBUFFERSIZE      (4096)					/* fingbuffer ��������С */
#define THRESHOLD           (RINGBUFFERSIZE / 2)	/* fingbuffer ��������ֵ */
#define WRITE_EVENT(i)      (0x01U << i)			/* ����Ȥ���¼� */
#define NRF24L01_SPI_DEVICE "spi20" 
#define MB_LEN			  (4)
#define MP_LEN            (MB_LEN)
#define MP_BLOCK_SIZE     RT_ALIGN(SEND_DEVICE_NUM * sizeof(struct recvdata), sizeof(intptr_t))	/* Ϊ���ֽڶ��� */
	

struct recvdata{

	int timestamp;		/* ʱ��ֵ */
	float temperature;	/* �¶�ֵ */

};

static char *DS_NAME[SEND_DEVICE_NUM] = {

	"temperature_p0", 
	"temperature_p1",
};

static rt_thread_t onenet_mqtt_init_thread;		/* mqtt��ʼ���߳� */
static rt_thread_t onenet_upload_data_thread;	/* onnet�����ϴ��߳� */
static rt_thread_t nrf24l10_thread;				/* nrf24l10���������߳� */
static rt_thread_t DFS_thread_p0;				/* ���ݴ洢�ļ��߳�1 */
static rt_thread_t DFS_thread_p1;				/* ���ݴ洢�ļ��߳�2 */
static rt_thread_t led_thread;					/* led��˸�߳� */
static rt_sem_t mqttinit_sem;					/* �ź��� */
static struct rt_ringbuffer *recvdatabuf;		/* ringbuffer */
static rt_event_t recvdata_event;				/* �¼��� */
static rt_mailbox_t tmp_msg_mb;					/* ���� */
static rt_mp_t tmp_msg_mp;						/* �ڴ�� */


static void onenet_mqtt_init_entry(void *parameter){

	while(1){
		
		/* mqtt ��ʼ�� */
		if(!onenet_mqtt_init()){
	
			/* mqtt��ʼ���ɹ�֮���ͷ��ź�����֪onenet_upload_data_thread�߳̿����ϴ������� */
			rt_sem_release(mqttinit_sem);
			return ;
		}
		rt_thread_mdelay(100);
	}
}

static void onenet_upload_data_entry(void *parameter){

	struct recvdata *buf_mp;
	int i;
	
	/* ���õȴ���ʽ�����ź����������ղ��������̻߳�һֱ���� */
	rt_sem_take(mqttinit_sem, RT_WAITING_FOREVER);
	/* ɾ���ź�����������Դ */
	rt_sem_delete(mqttinit_sem);

	while(1){
		
		if(rt_mb_recv(tmp_msg_mb, (rt_ubase_t *)&buf_mp, RT_WAITING_FOREVER) == RT_EOK){
		
			/* �ϴ����ͽڵ�i�����ݵ�OneNet��������������������temperature_p0 */
			for(i = 0; i < SEND_DEVICE_NUM; i++){
			
				onenet_mqtt_upload_digit(DS_NAME[i], buf_mp[i].temperature);
			}
			rt_mp_free(buf_mp);
			buf_mp = RT_NULL;
		}
	}
}

static void nrf24l01_receive_entry(void *parameter){
		
	static char str_data[32];
	
	struct recvdata buf[SEND_DEVICE_NUM];
	struct recvdata *buf_mp;
	int i;
	
	while(1){
		
		rt_thread_mdelay(200);
	
		/* �����nrf24_prx_run(), �ж����ĸ�ͨ������������ */
		if(!nrf24_prx_run()){
			
			for(i = 0; i < SEND_DEVICE_NUM; i++){
				
				/* ͨ��sscnaf�����յ��ķ��ͽڵ�1������ */
				if(sscanf((char *)RxBuf_P0, "%d,+%f", &buf[i].timestamp, &buf[i].temperature) != 2){
				
					if(sscanf((char *)RxBuf_P0, "%d,-%f", &buf[i].timestamp, &buf[i].temperature) != 2){

						buf[i].temperature = 0;
						buf[i].timestamp = 0;
						continue;
					}
					buf[i].temperature = -buf[i].temperature;
				}
				
				/* �����ݴ�ŵ�ringbuffer�� */
				rt_ringbuffer_put(recvdatabuf, (rt_uint8_t *)RxBuf_P0, strlen(str_data));
				
				/* �����¼� */
				rt_event_send(recvdata_event, WRITE_EVENT(i));
			}
			
			/* ����һ���ڴ� Ҫ���ڴ������ �͹���ȴ� */
			buf_mp = rt_mp_alloc(tmp_msg_mp, RT_WAITING_FOREVER);
			for(i = 0; i < SEND_DEVICE_NUM; i++){
			
				buf_mp[i].timestamp = buf[i].timestamp;
				buf_mp[i].temperature = buf[i].temperature;
			}
			rt_mb_send(tmp_msg_mb, (rt_ubase_t)buf_mp);
			buf_mp = NULL;
		}
	}
}

static void save_recv_p0_data_entry(void *parameter){

	rt_uint32_t set;
	FILE *recvdatafile_p0 = RT_NULL;
	static int writebuffer[1024];
	rt_size_t size;
	
	while(1){
	
		/* ���ܸ���Ȥ���¼�WRITE_EVENT, �����õȴ���ʽȥ���� */
		if(rt_event_recv(recvdata_event, WRITE_EVENT(0), RT_EVENT_FLAG_OR | RT_EVENT_FLAG_CLEAR, RT_WAITING_FOREVER, &set) != RT_EOK){
		
			continue;
		}
		
		/* ���ܸ���Ȥ���¼�WRITE_EVENT, ��1000ms��ʱ��ʽȥ���� */
		if(rt_event_recv(recvdata_event, WRITE_EVENT(0), RT_EVENT_FLAG_OR | RT_EVENT_FLAG_CLEAR, rt_tick_from_millisecond(1000), &set) == RT_EOK){
	
			/* �ж�д������ݴ�С��û�������õ�ringbuffer����ֵ */
			if(rt_ringbuffer_data_len(recvdatabuf) > THRESHOLD){
			
				/* ����ֵ��ֱ��д���� */
				recvdatafile_p0 = fopen("recvdata_p0.csv", "ab+");
				if(recvdatafile_p0 != RT_NULL){
				
					while(rt_ringbuffer_data_len(recvdatabuf)){
						
						size = rt_ringbuffer_get(recvdatabuf, (rt_uint8_t *)writebuffer, THRESHOLD);	
						fwrite(writebuffer, 1, size, recvdatafile_p0);
					}
					fclose(recvdatafile_p0);
				}
			}
			/* ��ֵû�� */
			continue;
		}
		/* 1000ms���ˣ���û�н��յ�����Ȥ���¼���ʱ�򲻹ܵ�û����ֵ��ֱ��д */
		recvdatafile_p0 = fopen("recvdata_p0.csv", "ab+");
		if(recvdatafile_p0 != RT_NULL){
		
			while(rt_ringbuffer_data_len(recvdatabuf)){
				
				size = rt_ringbuffer_get(recvdatabuf, (rt_uint8_t *)writebuffer, THRESHOLD);	
				fwrite(writebuffer, 1, size, recvdatafile_p0);
			}
			fclose(recvdatafile_p0);
		}
	}
}

static void save_recv_p1_data_entry(void *parameter){

	rt_uint32_t set;
	FILE *recvdatafile_p1 = RT_NULL;
	static int writebuffer[1024];
	rt_size_t size;
	
	while(1){
	
		/* ���ܸ���Ȥ���¼�WRITE_EVENT, �����õȴ���ʽȥ���� */
		if(rt_event_recv(recvdata_event, WRITE_EVENT(1), RT_EVENT_FLAG_OR | RT_EVENT_FLAG_CLEAR, RT_WAITING_FOREVER, &set) != RT_EOK){
		
			continue;
		}
		
		/* ���ܸ���Ȥ���¼�WRITE_EVENT, ��1000ms��ʱ��ʽȥ���� */
		if(rt_event_recv(recvdata_event, WRITE_EVENT(1), RT_EVENT_FLAG_OR | RT_EVENT_FLAG_CLEAR, rt_tick_from_millisecond(1000), &set) == RT_EOK){
	
			/* �ж�д������ݴ�С��û�������õ�ringbuffer����ֵ */
			if(rt_ringbuffer_data_len(recvdatabuf) > THRESHOLD){
			
				/* ����ֵ��ֱ��д���� */
				recvdatafile_p1 = fopen("recvdata_p1.csv", "ab+");
				if(recvdatafile_p1 != RT_NULL){
				
					while(rt_ringbuffer_data_len(recvdatabuf)){
						
						size = rt_ringbuffer_get(recvdatabuf, (rt_uint8_t *)writebuffer, THRESHOLD);	
						fwrite(writebuffer, 1, size, recvdatafile_p1);
					}
					fclose(recvdatafile_p1);
				}
			}
			/* ��ֵû�� */
			continue;
		}
		/* 1000ms���ˣ���û�н��յ�����Ȥ���¼���ʱ�򲻹ܵ�û����ֵ��ֱ��д */
		recvdatafile_p1 = fopen("recvdata_p1.csv", "ab+");
		if(recvdatafile_p1 != RT_NULL){
		
			while(rt_ringbuffer_data_len(recvdatabuf)){
				
				size = rt_ringbuffer_get(recvdatabuf, (rt_uint8_t *)writebuffer, THRESHOLD);	
				fwrite(writebuffer, 1, size, recvdatafile_p1);
			}
			fclose(recvdatafile_p1);
		}
	}
}


static void led_shine_entry(void *parameter)
{
    rt_pin_mode(LED0_PIN, PIN_MODE_OUTPUT);
    
    while(1)
    {
        rt_pin_write(LED0_PIN, PIN_HIGH);
        rt_thread_mdelay(500);
        rt_pin_write(LED0_PIN, PIN_LOW);
        rt_thread_mdelay(500);
    }
}

void nrf24l01_init(){

	nrf24_cfg_t cfg;
	struct hal_nrf24l01_port_cfg halcfg;
		
	nrf24_default_param(&cfg);
	halcfg.ce_pin = NRF24L01_CE_PIN;
	halcfg.spi_device_name = NRF24L01_SPI_DEVICE;
	cfg.role = ROLE_PRX;
	cfg.ud = &halcfg;
	cfg.use_irq = 0;
	/* �޸��� nrf24l01 �������ʵ�ֶ��ͨ�Ź���, ����� nrf24l01_multichannel(); ���� */
	nrf24_init(&cfg);
}

int main(void)
{	
	mqttinit_sem = rt_sem_create("mqtt_sem", RT_NULL, RT_IPC_FLAG_FIFO);
	RT_ASSERT(mqttinit_sem);
	
	recvdatabuf = rt_ringbuffer_create(RINGBUFFERSIZE);	/* ringbuffer�Ĵ�С��4k */
	RT_ASSERT(recvdatabuf);
	
	recvdata_event = rt_event_create("temp_evt0", RT_IPC_FLAG_FIFO);
	RT_ASSERT(recvdata_event);
	
	tmp_msg_mb = rt_mb_create("temp_mb0", MB_LEN, RT_IPC_FLAG_FIFO);
	RT_ASSERT(tmp_msg_mb);
	
	tmp_msg_mp = rt_mp_create("temp_mb1", MP_LEN, MP_BLOCK_SIZE);
	RT_ASSERT(tmp_msg_mp);
	
	onenet_mqtt_init_thread = rt_thread_create("mqttinit", onenet_mqtt_init_entry, RT_NULL, 1024, RT_THREAD_PRIORITY_MAX / 2 -2, 20);
	if(onenet_mqtt_init_thread != RT_NULL){
	
		rt_thread_startup(onenet_mqtt_init_thread);
	}
	
	onenet_upload_data_thread = rt_thread_create("uploaddata", onenet_upload_data_entry, RT_NULL, 1024, RT_THREAD_PRIORITY_MAX / 2 - 2, 20);
	if(onenet_upload_data_thread != RT_NULL){
	
		rt_thread_startup(onenet_upload_data_thread);
	} 
	
	nrf24l01_init();
	nrf24l10_thread = rt_thread_create("nrfrecv", nrf24l01_receive_entry, RT_NULL, 1024, RT_THREAD_PRIORITY_MAX / 2, 20);
	if(nrf24l10_thread != RT_NULL){
	
		rt_thread_startup(nrf24l10_thread);
	}
	
	DFS_thread_p0 = rt_thread_create("DFSsaveP0", save_recv_p0_data_entry, RT_NULL, 1024, RT_THREAD_PRIORITY_MAX / 2 - 1, 20);
	if(DFS_thread_p0 != RT_NULL){
	
		rt_thread_startup(DFS_thread_p0);
	}
	
	DFS_thread_p1 = rt_thread_create("DFSsaveP1", save_recv_p1_data_entry, RT_NULL, 1024, RT_THREAD_PRIORITY_MAX / 2 - 1, 20);
	if(DFS_thread_p1 != RT_NULL){
	
		rt_thread_startup(DFS_thread_p1);
	}
	
	
	
	led_thread = rt_thread_create("ledshine", led_shine_entry, "temp_ds18b20", 192, RT_THREAD_PRIORITY_MAX / 2, 20);
	if(led_thread != RT_NULL){
	
		rt_thread_startup(led_thread);
	}
	
    return RT_EOK;
}


static int rt_hw_nrf24l01_init(void){
	
	/* ���� SPI �豸 */
	rt_hw_spi_device_attach("spi2", "spi20", GPIOD, GPIO_PIN_5);
	
	return RT_EOK;
}
INIT_COMPONENT_EXPORT(rt_hw_nrf24l01_init);	

