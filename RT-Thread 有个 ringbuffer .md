在嵌入式开发中，我们经常需要用到 FIFO 数据结构来存储数据，比如任务间的通信、串口数据收发等场合。很多小伙伴不知道 RT-Thread 为我们提供了一个 ringbuffer 数据结构，代码位于：

* components/drivers/src/ringbuffer.c
* components/drivers/include/ipc/ringbuffer.h

RingBuffer 其实就是先进先出（FIFO）的循环缓冲区。把一段线性的存储空间当作一个环形的存储空间使用，可以提高存储空间的利用率。

![](D:\NotBook_学习笔记\images\20200713003707287.png)

## 数据结构
RT-Thread 定义了 rt_ringbuffer 结构体，包括四组成员：缓冲区指针 buffer_ptr、缓冲区大小 buffer_size、读指针、写指针。

```c
struct rt_ringbuffer
{
    rt_uint8_t *buffer_ptr;
    rt_uint16_t read_mirror : 1;
    rt_uint16_t read_index : 15;
    rt_uint16_t write_mirror : 1;
    rt_uint16_t write_index : 15;
    rt_int16_t buffer_size;
};
```


对于读、写指针，rt_ringbuffer 结构体使用位域来定义 read 和 write 的索引值和镜像位。更具体来说，使用 MSB（最高有效位）作为 read_index 和 write_index 变量的镜像位。通过这种方式，为缓冲区添加了虚拟镜像，用于指示 read 和 write 指针指向的是普通缓冲区还是镜像缓冲区。

* 如果 write_index 和 read_index 相等，但在不同镜像，说明缓冲区满了；
* 如果 write_index 和 read_index 相等，但在相同镜像，说明缓冲区空了。

为了让大家更好地理解，我给大家画了个图：

![](D:\NotBook_学习笔记\images\20200713003626257.png)

## 接口函数
**初始化与重置**

```c 
void rt_ringbuffer_init(struct rt_ringbuffer *rb, rt_uint8_t *pool, rt_int16_t size);
void rt_ringbuffer_reset(struct rt_ringbuffer *rb);
```
这两个函数适用于以静态方式初始化或重置 ringbuffer，需要事先准备好 ringbuffer 对象和一段内存空间。

**创建和销毁**
```c
struct rt_ringbuffer* rt_ringbuffer_create(rt_uint16_t length);
void rt_ringbuffer_destroy(struct rt_ringbuffer *rb);
```
这两个函数适用于以动态方式创建和销毁 ringbuffer，将在堆空间申请相关资源，并返回一个 ringbuffer 指针。

**写入数据**
```c
rt_size_t rt_ringbuffer_put(struct rt_ringbuffer *rb, const rt_uint8_t *ptr, rt_uint16_t length);
rt_size_t rt_ringbuffer_put_force(struct rt_ringbuffer *rb, const rt_uint8_t *ptr, rt_uint16_t length);
rt_size_t rt_ringbuffer_putchar(struct rt_ringbuffer *rb, const rt_uint8_t ch);
rt_size_t rt_ringbuffer_putchar_force(struct rt_ringbuffer *rb, const rt_uint8_t ch);
```
往 ringbuffer 写入数据可以使用这组函数，其中 _put 为写入一串字符，_putchar 为写入一个字符，带 _force 的函数则表示如果缓冲区满了，将直接用新数据覆盖旧数据（谨慎使用）。

**读出数据**
```c
rt_size_t rt_ringbuffer_get(struct rt_ringbuffer *rb, rt_uint8_t *ptr, rt_uint16_t length);
rt_size_t rt_ringbuffer_getchar(struct rt_ringbuffer *rb, rt_uint8_t *ch);
```
从 ringbuffer 读出数据可以使用这组函数，其中 _get 为读出一串字符，_getchar 为读出一个字符。

**获取长度**
读写操作前可以先判断是否有数据可读或者有位置可写，ringbuffer 提供了三个接口获取长度，包括获取 ringbuffer 的总长度、数据长度、空闲长度。

**获取缓冲区数据长度**

```c
rt_size_t rt_ringbuffer_data_len(struct rt_ringbuffer *rb);
```
**获取缓冲区总长度**

```c
rt_uint16_t rt_ringbuffer_get_size(struct rt_ringbuffer *rb);
```
**获取缓冲区空闲长度**
```c
#define rt_ringbuffer_space_len(rb) ((rb)->buffer_size - rt_ringbuffer_data_len(rb))
```
## 应用示例
下面通过一个简单的示例，来看看在程序中该如何使用 ringbuffer。首先创建一个 ringbuffer 对象，然后 Producer 线程往 ringbuffer 写入数据，Consumer 线程从 ringbuffer 读出数据。这是一个典型生产者-消费者模型。

```c
#include <rtthread.h>
#include <rtdevice.h>
#include <string.h>

#define RING_BUFFER_LEN        8

static struct rt_ringbuffer *rb;
static char  *str = "Hello, World";

static void consumer_thread_entry(void *arg)
{
    char ch;

    while (1)
    {
        if (1 == rt_ringbuffer_getchar(rb, &ch))
        {
            rt_kprintf("[Consumer] <- %c\n", ch);
        }
        rt_thread_mdelay(500);
    }
}

static void ringbuffer_sample(int argc, char** argv)
{
    rt_thread_t tid;
    rt_uint16_t i = 0;

    rb = rt_ringbuffer_create(RING_BUFFER_LEN);
    if (rb == RT_NULL)
    {
        rt_kprintf("Can't create ringbffer");
        return;
    }
    
    tid = rt_thread_create("consumer", consumer_thread_entry, RT_NULL,
                           1024, RT_THREAD_PRIORITY_MAX/3, 20);
    if (tid == RT_NULL)
    {
        rt_ringbuffer_destroy(rb);
    }
    rt_thread_startup(tid);


    while (str[i] != '\0')
    {
        rt_kprintf("[Producer] -> %c\n", str[i]);
        rt_ringbuffer_putchar(rb, str[i++]);
        rt_thread_mdelay(500);
    }
    
    rt_thread_delete(tid);
    rt_ringbuffer_destroy(rb);

}
#ifdef RT_USING_FINSH
MSH_CMD_EXPORT(ringbuffer_sample, Start a producer and a consumer with a ringbuffer);
#endif
```
运行结果：

![](D:\NotBook_学习笔记\images\20200713003545581.png)