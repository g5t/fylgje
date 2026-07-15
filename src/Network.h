// Copyright (C) 2025 European Spallation Source, ERIC. See LICENSE file
//===----------------------------------------------------------------------===//
///
/// \file
///
/// \brief Network data structures for the ESS readout system and interface details for Kafka service
//===----------------------------------------------------------------------===//
#pragma once
#include "Configuration.h"
#include "ar51_readout_data_generated.h"
#include <librdkafka/rdkafkacpp.h>
#include "EventManager.h"
#include "KafkaConfig.h"
#include "Time.h"

namespace ess::network {
  enum Status {Continue, Update, Halt};
  enum Start {Beginning, End, Time};

  struct PulseTimes {
    uint32_t PulseHigh;
    uint32_t PulseLow;
    uint32_t PrevPulseHigh;
    uint32_t PrevPulseLow;
  };

  // Header common to all ESS readout data
  // Reviewed ICD (version 2) packet header version 0
  // ownCloud: https://project.esss.dk/nextcloud/index.php/s/DWNer23727TiI1x
  struct PacketHeaderV0 {
    uint8_t Padding0;
    uint8_t Version;
    uint32_t CookieAndType;
    uint16_t TotalLength;
    uint8_t OutputQueue;
    uint8_t TimeSource;
    uint32_t PulseHigh;
    uint32_t PulseLow;
    uint32_t PrevPulseHigh;
    uint32_t PrevPulseLow;
    uint32_t SeqNum;
  } __attribute__((packed));

  // Header common to all ESS readout data
  // Reviewed ICD (version 2) packet header version 0
  // ownCloud: https://project.esss.dk/nextcloud/index.php/s/DWNer23727TiI1x
  struct PacketHeaderV1 {
    uint8_t Padding0;
    uint8_t Version;
    uint32_t CookieAndType;
    uint16_t TotalLength;
    uint8_t OutputQueue;
    uint8_t TimeSource;
    uint32_t PulseHigh;
    uint32_t PulseLow;
    uint32_t PrevPulseHigh;
    uint32_t PrevPulseLow;
    uint32_t SeqNum;
    uint16_t CMACPadd; // these two bytes are the reason for shifting the data pointer in AR51 message handling
  } __attribute__((packed));

  struct CAENReadout {
    uint8_t Fiber;
    uint8_t FEN;
    uint16_t Length;
    uint32_t HighTime;
    uint32_t LowTime;
    uint8_t Flags_OM;
    uint8_t Group;
    uint16_t Unused;
    int16_t A;
    int16_t B;
    int16_t C;
    int16_t D;
  } __attribute__((packed));


  // Data format for VMM3a based readout
  struct VMM3aReadout {
    uint8_t Fiber;
    uint8_t FEN;
    uint16_t Length;
    uint32_t TimeHi;
    uint32_t TimeLo;
    uint16_t BC;
    uint16_t OTADC;
    uint8_t GEO;
    uint8_t TDC;
    uint8_t VMM;
    uint8_t Channel;
  } __attribute__((packed));


  // Data format for CDT Readout
  struct CDTReadout {
    uint8_t Fiber;
    uint8_t FEN;
    uint16_t Length;
    uint32_t TimeHi;
    uint32_t TimeLo;
    uint8_t OM;
    uint8_t UnitId;
    uint8_t Cathode;
    uint8_t Anode;
  } __attribute__((packed));


/// \brief Tracks which assigned partitions have finished (passed the end of
///        the requested time window) so consumption halts only when every
///        partition is done, not on the first EOF/late message.
///
/// A partition at EOF is only *caught up*, not necessarily finished: when the
/// window end lies in the future, more in-window messages may still arrive, so
/// EOF is recorded separately and promoted to done once the window has closed.
  class PartitionWindow {
  public:
    /// \brief (Re)build the tracked set from the consumer's current assignment
    void reset(RdKafka::KafkaConsumer * consumer);
    void mark_done(int32_t partition) { states_[partition].done = true; }
    void mark_eof(int32_t partition) { states_[partition].at_eof = true; }
    void clear_eof(int32_t partition) { states_[partition].at_eof = false; }
    /// \brief promote caught-up partitions to done; call once the window has
    ///        closed (any message yet to come is timestamped past the end)
    void finish_eof_partitions() {
      for (auto & [partition, state]: states_) if (state.at_eof) state.done = true;
    }
    [[nodiscard]] bool is_done(int32_t partition) const {
      const auto it = states_.find(partition);
      return it != states_.end() && it->second.done;
    }
    /// \return true only when partitions are tracked and all are finished
    [[nodiscard]] bool all_done() const {
      if (states_.empty()) return false;
      for (const auto & [partition, state]: states_) if (!state.done) return false;
      return true;
    }
  private:
    struct State {
      bool done{false};
      bool at_eof{false};
    };
    std::map<int32_t, State> states_;
  };

/// \brief setup librdkafka parameters for Broker and Topic
  RdKafka::KafkaConsumer * subscribe_topic(const Configuration & Config, const std::vector<std::pair<std::string, std::string>> & kafkaConfig);

  int64_t consume_all(Configuration & configuration, RdKafka::KafkaConsumer * consumer);
  int64_t consume_from(Configuration & configuration, RdKafka::KafkaConsumer * consumer, std::optional<kafka::time::milliseconds> since_epoch);
  int64_t consume_until(Configuration & configuration, RdKafka::KafkaConsumer * consumer, int64_t early, std::optional<kafka::time::milliseconds> since_epoch);

  void set_consumer_offset(Configuration & configuration, RdKafka::KafkaConsumer * consumer, Start start, int64_t ms_since_utc_epoch);

  using TypeCallback = std::function<uint32_t(const uint8_t*, int, const PulseTimes*)>;
  using CallbacksType = std::map<uint32_t, TypeCallback>;

/// \brief initial checks for kafka error messages
/// \param consumer needed to pause a partition that has passed the window end
/// \param window per-partition done-state; Halt is returned only when all
///        assigned partitions are done (or on a hard error)
/// \return Update if data is processed, Continue if no data, or Halt if finished or errored
  std::tuple<Status, uint32_t> handle_message(RdKafka::KafkaConsumer * consumer, int64_t early, int64_t late, RdKafka::Message * message, CallbacksType & callbacks, PartitionWindow & window);

/// \brief Main processing function for AR51 data
/// \return number of processed events _in_ the message
  std::tuple<uint32_t, uint32_t> process_AR51_data(const RdKafka::Message * Msg, CallbacksType & callbacks);

}
