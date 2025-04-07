include(QtLibraryConfig)
find_package(RdKafka REQUIRED)
find_package(h5cpp REQUIRED)
find_package(qplot REQUIRED)

set(cli_target fylgje-cli)
set(gui_target fylgje)

set(${cli_target}_src
  App.cpp
  Calibration.cpp
  CLI.cpp
  Configuration.cpp
  ESSConsumer.cpp
  EventManager.cpp
  HistogramManager.cpp
  KafkaConfig.cpp
  Main.cpp
  Message.cpp
  Network.cpp
  PixelManager.cpp
  Time.cpp
)

set(${cli_target}_inc
  App.h
  CLI.h
  Calibration.h
  Configuration.h
  ESSConsumer.h
  EventManager.h
  HistogramManager.h
  JsonFile.h
  KafkaConfig.h
  Message.h
  Network.h
  PixelManager.h
  Time.h
)

add_executable(
  ${cli_target}
  ${${cli_target}_src}
  ${${cli_target}_inc}
)

target_link_libraries(
  ${cli_target}
  PUBLIC fmt::fmt
  PRIVATE RdKafka::rdkafka++
  PRIVATE RdKafka::rdkafka
  PRIVATE h5cpp::h5cpp
)

target_link_libraries(${cli_target}
  PRIVATE $<$<AND:$<CXX_COMPILER_ID:GNU>,$<VERSION_LESS:$<CXX_COMPILER_VERSION>,9.0>>:stdc++fs>)
target_link_libraries(${cli_target}
  PRIVATE $<$<AND:$<CXX_COMPILER_ID:AppleClang>,$<VERSION_LESS:$<CXX_COMPILER_VERSION>,11.0>>:c++fs>)


set(${gui_target}_src
  App.cpp
  AppWindow.cpp
  AppOverview.cpp
  AppCalibration.cpp
  AppData.cpp
  Calibration.cpp
  CLI.cpp
  Configuration.cpp
  ESSConsumer.cpp
  EventManager.cpp
  GUI.cpp
  HistogramManager.cpp
  KafkaConfig.cpp
  Main.cpp
  Message.cpp
  Network.cpp
  PixelManager.cpp
  PlotManager.cpp
  QHistogramManager.cpp
  TableItemTypes.cpp
  Time.cpp
)
set(${gui_target}_inc
  App.h
  AppWindow.h
  Calibration.h
  CLI.h
  Configuration.h
  Cycles.h
  ESSConsumer.h
  EventManager.h
  GUI.h
  HistogramManager.h
  JsonFile.h
  KafkaConfig.h
  Message.h
  Network.h
  PlotManager.h
  PixelManager.h
  QEventManager.h
  QHistogramManager.h
  Time.h
  TwoSpinBox.h
  WorkerThread.h
)
set(${gui_target}_ui
  AppWindow.ui
)
set(CMAKE_AUTOUIC ON)
add_executable(
  ${gui_target}
  ${${gui_target}_src}
  ${${gui_target}_inc}
  ${${gui_target}_ui}
)
target_compile_definitions(${gui_target} PUBLIC FYLGJE_GUI)
target_link_libraries(
  ${gui_target}
  PUBLIC fmt::fmt
  PRIVATE RdKafka::rdkafka++
  PRIVATE RdKafka::rdkafka
  PRIVATE QPlot
  PRIVATE Qt6::Widgets
  PRIVATE Qt6::Core5Compat
  PRIVATE h5cpp::h5cpp
)
target_link_libraries(${gui_target}
  PRIVATE $<$<AND:$<CXX_COMPILER_ID:GNU>,$<VERSION_LESS:$<CXX_COMPILER_VERSION>,9.0>>:stdc++fs>)
target_link_libraries(${gui_target}
  PRIVATE $<$<AND:$<CXX_COMPILER_ID:AppleClang>,$<VERSION_LESS:$<CXX_COMPILER_VERSION>,11.0>>:c++fs>)