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
#ifndef HV_EVT2_CODEC_H
#define HV_EVT2_CODEC_H

#include <cstdint>
#include <vector>
#include <string>
#include <metavision/sdk/base/utils/timestamp.h>
#include <metavision/sdk/base/events/event_cd.h>

namespace hv {
namespace evt2 {

/// @brief Event types for EVT2 format
enum class EventTypes : uint8_t {
    CD_OFF        = 0x00, ///< OFF CD event, decrease in illumination (polarity '0')
    CD_ON         = 0x01, ///< ON CD event, increase in illumination (polarity '1')
    EVT_TIME_HIGH = 0x08, ///< Encodes the higher portion of the timebase (bits 33..6)
    EXT_TRIGGER   = 0x0A, ///< External trigger output
};

/// @brief EVT2 raw events are 32-bit words
struct RawEvent {
    unsigned int pad : 28;  ///< Padding
    unsigned int type : 4;  ///< Event type
};

/// @brief Time High event structure
struct RawEventTime {
    unsigned int timestamp : 28; ///< Most significant bits of the event timestamp (bits 33..6)
    unsigned int type : 4;       ///< Event type: EventTypes::EVT_TIME_HIGH
};

/// @brief CD event structure
struct RawEventCD {
    unsigned int y : 11;        ///< Pixel Y coordinate
    unsigned int x : 11;        ///< Pixel X coordinate
    unsigned int timestamp : 6; ///< Least significant bits of the event timestamp (bits 5..0)
    unsigned int type : 4;      ///< Event type: EventTypes::CD_OFF or EventTypes::CD_ON
};

/// @brief External trigger event structure
struct RawEventExtTrigger {
    unsigned int value : 1;     ///< Trigger current value (edge polarity)
    unsigned int unused2 : 7;   ///< Unused bits
    unsigned int id : 5;        ///< Trigger channel ID
    unsigned int unused1 : 9;   ///< Unused bits
    unsigned int timestamp : 6; ///< Least significant bits of the event timestamp (bits 5..0)
    unsigned int type : 4;      ///< Event type: EventTypes::EXT_TRIGGER
};

using Timestamp = uint64_t; ///< Type for timestamp, in microseconds

/// @brief EVT2 file header structure
struct EVT2Header {
    std::string format_line;    ///< Format line with sensor geometry
    std::string integrator;     ///< Integrator name
    std::string date;          ///< Creation date
    uint32_t width;            ///< Sensor width
    uint32_t height;           ///< Sensor height
    uint64_t start_timestamp;  ///< Start timestamp in microseconds
};

/// @brief CD event encoder for EVT2 format
class EventCDEncoder {
public:
    unsigned short x;  ///< X coordinate
    unsigned short y;  ///< Y coordinate
    short p;          ///< Polarity (0 or 1)
    Timestamp t;      ///< Timestamp in microseconds
    
    /// @brief Encodes CD event to EVT2 format
    /// @param raw_event Pointer to the raw event data
    void encode(RawEvent* raw_event);
    
    /// @brief Sets event data
    /// @param x_coord X coordinate
    /// @param y_coord Y coordinate
    /// @param polarity Polarity (0 or 1)
    /// @param timestamp Timestamp in microseconds
    void setEvent(unsigned short x_coord, unsigned short y_coord, short polarity, Timestamp timestamp);
};

/// @brief External trigger encoder for EVT2 format
class EventTriggerEncoder {
public:
    short p;          ///< Polarity
    Timestamp t;      ///< Timestamp in microseconds
    short id;         ///< Trigger ID
    
    /// @brief Encodes trigger event to EVT2 format
    /// @param raw_event Pointer to the raw event data
    void encode(RawEvent* raw_event);
    
    /// @brief Sets trigger event data
    /// @param polarity Polarity
    /// @param trigger_id Trigger ID
    /// @param timestamp Timestamp in microseconds
    void setEvent(short polarity, short trigger_id, Timestamp timestamp);
};

/// @brief Time High encoder for EVT2 format
class EventTimeEncoder {
public:
    /// @brief Constructor
    /// @param base Time (in us) of the first event to encode
    explicit EventTimeEncoder(Timestamp base);
    
    /// @brief Encodes Time High event
    /// @param raw_event Pointer to the raw event data
    void encode(RawEvent* raw_event);
    
    /// @brief Gets next time high value
    /// @return Next time high timestamp
    Timestamp getNextTimeHigh() const { return th; }
    
    /// @brief Resets the time encoder to a new base timestamp
    /// @param base New base timestamp
    void reset(Timestamp base = 0);
    
private:
    Timestamp th;  ///< Next Time High to encode
    
    static constexpr char N_LOWER_BITS_TH = 6;
    static constexpr unsigned int REDUNDANCY_FACTOR = 4;
    static constexpr Timestamp TH_STEP = (1ul << N_LOWER_BITS_TH);
    static constexpr Timestamp TH_NEXT_STEP = TH_STEP / REDUNDANCY_FACTOR;
};

/// @brief EVT2 decoder class
class EVT2Decoder {
public:
    /// @brief Constructor
    EVT2Decoder();
    
    /// @brief Decodes a raw event buffer
    /// @param buffer Pointer to raw event buffer
    /// @param buffer_size Size of buffer in bytes
    /// @param cd_events Output vector for CD events
    /// @param trigger_events Output vector for trigger events (optional)
    /// @return Number of events decoded
    size_t decode(const uint8_t* buffer, size_t buffer_size, 
                  std::vector<Metavision::EventCD>& cd_events,
                  std::vector<std::tuple<short, short, Timestamp>>* trigger_events = nullptr);
    
    /// @brief Resets decoder state
    void reset();
    
    /// @brief Gets current time base
    /// @return Current time base
    Timestamp getCurrentTimeBase() const { return current_time_base_; }
    
private:
    Timestamp current_time_base_;        ///< Current time base
    bool first_time_base_set_;          ///< Whether first time base is set
    unsigned int n_time_high_loop_;     ///< Counter of time high loops
    
    /// @brief Processes a single raw event
    /// @param raw_event Pointer to raw event
    /// @param cd_events Output vector for CD events
    /// @param trigger_events Output vector for trigger events
    void processEvent(const RawEvent* raw_event,
                     std::vector<Metavision::EventCD>& cd_events,
                     std::vector<std::tuple<short, short, Timestamp>>* trigger_events);
};

/// @brief Utility functions for EVT2 format
namespace utils {
    /// @brief Parses EVT2 header from file stream
    /// @param header_lines Vector of header lines
    /// @param header Output header structure
    /// @return True if parsing successful
    bool parseEVT2Header(const std::vector<std::string>& header_lines, EVT2Header& header);
    
    /// @brief Generates EVT2 header lines
    /// @param header EVT2 header structure
    /// @return Vector of header lines
    std::vector<std::string> generateEVT2Header(const EVT2Header& header);
    
    /// @brief Generates EVT2 header lines
    /// @param width Sensor width
    /// @param height Sensor height
    /// @param integrator Integrator name
    /// @return Vector of header lines
    std::vector<std::string> generateEVT2Header(uint32_t width, uint32_t height, 
                                               const std::string& integrator = "Prophesee");
    
    /// @brief Converts Metavision::EventCD to EVT2 format
    /// @param events Input EventCD vector
    /// @param raw_data Output raw data as bytes
    /// @param time_encoder Time encoder for generating time high events
    /// @return Number of events converted
    size_t convertToEVT2(const std::vector<Metavision::EventCD>& events,
                         std::vector<uint8_t>& raw_data,
                         EventTimeEncoder& time_encoder);
}

} // namespace evt2
} // namespace hv

#endif // HV_EVT2_CODEC_H