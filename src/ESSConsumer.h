// Copyright (C) 2023 European Spallation Source, ERIC. See LICENSE file
//===----------------------------------------------------------------------===//
///
/// \file ESSConsumer.h
///
/// \brief Wrapper class for librdkafka
///
/// Sets up the kafka consumer and handles binning of event pixel ids
//===----------------------------------------------------------------------===//

#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <optional>
#include "Configuration.h"
#include "ar51_readout_data_generated.h"
#include <librdkafka/rdkafkacpp.h>
#include "EventManager.h"
#include "KafkaConfig.h"
#include "Time.h"
#include "Network.h"

std::tuple<double, uint32_t, uint32_t> frame_time(const ess::network::PulseTimes * header, uint32_t high, uint32_t low);


template<class T>
class ESSConsumer {
public:
  using kafka_config_t = std::vector<std::pair<std::string, std::string>>;
  using kafka_time_t = kafka::time::milliseconds;
  using data_t = T;
  using ptr_t = std::shared_ptr<T>;
  using Status = ess::network::Status;
  using Start = ess::network::Start;
private:
  Configuration & configuration;
  RdKafka::KafkaConsumer * mConsumer {nullptr};
  int64_t earliest_timestamp{-1}, latest_timestamp{-1};
  ptr_t histograms;
  /// \brief loadable Kafka-specific configuration
  kafka_config_t kafkaConfig;
  int64_t total_ar51{0};
  int64_t good_readout_count{0};
  int64_t bad_message_count{0};
  int64_t bad_readout_count{0};
  ess::network::PartitionWindow window;
  std::chrono::milliseconds maximum_idle{30'000};
  const std::atomic<bool> * stop_flag{nullptr};
  std::chrono::milliseconds callback_interval{0};
  std::function<void()> periodic_callback;
  std::chrono::milliseconds progress_interval{0};
  std::function<void()> progress_callback;
  std::function<void()> progress_line_break;
  std::chrono::steady_clock::duration consume_time{0};
  std::chrono::steady_clock::duration write_time{0};
  int64_t flush_count{0};
  std::optional<std::chrono::steady_clock::time_point> last_write_at;

  ess::network::CallbacksType callbacks{
    {3, std::bind(&ESSConsumer::parseCAENData, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3)},
    {4, std::bind(&ESSConsumer::parseVMM3aData, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3)},
    {6, std::bind(&ESSConsumer::parseCDTData, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3)}
  };

public:
  /// \brief Construct a consumer of AR51 messages which seeks out a finite time window
  ///
  /// \param data The structure into which any found readout packets are inserted
  /// \param Config A combined Kafka and ESS configuration structure
  /// \param from The earliest time to look for messages in the Kafka topic stream
  /// \param to The latest time to look for messages in the Kafka topic stream
  ESSConsumer(ptr_t data, Configuration & Config, kafka::time::milliseconds from, kafka::time::milliseconds to):
  configuration{Config}, histograms{data} {
    setup();
    consumeFrom(from);
    consumeUntil(to);
  }

  /// \brief Construct a consumer of AR51 messages which collects messages from a specified time without end
  ///
  /// \param data The structure into which any found readout packets are inserted
  /// \param Config A combined Kafka and ESS configuration structure
  /// \param from The earliest time to look for messages in the Kafka topic stream
  ESSConsumer(ptr_t data, Configuration & Config, kafka::time::milliseconds from):
      configuration{Config}, histograms{data} {
    setup();
    consumeFrom(from);
  }

  /// \brief Construct a consumer of AR51 messages which collects messages from the current time without end
  ///
  /// \param data The structure into which any found readout packets are inserted
  /// \param Config A combined Kafka and ESS configuration structure
  ESSConsumer(ptr_t data, Configuration & Config):
      configuration{Config}, histograms{data} {
    using namespace kafka::time;
    setup();
    consumeFrom(time_t_to_milliseconds(now_to_time_t()));
  };


  /// \brief wrapper function for librdkafka consumer
  RdKafka::Message *consume() const { return mConsumer->consume(1000); }


  [[maybe_unused]] void consumeAll() const {
    ess::network::consume_all(configuration, mConsumer);
  }
  void consumeForever() {
    latest_timestamp = -1;
  }
  void consumeFrom(const std::optional<kafka_time_t> ms_since_utc_epoch){
    earliest_timestamp = ess::network::consume_from(configuration, mConsumer, ms_since_utc_epoch);
    window.reset(mConsumer);
  }
  void consumeUntil(const std::optional<kafka_time_t> ms_since_utc_epoch){
    latest_timestamp = ess::network::consume_until(configuration, mConsumer, earliest_timestamp, ms_since_utc_epoch);
    window.reset(mConsumer);
  }

  /// \brief bound on how long run() may go without any message or EOF once
  ///        the requested window has closed, before giving up (safety net;
  ///        all-partitions-done is the normal exit). While the window end
  ///        lies in the future the consumer waits indefinitely for new data.
  void setMaximumIdle(std::chrono::milliseconds ms) { maximum_idle = ms; }

  /// \brief run() halts (at ~1 s poll granularity) once the pointed-to flag
  ///        becomes true; intended to be set from a signal handler
  void setStopFlag(const std::atomic<bool> * flag) { stop_flag = flag; }

  /// \brief run() invokes the callback whenever the interval has elapsed
  ///        (checked once per ~1 s poll). The callback runs on run()'s thread,
  ///        the same thread that fills the data sink in CLI use; any other
  ///        arrangement needs its own synchronisation.
  void setPeriodicCallback(std::chrono::milliseconds interval, std::function<void()> callback) {
    callback_interval = interval;
    periodic_callback = std::move(callback);
  }

  /// \brief run() invokes the callback whenever the interval has elapsed
  ///        (checked once per ~1 s poll, which is the effective floor), on
  ///        run()'s thread; intended for progress reporting. If the callback
  ///        maintains an unterminated display line (e.g. a '\r'-rewritten
  ///        progress line), pass a line_break that terminates it: run() calls
  ///        it before printing any of its own messages.
  void setProgressCallback(std::chrono::milliseconds interval, std::function<void()> callback,
                           std::function<void()> line_break = {}) {
    progress_interval = interval;
    progress_callback = std::move(callback);
    progress_line_break = std::move(line_break);
  }

  void run(){
    using clock_t = std::chrono::steady_clock;
    Status intent{Status::Continue};
    auto last_activity = clock_t::now();
    auto last_callback = clock_t::now();
    auto last_progress = clock_t::now();
    while (intent != Status::Halt) {
      const auto consume_start = clock_t::now();
      const auto Msg = consume();
      auto [status, ar51s] = ess::network::handle_message(mConsumer, earliest_timestamp, latest_timestamp, Msg, callbacks, window);
      consume_time += clock_t::now() - consume_start;
      total_ar51 += ar51s;
      if (Msg->err() != RdKafka::ERR__TIMED_OUT) {
        last_activity = clock_t::now();
      }
      delete Msg;
      intent = status;
      if (intent != Status::Halt && stop_flag != nullptr && stop_flag->load()) {
        break_progress_line();
        fmt::print("Stop requested; halting consumption\n");
        intent = Status::Halt;
      }
      if (intent != Status::Halt && periodic_callback && callback_interval.count() > 0
          && clock_t::now() - last_callback >= callback_interval) {
        const auto write_start = clock_t::now();
        periodic_callback();
        const auto write_end = clock_t::now();
        write_time += write_end - write_start;
        ++flush_count;
        last_write_at = write_end;
        last_callback = write_end;
      }
      if (intent != Status::Halt && progress_callback && progress_interval.count() > 0
          && clock_t::now() - last_progress >= progress_interval) {
        progress_callback();
        last_progress = clock_t::now();
      }
      if (intent != Status::Halt && latest_timestamp >= 0
          && kafka::time::now_milliseconds().count() >= latest_timestamp) {
        // the window has closed: partitions that are caught up cannot receive
        // in-window data any more, and EOF is not re-emitted while idle
        window.finish_eof_partitions();
        if (window.all_done()) {
          intent = Status::Halt;
        } else if (clock_t::now() - last_activity > maximum_idle) {
          break_progress_line();
          fmt::print("No messages or partition EOFs for {} ms after the window closed; stopping\n", maximum_idle.count());
          intent = Status::Halt;
        }
      }
    }
    break_progress_line();
    fmt::print("Processed {} AR51 messages ({} bad) and {} CAEN readouts ({} bad)\n", total_ar51, bad_message_count, good_readout_count, bad_readout_count);
  }

  /// \brief Parser for CAEN Data from BIFROST
  uint32_t parseCAENData(const uint8_t * Readout, const int Size, const ess::network::PulseTimes * header) {
    uint32_t processed{0};
    int BytesLeft = Size;
    constexpr auto bytes = sizeof(ess::network::CAENReadout);
    while (BytesLeft >= static_cast<int>(bytes)) {
      if (auto * crd = reinterpret_cast<const ess::network::CAENReadout *>(Readout); crd->FEN != 0){
        // FIXME: this is BIFROST specific; other instruments actually use their FEN counter -- remove or make flexible
        printf("FEN %u, Length %u, HighTime %u, LowTime %u, Flags %u, Group %u\n",
               crd->FEN, crd->Length, crd->HighTime, crd->LowTime, crd->Flags_OM, crd->Group);
        ++bad_readout_count;
      } else {
        auto [time, h, l] = frame_time(header, crd->HighTime, crd->LowTime);
        histograms->add(crd->Fiber, crd->Group, crd->A, crd->B, time, h, l);
      }
      BytesLeft -= bytes;
      Readout += bytes;
      ++processed;
    }
    if (BytesLeft) {
      // printf("CAEN readout message has %d extra bytes at the end\n", BytesLeft);
      ++bad_message_count;
    }

    good_readout_count += processed;
    return processed;
  }

  uint32_t parseVMM3aData(const uint8_t * Readout, const int Size, const ess::network::PulseTimes * header) {
    uint32_t processed{0};
    int BytesLeft = Size;
    constexpr auto bytes = sizeof(ess::network::VMM3aReadout);
    while (BytesLeft >= static_cast<int>(bytes)) {
      auto * vmm = reinterpret_cast<const ess::network::VMM3aReadout *>(Readout);
      auto [time, h, l] = frame_time(header, vmm->TimeHi, vmm->TimeLo);

      const auto ring = vmm->Fiber/2;
      const auto fen = vmm->FEN;
      const auto hybrid = vmm->VMM >> 1;
      const auto asic = vmm->VMM & 1;
      const auto channel = vmm->Channel;
      fmt::print("Ring {}, FEN {}, Hybrid {}, ASIC {}, Channel {}, Time {:.6f}, High {}, Low {}\n",
                 ring, fen, hybrid, asic, channel, time, h, l);

      BytesLeft -= bytes;
      Readout += bytes;
      ++processed;
    }
    if (BytesLeft) {
      // printf("VMM3a readout message has %d extra bytes at the end\n", BytesLeft);
      ++bad_message_count;
    }

    good_readout_count += processed;
    return processed;
  }
  uint32_t parseCDTData(const uint8_t * Readout, const int Size, const ess::network::PulseTimes * header) {
    uint32_t processed{0};
    int BytesLeft = Size;
    constexpr auto bytes = sizeof(ess::network::CDTReadout);
    while (BytesLeft >= static_cast<int>(bytes)) {
      auto * cdt = reinterpret_cast<const ess::network::CDTReadout *>(Readout);
      auto [time, h, l] = frame_time(header, cdt->TimeHi, cdt->TimeLo);

      const auto ring = cdt->Fiber/2;
      const auto fen = cdt->FEN;
      const auto cathode = cdt->Cathode;
      const auto anode = cdt->Anode;
      fmt::print("Ring {}, FEN {}, Cathode {}, Anode {}, Time {:.6f}, High {}, Low {}\n",
                 ring, fen, cathode, anode, time, h, l);

      BytesLeft -= bytes;
      Readout += bytes;
      ++processed;
    }
    if (BytesLeft) {
      // printf("CDT readout message has %d extra bytes at the end\n", BytesLeft);
      ++bad_message_count;
    }

    good_readout_count += processed;
    return processed;
  }


  [[nodiscard]] int64_t message_count() const {
    return total_ar51;
  }
  [[nodiscard]] int64_t event_count() const {
    return good_readout_count;
  }

  /// \brief time run() has spent polling Kafka and handling messages
  [[nodiscard]] std::chrono::steady_clock::duration consume_duration() const {
    return consume_time;
  }
  /// \brief time run() has spent inside the periodic (write) callback
  [[nodiscard]] std::chrono::steady_clock::duration write_duration() const {
    return write_time;
  }
  /// \brief number of periodic-callback invocations so far
  [[nodiscard]] int64_t flushes() const {
    return flush_count;
  }
  /// \brief time since the periodic callback last completed; nullopt before the first
  [[nodiscard]] std::optional<std::chrono::steady_clock::duration> since_last_write() const {
    if (!last_write_at.has_value()) {
      return std::nullopt;
    }
    return std::chrono::steady_clock::now() - last_write_at.value();
  }

private:
  void break_progress_line() const {
    if (progress_line_break) {
      progress_line_break();
    }
  }

  void setup() {
    kafkaConfig = KafkaConfig(configuration.KafkaConfigFile).CfgParms;
    mConsumer = ess::network::subscribe_topic(configuration, kafkaConfig);
    assert(mConsumer != nullptr);
    // positioning (offset lookup + assign) happens in consumeFrom
  }

};
