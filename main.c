#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "parser.h"

static void print_frame(const frame_t *f) {
  printf("[frame] seq=%u  type=0x%02X", (unsigned)f->seq, (unsigned)f->type);

  if (f->type == 0x01u && f->len == 4u) {
    unsigned t = ((unsigned)f->data[0] << 8) | f->data[1];
    unsigned h = ((unsigned)f->data[2] << 8) | f->data[3];
    printf("  temp=%u.%u  humi=%u.%u", t / 10u, t % 10u, h / 10u, h % 10u);
  } else if (f->type == 0x02u) {
    printf("  heartbeat");
  } else {
    printf("  len=%u", (unsigned)f->len);
  }

  printf("\n");
}

int main(int argc, char *argv[]) {
  const char *path = NULL;
  int fd;
  int exit_code = 0;
  int incomplete;
  parser_t p;
  unsigned char buf[4096];
  ssize_t n;
  ssize_t i;

  if (argc > 2) {
    fprintf(stderr, "用法: %s [文件]\n", argv[0]);
    return 1;
  }
  if (argc == 2) {
    path = argv[1];
  }

  if (path != NULL) {
    fd = open(path, O_RDONLY);
    if (fd < 0) {
      fprintf(stderr, "parser: %s: %s\n", path, strerror(errno));
      return 1;
    }
  } else {
    fd = STDIN_FILENO;
  }

  parser_init(&p);

  while ((n = read(fd, buf, sizeof buf)) > 0) {
    for (i = 0; i < n; i++) {
      switch (parser_feed(&p, buf[i])) {
      case EV_FRAME:
        print_frame(&p.frame);
        break;
      case EV_ERR_LEN:
        printf("[error] invalid len %u\n", (unsigned)p.bad_len);
        break;
      case EV_ERR_CRC:
        printf("[error] seq=%u  crc mismatch\n", (unsigned)p.frame.seq);
        break;
      case EV_NONE:
      default:
        break;
      }
    }
  }

  if (n < 0) {
    fprintf(stderr, "parser: %s: read: %s\n", path ? path : "<stdin>",
            strerror(errno));
    exit_code = 1;
  }

  incomplete = parser_finish(&p);
  if (incomplete) {
    printf("[error] incomplete frame at EOF\n");
  }

  printf("----\n");
  printf("ok=%u  crc_err=%u  invalid_len=%u  incomplete=%d  noise=%u\n",
         (unsigned)p.ok, (unsigned)p.crc_err, (unsigned)p.invalid_len,
         incomplete, (unsigned)p.noise);

  if (path != NULL) {
    close(fd);
  }
  return exit_code;
}