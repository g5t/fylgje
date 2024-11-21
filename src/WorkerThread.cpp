// Copyright (C) 2023 European Spallation Source, ERIC. See LICENSE file
//===----------------------------------------------------------------------===//
///
/// \file WorkerThread.cpp
///
//===----------------------------------------------------------------------===//

#include "WorkerThread.h"

void WorkerThread::run() {
  qDebug("Entering main consumer loop\n");
  ESSConsumer::Status intent{ESSConsumer::Status::Continue};
  while (intent != ESSConsumer::Status::Halt) {
    auto Msg = Consumer->consume();
    intent = Consumer->handleMessage(Msg);
    delete Msg;
    if (ESSConsumer::Status::Update == intent){
        intent = ESSConsumer::Status::Continue;
    }
  }
}


