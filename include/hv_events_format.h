/*
 * Copyright 2025 ShiMetaPi
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0 
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#ifndef HV_EVENTS_FORMAT_H
#define HV_EVENTS_FORMAT_H

#include <cstdint>
#include <vector>
#include <metavision/sdk/base/utils/timestamp.h>
#include <metavision/sdk/base/events/event_cd.h>

// 优化的事件编码格式：使用64位来存储事件
// timestamp : 43位 (支持更长的时间戳)
// x坐标    : 10位 (支持0-1023像素)
// y坐标    : 10位 (支持0-1023像素)
// 极性     : 1位  (0或1)
using HVEventsFormat = std::uint64_t;

// 位字段定义
constexpr int HV_TS_BITS = 43;
constexpr int HV_X_BITS  = 10;
constexpr int HV_Y_BITS  = 10;
constexpr int HV_P_BITS  = 1;

// 掩码定义（优化的位运算）
constexpr uint64_t HV_TS_MASK = (1ULL << HV_TS_BITS) - 1;  // 0x7FFFFFFFFFF
constexpr uint64_t HV_X_MASK_SHIFTED = (1ULL << HV_X_BITS) - 1;  // 0x3FF
constexpr uint64_t HV_Y_MASK_SHIFTED = (1ULL << HV_Y_BITS) - 1;  // 0x3FF
constexpr uint64_t HV_P_MASK_SHIFTED = 1ULL;  // 0x1

// 解码掩码
constexpr uint64_t HV_X_MASK = HV_X_MASK_SHIFTED << HV_TS_BITS;
constexpr uint64_t HV_Y_MASK = HV_Y_MASK_SHIFTED << (HV_TS_BITS + HV_X_BITS);
constexpr uint64_t HV_P_MASK = 1ULL << (HV_TS_BITS + HV_X_BITS + HV_Y_BITS);

// 魔数，用于检验文件格式
constexpr uint32_t HV_RAW_MAGIC = 0x48565241;  // "HVRA"

// 文件头结构
struct HVRawHeader {
    uint32_t magic;           // 魔数
    uint32_t version;         // 版本号
    uint32_t width;           // 图像宽度
    uint32_t height;          // 图像高度
    uint64_t num_events;      // 事件总数
    uint64_t start_timestamp; // 起始时间戳
    uint64_t end_timestamp;   // 结束时间戳
    char reserved[32];        // 预留空间
};

/**
 * 优化的事件编码函数
 * @param encoded_ev 编码后的事件
 * @param x X坐标
 * @param y Y坐标
 * @param p 极性 (0或1)
 * @param t 时间戳
 */
inline void encode_hv_event(HVEventsFormat &encoded_ev, 
                           unsigned short x, unsigned short y, 
                           short p, Metavision::timestamp t) {
    encoded_ev = (t & HV_TS_MASK) | 
                ((static_cast<uint64_t>(x) & HV_X_MASK_SHIFTED) << HV_TS_BITS) | 
                ((static_cast<uint64_t>(y) & HV_Y_MASK_SHIFTED) << (HV_TS_BITS + HV_X_BITS)) |
                ((static_cast<uint64_t>(p) & HV_P_MASK_SHIFTED) << (HV_TS_BITS + HV_X_BITS + HV_Y_BITS));
}

/**
 * 优化的事件解码函数
 * @param encoded_ev 编码的事件
 * @param ev 解码后的EventCD对象
 * @param t_shift 时间偏移（可选）
 */
inline void decode_hv_event(HVEventsFormat encoded_ev, 
                           Metavision::EventCD &ev, 
                           Metavision::timestamp t_shift = 0) {
    ev.t = (encoded_ev & HV_TS_MASK) - t_shift;
    ev.x = static_cast<unsigned short>((encoded_ev & HV_X_MASK) >> HV_TS_BITS);
    ev.y = static_cast<unsigned short>((encoded_ev & HV_Y_MASK) >> (HV_TS_BITS + HV_X_BITS));
    ev.p = static_cast<short>((encoded_ev & HV_P_MASK) >> (HV_TS_BITS + HV_X_BITS + HV_Y_BITS));
}

/**
 * 批量编码事件
 * @param events EventCD事件向量
 * @param encoded_events 编码后的事件向量
 */
inline void encode_hv_events_batch(const std::vector<Metavision::EventCD>& events,
                                  std::vector<HVEventsFormat>& encoded_events) {
    encoded_events.resize(events.size());
    for (size_t i = 0; i < events.size(); ++i) {
        encode_hv_event(encoded_events[i], events[i].x, events[i].y, events[i].p, events[i].t);
    }
}

/**
 * 批量解码事件
 * @param encoded_events 编码的事件向量
 * @param events 解码后的EventCD事件向量
 * @param t_shift 时间偏移（可选）
 */
inline void decode_hv_events_batch(const std::vector<HVEventsFormat>& encoded_events,
                                  std::vector<Metavision::EventCD>& events,
                                  Metavision::timestamp t_shift = 0) {
    events.resize(encoded_events.size());
    for (size_t i = 0; i < encoded_events.size(); ++i) {
        decode_hv_event(encoded_events[i], events[i], t_shift);
    }
}

#endif // HV_EVENTS_FORMAT_H