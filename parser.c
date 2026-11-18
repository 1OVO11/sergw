#include "parser.h"

enum { ST_HEAD = 0, ST_LEN, ST_SEQ, ST_TYPE, ST_DATA, ST_CRC };

void parser_init(parser_t *p) {
  p->state = ST_HEAD;
  p->fill = 0;
  p->crc = 0;
  p->bad_len = 0;
  p->got_crc = 0;

  p->ok = 0;
  p->crc_err = 0;
  p->invalid_len = 0;
  p->noise = 0;
}

int parser_finish(parser_t *p) { return (p->state != ST_HEAD) ? 1 : 0; }

uint8_t crc8_update(uint8_t crc, uint8_t byte) {
  int i;
  crc ^= byte;
  for (i = 0; i < 8; i++) {
    if (crc & 0x80u) {
      crc = (uint8_t)((crc << 1) ^ 0x07u);
    } else {
      crc = (uint8_t)(crc << 1);
    }
  }
  return crc;
}
uint8_t crc8(const uint8_t *buf, size_t len) {
  uint8_t crc = 0;
  size_t i;
  for (i = 0; i < len; i++) {
    crc = crc8_update(crc, buf[i]);
  }
  return crc;
}
parser_event_t parser_feed(parser_t *p, uint8_t byte) {
  parser_event_t ev = EV_NONE;

  switch (p->state) {

  case ST_HEAD:
    if (byte == PARSER_HEAD) {
      p->crc = 0;
      p->state = ST_LEN;
    } else {
      p->noise++;
    }
    break;

  case ST_LEN:
    p->frame.len = byte;

    if (byte > PARSER_MAX_DATA) {
      p->invalid_len++;
      p->bad_len = byte;
      p->state = ST_HEAD;

      /* 重同步：这个字节本身可能就是下一帧的帧头。
       * 注意 0xAA = 170 > 32，一定会走到这里，所以要判一下。 */
      if (byte == PARSER_HEAD) {
        p->crc = 0;
        p->state = ST_LEN;
      }
      ev = EV_ERR_LEN;
    } else {
      p->crc = crc8_update(p->crc, byte);
      p->fill = 0;
      p->state = ST_SEQ;
    }
    break;

  case ST_SEQ:
    p->frame.seq = byte;
    p->crc = crc8_update(p->crc, byte);
    p->state = ST_TYPE;
    break;

  case ST_TYPE:
    p->frame.type = byte;
    p->crc = crc8_update(p->crc, byte);
    /* LEN=0 的帧（心跳）没有 DATA，直接去收 CRC */
    p->state = (p->frame.len == 0) ? ST_CRC : ST_DATA;
    break;

  case ST_DATA:
    p->frame.data[p->fill] = byte;
    p->fill++;
    p->crc = crc8_update(p->crc, byte);
    if (p->fill >= p->frame.len) {
      p->state = ST_CRC;
    }
    break;

  case ST_CRC:
    p->got_crc = byte;
    p->state = ST_HEAD;

    if (byte == p->crc) {
      p->ok++;
      ev = EV_FRAME;
    } else {
      p->crc_err++;
      ev = EV_ERR_CRC;
    }
    break;

  default:
    p->state = ST_HEAD;
    break;
  }

  return ev;
}
