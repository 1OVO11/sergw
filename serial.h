#ifndef SERIAL_H
#define SERIAL_H

#include <stddef.h>
#include <stdint.h>
#include <termios.h>
#include <unistd.h>

typedef struct {
  int fd;                 // 串口文件描述符
  int baud;               // 波特率
  int has_old;            // 标记：是否打开了前原始终端的配置
  struct termios old_tio; // 保存打开串口之前的termios参数，关闭时恢复
} serial_t;               // 保存串口所有上下文信息

// 打开串口并配置参数
int serial_open(serial_t *s, const char *path, int baud);
// 从串口读取数据
ssize_t serial_read(serial_t *s, uint8_t *buf, size_t len);
// 关闭串口，恢复原有配置
int serial_close(serial_t *s);

#endif