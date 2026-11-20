#define _DEFAULT_SOURCE

#include "serial.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

// Linux termios 不能直接填数字给 cfsetispeed，必须用 Bxxx 宏
static int baud_to_speed(int baud, speed_t *out) { // 波特率数值转 termios 宏
  switch (baud) {
  case 9600:
    *out = B9600;
    return 0;
  case 19200:
    *out = B19200;
    return 0;
  case 38400:
    *out = B38400;
    return 0;
  case 57600:
    *out = B57600;
    return 0;
  case 115200:
    *out = B115200;
    return 0;
  case 230400:
    *out = B230400;
    return 0;
  case 460800:
    *out = B460800;
    return 0;
  default:
    return -1;
  }
}

int serial_open(serial_t *s, const char *path, int baud) {
  struct termios tio;
  speed_t speed;
  int fd;
  int saved;

  s->fd = -1;
  s->baud = baud;
  s->has_old = 0;

  if (baud_to_speed(baud, &speed) != 0) {
    errno = EINVAL;
    return -1;
  }

  fd = open(path, O_RDWR | O_NOCTTY);
  if (fd < 0) {
    return -1;
  }

  if (tcgetattr(fd, &tio) != 0) {
    goto fail;
  }
  s->old_tio = tio;
  s->has_old = 1;

  cfmakeraw(&tio);

  tio.c_cflag |= (CLOCAL | CREAD);
  tio.c_cflag &= ~CSTOPB;
  tio.c_cflag &= ~PARENB;
  tio.c_cflag &= ~CRTSCTS;
  tio.c_cflag &= ~CSIZE;
  tio.c_cflag |= CS8;

  tio.c_cc[VMIN] = 0;
  tio.c_cc[VTIME] = 5;

  if (cfsetispeed(&tio, speed) != 0)
    goto fail;
  if (cfsetospeed(&tio, speed) != 0)
    goto fail;
  if (tcsetattr(fd, TCSANOW, &tio) != 0)
    goto fail;

  tcflush(fd, TCIOFLUSH);

  s->fd = fd;
  return 0;

fail:
  saved = errno;
  close(fd);
  errno = saved;
  return -1;
}
ssize_t serial_read(serial_t *s, uint8_t *buf, size_t len) {
  return read(s->fd, buf, len);
}

int serial_close(serial_t *s) {
  if (s->fd < 0) {
    return 0;
  }
  if (s->has_old) {
    (void)tcsetattr(s->fd, TCSANOW, &s->old_tio); // 恢复原来配置
  }
  close(s->fd);
  s->fd = -1;
  return 0;
}