# 基于 RT-Thread 的分布式无线温度监控系统

发送端：**芯片：STMF407，使用 ds18b20 读取温度**

SEND(mailbox+mempool)：**使用 nrf24l01 以（邮箱+内存池）方式发送数据。**



接收端：**使用 nrf24l01 接收数据，使用 ESP8266 上传到 OneNet**

RECV(SPI_Flash)：**在 SPI_Flash 上使用文件系统**

RECV(SD_Card)：**在 SD_Card 上使用文件系统**

