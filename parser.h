#ifndef PARSER_H
#define PARSER_H

#include <stddef.h>
#include <stdint.h>
// 帧头固定字节，通讯时用来同步，寻找一阵数据的起点
// 0xAA转二进制为10101010，
// 因为交替0、1，在串口通信里不容易和随机噪声巧合撞头，是很经典的同步头
// u 表示unsigned 无符号符
#define PARSER_HEAD 0xAAu
// 数组最大长度
#define PARSER_MAX_DATA 32u
// 解析器返回的事件类型
typedef enum { EV_NONE = 0, EV_FRAME, EV_ERR_LEN, EV_ERR_CRC } parser_event_t;

// 帧结构体，用于存放一帧有效业务数据
typedef struct {

  uint8_t len;                   // 数据区有效长度
  uint8_t seq;                   // 帧序号
  uint8_t type;                  // 消息类型
  uint8_t data[PARSER_MAX_DATA]; // 数组
} frame_t;
// 解析器实例结构体，保存状态机所有状态，缓存等
typedef struct {
  int state;       // 状态机当前状态
  frame_t frame;   // 正在解析的帧缓存
  uint8_t fill;    // data已经收到的字节数，用来判断data是否收完
  uint8_t crc;     // 流式计算CRC，收到一个字节就更新一次
  uint8_t bad_len; // 长度出错时，保存收到的非法长度，给上层打印日志
  uint8_t got_crc; // CRC出错时，保存收到的CRC值，用来和本地算出的CRC对比调试

  // 统计
  uint32_t ok;          // 成功解析的合法帧总数
  uint32_t crc_err;     // CRC校验失败的数目
  uint32_t invalid_len; // 长度非法的错误帧总数
  uint32_t noise;       // 噪音/乱无效的字节计数
} parser_t;

// 解析器初始化，重置状态机，清空缓存，清零统计计数
void parser_init(parser_t *p);
// 核心流式喂字节函数
// 每收到1个收到的串口字节，返回parser_event_t，通知上层解析
parser_event_t parser_feed(parser_t *p, uint8_t byte);
// 流结束检查函数，返回1表示还有半包残留数据，0表示无半包
int parser_finish(parser_t *p);
// CRC8更新，逐字节迭代计算CRC，给状态机边收边算CRC使用
uint8_t crc8_update(uint8_t crc, uint8_t byte);
// 整块buf计算CRC
uint8_t crc8(const uint8_t *buf, size_t len);

#endif