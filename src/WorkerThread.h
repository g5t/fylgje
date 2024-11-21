// Copyright (C) 2023 European Spallation Source, ERIC. See LICENSE file
//===----------------------------------------------------------------------===//
///
/// \file WorkerThread.h
///
/// \brief main consumer loop for ar51consumer
//===----------------------------------------------------------------------===//

#pragma once

#include "ESSConsumer.h"
#include "KafkaConfig.h"
#include "Configuration.h"
#include <QThread>
#include <utility>

class WorkerThread : public QThread {
  Q_OBJECT

public:
  WorkerThread(ESSConsumer::data_t * data, Configuration & Config):
  configuration(Config) {
    KafkaConfig kcfg(Config.KafkaConfigFile);
    Consumer = new ESSConsumer(data, configuration, kcfg.CfgParms);
  };

  /// \brief thread main loop
  void run() override;

  /// \brief Kafka consumer
  ESSConsumer *Consumer;

private:
  Configuration &configuration;

signals:
  /// \brief this signal is 'emitted' when there is new data
  void resultReady();

};
