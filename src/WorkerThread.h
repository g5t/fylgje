// Copyright (C) 2023-2025 European Spallation Source, ERIC. See LICENSE file
//===----------------------------------------------------------------------===//
///
/// \file WorkerThread.h
///
/// \brief main consumer loop for ar51consumer
//===----------------------------------------------------------------------===//

#pragma once

#include "QEventManager.h"
#include "ESSConsumer.h"
#include "KafkaConfig.h"
#include "Configuration.h"
#include "Time.h"
#include <QThread>
#include <utility>

class WorkerThread : public QThread {
  Q_OBJECT

public:
  using data_t = bifrost::data::Q::EventManager;
  using consumer_t = ESSConsumer<data_t>;
  using ptr_t = std::shared_ptr<data_t>;
  WorkerThread(ptr_t data, Configuration & Config): consumer(std::move(data), Config) {};

  /// \brief thread main loop
  void run() override {
    qDebug("Entering main consumer loop\n");
    consumer.run();
  }

  /// \brief Kafka consumer
  consumer_t consumer;

  void consumeFrom(kafka::time::milliseconds from) {
    consumer.consumeFrom(from);
  }
  void consumeUntil(kafka::time::milliseconds to) {
    consumer.consumeUntil(to);
  };
  void consumeForever() {
    consumer.consumeForever();
  }
  void consumeAll() {
    consumer.consumeAll();
  };

  [[nodiscard]] auto message_count() const {
    return consumer.message_count();
  }
  [[nodiscard]] auto event_count() const {
    return consumer.event_count();
  }

signals:
  /// \brief this signal is 'emitted' when there is new data
  //TODO work out if this is actually needed
  void resultReady();
};
