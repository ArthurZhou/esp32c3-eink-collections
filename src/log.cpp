// src/log.cpp — logf() 实现：串口 + 内存环形缓冲。

#include "log.h"

#include <stdarg.h>
#include <string.h>

namespace {
constexpr uint16_t kSlots  = 256;   // 环形槽数（最多保留最近 256 条）
constexpr uint8_t  kMsgLen = 96;    // 每条消息最大长度（不含时间戳前缀）

struct Entry {
  uint32_t seq;   // 全局单调序号（可超过 kSlots）
  uint32_t t;     // millis() 时间戳
  char     msg[kMsgLen];
};

Entry    s_buf[kSlots];
uint32_t s_head = 0;   // 下一个写入槽
uint32_t s_seq  = 0;   // 下一条序号
}  // namespace

void logf(const char* fmt, ...) {
  char tmp[kMsgLen];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(tmp, sizeof(tmp), fmt, ap);
  va_end(ap);
  tmp[sizeof(tmp) - 1] = '\0';

  // 串口：原样输出（保持与 monitor 兼容）。
  Serial.print(tmp);

  // 环形缓冲。
  Entry& e = s_buf[s_head % kSlots];
  e.seq = s_seq;
  e.t   = millis();
  strncpy(e.msg, tmp, kMsgLen - 1);
  e.msg[kMsgLen - 1] = '\0';
  s_head++;
  s_seq++;
}

uint32_t logOldestSeq() {
  return s_seq > kSlots ? s_seq - kSlots : 0;
}

uint16_t logFetch(uint32_t* from, char* out, uint16_t outCap) {
  if (!out || outCap == 0) return 0;
  uint32_t start = *from;
  uint32_t oldest = logOldestSeq();
  if (start < oldest) start = oldest;   // 已被覆盖的旧消息跳过

  uint16_t used = 0;
  for (uint32_t seq = start; seq < s_seq; seq++) {
    const Entry& e = s_buf[seq % kSlots];
    if (e.seq != seq) continue;         // 槽被覆盖 → 保底跳过
    int n = snprintf(out + used, outCap - used, "[%6lu] %s",
                     (unsigned long)e.t, e.msg);
    if (n < 0) break;
    if (used + (uint16_t)n >= outCap) break;  // 空间不足，留给下次拉取
    used += (uint16_t)n;
  }
  *from = s_seq;
  return used;
}

void logClear() { s_head = 0; s_seq = 0; }
