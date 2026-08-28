// src/log.h — 轻量运行期日志。
//
// 目标：同一份诊断既能打到串口（COM / PlatformIO monitor），也能被网页
// 读取（GET /log），这样不接串口也能远程判断屏幕/驱动问题出在哪一步。
//
// 实现：logf() 每次调用同时
//   1) Serial.print 到串口（与之前行为一致）；
//   2) 压入内存环形缓冲（带 millis() 时间戳，最多保留最近 N 条）。
// web 端通过 logFetch(from, ...) 增量拉取，见 main.cpp 的 /log 端点。
//
// 所有 logf 调用都发生在主循环/Web handler 的同一执行上下文，因此
// 环形缓冲无需加锁；为最坏情况（OTA 中断）留了 seq 校验作保底。

#pragma once

#include <Arduino.h>

// 打印一条日志；消息内请自带换行（通常以 \n 结尾）。
// 等价于 Serial.printf + 写入环形缓冲。
void logf(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

// 增量拉取：返回 seq > *from 的所有消息，追加到 out（最多 outCap 字节）。
// 末尾自动补 '\0'；返回实际写入字节数。*from 更新为已取到的最大 seq+1。
// 首次拉取传 *from=0 会返回当前缓冲全部（最多最近 N 条）。
uint16_t logFetch(uint32_t* from, char* out, uint16_t outCap);

// 当前缓冲中最旧可用 seq（用于跳过头 N 条之前的空洞）。
uint32_t logOldestSeq();

// 清空缓冲。
void logClear();
