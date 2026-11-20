/* 打开 POSIX 接口：-std=c11 会定义 __STRICT_ANSI__，glibc 默认不暴露
 * struct sigaction / sigaction() / sigemptyset()，必须显式声明特性宏 */
#define _DEFAULT_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "parser.h"
#include "serial.h"

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

static void handle_event(parser_t *p, parser_event_t ev) {
  switch (ev) {
  case EV_FRAME:
    print_frame(&p->frame);
    break;
  case EV_ERR_LEN:
    printf("[error] invalid len %u\n", (unsigned)p->bad_len);
    break;
  case EV_ERR_CRC:
    printf("[error] seq=%u  crc mismatch\n", (unsigned)p->frame.seq);
    break;
  case EV_NONE:
  default:
    break;
  }
}

static volatile sig_atomic_t g_stop = 0;

static void on_signal(int signo) {
  (void)signo;
  g_stop = 1;
}

static int install_signals(void) {
  struct sigaction sa;

  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = on_signal;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;

  if (sigaction(SIGINT, &sa, NULL) != 0) {
    return -1;
  }
  if (sigaction(SIGTERM, &sa, NULL) != 0) {
    return -1;
  }
  return 0;
}

/* ---------------- 数据源抽象 ---------------- */
typedef ssize_t (*read_fn_t)(void *ctx, uint8_t *buf, size_t len);

typedef struct {
  read_fn_t fn;
  void *ctx;
  int zero_means_eof; /* 1 = 文件；0 = 串口（0 表示超时） */
} source_t;

static ssize_t file_read(void *ctx, uint8_t *buf, size_t len) {
  int fd = *(const int *)ctx;
  return read(fd, buf, len);
}

static ssize_t serial_source_read(void *ctx, uint8_t *buf, size_t len) {
  return serial_read((serial_t *)ctx, buf, len);
}

static void usage(const char *prog) {
  fprintf(stderr,
          "用法:\n"
          "  %s -d <设备> [-b <波特率>]   从串口读\n"
          "  %s -f <文件>                 从文件读\n"
          "  %s                           从 stdin 读\n"
          "  默认波特率: 115200\n",
          prog, prog, prog);
}

static int parse_int(const char *s, int *out) {
  char *end = NULL;
  long v;

  errno = 0;
  v = strtol(s, &end, 10);
  if (errno != 0 || end == s || *end != '\0' || v <= 0 || v > 10000000L) {
    return -1;
  }
  *out = (int)v;
  return 0;
}

int main(int argc, char *argv[]) {
  const char *file_path = NULL;
  const char *dev_path = NULL;
  int baud = 115200;

  int fd = -1;
  serial_t ser;
  source_t src;

  parser_t p;
  uint8_t buf[4096];
  ssize_t n;
  size_t i;
  int k;
  int exit_code = 0;
  int incomplete;
  unsigned long io_bytes = 0, io_reads = 0, io_timeouts = 0, io_errors = 0,
                io_eintr = 0;

  /* ---- 参数 ---- */
  for (k = 1; k < argc; k++) {
    if (strcmp(argv[k], "-f") == 0 && k + 1 < argc) {
      file_path = argv[++k];
    } else if (strcmp(argv[k], "-d") == 0 && k + 1 < argc) {
      dev_path = argv[++k];
    } else if (strcmp(argv[k], "-b") == 0 && k + 1 < argc) {
      if (parse_int(argv[++k], &baud) != 0) {
        fprintf(stderr, "错误: 非法波特率 '%s'\n", argv[k]);
        return 1;
      }
    } else {
      usage(argv[0]);
      return 1;
    }
  }

  if (file_path != NULL && dev_path != NULL) {
    fprintf(stderr, "错误: -f 和 -d 不能同时使用\n");
    return 1;
  }

  if (install_signals() != 0) {
    perror("sigaction");
    return 1;
  }

  /* ---- 选择数据源 ---- */
  if (dev_path != NULL) {
    if (serial_open(&ser, dev_path, baud) != 0) {
      fprintf(stderr, "sergw: %s: %s\n", dev_path, strerror(errno));
      return 1;
    }
    src.fn = serial_source_read;
    src.ctx = &ser;
    src.zero_means_eof = 0; /* 串口：0 = 超时 */
  } else {
    if (file_path != NULL) {
      fd = open(file_path, O_RDONLY);
      if (fd < 0) {
        fprintf(stderr, "sergw: %s: %s\n", file_path, strerror(errno));
        return 1;
      }
    } else {
      fd = STDIN_FILENO;
    }
    src.fn = file_read;
    src.ctx = &fd;
    src.zero_means_eof = 1; /* 文件：0 = EOF */
  }

  /* ---- 主循环 ---- */
  parser_init(&p);

  while (!g_stop) {
    n = src.fn(src.ctx, buf, sizeof(buf));
    io_reads++;

    if (n < 0) {
      if (errno == EINTR) {
        io_eintr++;
        continue; /* 被信号打断，回去看 g_stop */
      }
      fprintf(stderr, "sergw: %s: read: %s\n",
              dev_path ? dev_path : (file_path ? file_path : "<stdin>"),
              strerror(errno));
      exit_code = 1;
      io_errors++;
      break;
    }

    if (n == 0) {
      if (src.zero_means_eof) {
        break; /* 文件读完，正常结束 */
      }
      io_timeouts++;
      continue; /* 串口超时，继续等 */
    }
    io_bytes += (unsigned long)n;
    for (i = 0; i < (size_t)n; i++) {
      handle_event(&p, parser_feed(&p, buf[i]));
    }
  }

  /* ---- 收尾 ---- */
  incomplete = parser_finish(&p);
  if (incomplete) {
    printf("[error] incomplete frame at EOF\n");
  }

  printf("----\n");
  printf("ok=%u  crc_err=%u  invalid_len=%u  incomplete=%d  noise=%u\n",
         (unsigned)p.ok, (unsigned)p.crc_err, (unsigned)p.invalid_len,
         incomplete, (unsigned)p.noise);
  if (dev_path != NULL) {
    printf("io: bytes=%lu  reads=%lu  timeouts=%lu  eintr=%lu  errors=%lu\n",
           io_bytes, io_reads, io_timeouts, io_eintr, io_errors);
  }
  if (dev_path != NULL) {
    serial_close(&ser);
  } else if (fd >= 0 && fd != STDIN_FILENO) {
    close(fd);
  }

  return exit_code;
}
