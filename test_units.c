#include <stdint.h>
#include <stdio.h>

#include "parser.h"

static int failures;

#define CHECK(cond, msg)                                                       \
  do {                                                                         \
    if (cond) {                                                                \
      printf("ok   %s\n", (msg));                                              \
    } else {                                                                   \
      printf("FAIL %s\n", (msg));                                              \
      failures++;                                                              \
    }                                                                          \
  } while (0)

static void test_crc(void) {
  uint8_t v = crc8((const uint8_t *)"123456789", 9);

  printf("crc8(\"123456789\") = 0x%02X\n", (unsigned)v);
  CHECK(v == 0xF4u, "CRC8 标准校验值 = 0xF4");
}

static void test_heartbeat_halfpacket(void) {
  uint8_t body[3] = {0x00, 0x01, 0x02};
  uint8_t f[5];
  parser_t p;
  parser_event_t ev = EV_NONE;
  size_t i;

  f[0] = PARSER_HEAD;
  f[1] = 0x00;
  f[2] = 0x01;
  f[3] = 0x02;
  f[4] = crc8(body, sizeof body);

  parser_init(&p);
  for (i = 0; i < sizeof f; i++) {
    ev = parser_feed(&p, f[i]);
    if (i + 1 < sizeof f) {
      CHECK(ev == EV_NONE, "半包：未收完不产生事件");
    }
  }
  CHECK(ev == EV_FRAME, "半包：收完产生 EV_FRAME");
  CHECK(p.frame.len == 0u, "心跳帧 LEN  = 0");
  CHECK(p.frame.seq == 1u, "心跳帧 SEQ  = 1");
  CHECK(p.frame.type == 0x02u, "心跳帧 TYPE = 0x02");
}

static void test_noise(void) {
  parser_t p;
  int i;

  parser_init(&p);
  for (i = 0; i < 100; i++) {
    (void)parser_feed(&p, 0x00);
  }
  CHECK(p.noise == 100u, "100 个噪声字节全部计入");
}

static void test_bad_len(void) {
  parser_t p;

  parser_init(&p);
  CHECK(parser_feed(&p, PARSER_HEAD) == EV_NONE, "帧头不产生事件");
  CHECK(parser_feed(&p, 0x21) == EV_ERR_LEN, "LEN=33 触发 EV_ERR_LEN");
  CHECK(p.bad_len == 0x21u, "非法长度值被记录");
}

/* 重同步：LEN 字节本身是 0xAA（=170 > 32，非法），
 * 但它同时也是合法帧头，必须被重新当作帧头使用 */
static void test_resync(void) {
  uint8_t body[3] = {0x00, 0x01, 0x02};
  uint8_t stream[6];
  parser_t p;
  parser_event_t ev = EV_NONE;
  size_t i;

  stream[0] = PARSER_HEAD;
  stream[1] = PARSER_HEAD; /* 既非法 LEN，又是新帧头 */
  stream[2] = 0x00;
  stream[3] = 0x01;
  stream[4] = 0x02;
  stream[5] = crc8(body, sizeof body);

  parser_init(&p);
  for (i = 0; i < sizeof stream; i++) {
    ev = parser_feed(&p, stream[i]);
  }
  CHECK(p.invalid_len == 1u, "重同步：记录一次非法长度");
  CHECK(ev == EV_FRAME, "重同步：丢坏帧后仍解析出新帧");
  CHECK(p.frame.seq == 1u, "重同步：新帧序号正确");
}

int main(void) {
  test_crc();
  test_heartbeat_halfpacket();
  test_noise();
  test_bad_len();
  test_resync();

  if (failures != 0) {
    printf("\n%d 个断言失败\n", failures);
    return 1;
  }
  printf("\n全部通过\n");
  return 0;
}