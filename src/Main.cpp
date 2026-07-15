// Copyright (C) 2025 European Spallation Source, ERIC. See LICENSE file
//===----------------------------------------------------------------------===//
//
/// \file
//
/// \brief The main entrypoint for the fylgje application with command line parsing
//===----------------------------------------------------------------------===//
#include <iostream>
#include <map>
#include <fmt/format.h>

#include "args.hxx"

#include "App.h"
#include "Configuration.h"
#include "Calibration.h"
#include "Time.h"
#include "Version.h"

void print_licenses(const std::string & which){
  std::map<std::string, std::string> licenses{
      {"fylgje",
          " Copyright (c) 2025, European Spallation Source \n"
          " All rights reserved.\n"
          "\n"
          " Redistribution and use in source and binary forms, with or without modification, are \n"
          " permitted provided that the following conditions are met:\n"
          "\n"
          " 1. Redistributions of source code must retain the above copyright notice, \n"
          "    this list of conditions and the following disclaimer.\n"
          "\n"
          " 2. Redistributions in binary form must reproduce the above copyright notice, \n"
          "    this list of conditions and the following disclaimer in the documentation \n"
          "    and/or other materials provided with the distribution.\n"
          "\n"
          " THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS \"AS IS\" AND \n"
          " ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED \n"
          " WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. \n"
          " IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, \n"
          " INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, \n"
          " BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, \n"
          " DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF \n"
          " LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE \n"
          " OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED \n"
          " OF THE POSSIBILITY OF SUCH DAMAGE."
          },
      {"args",
          " Copyright (c) 2016-2024 Taylor C. Richberger <taylor@axfive.net> and Pavel Belikov\n"
          "\n"
          " Permission is hereby granted, free of charge, to any person obtaining a copy\n"
          " of this software and associated documentation files (the \"Software\"), to\n"
          " deal in the Software without restriction, including without limitation the\n"
          " rights to use, copy, modify, merge, publish, distribute, sublicense, and/or\n"
          " sell copies of the Software, and to permit persons to whom the Software is\n"
          " furnished to do so, subject to the following conditions:\n"
          "\n"
          " The above copyright notice and this permission notice shall be included in\n"
          " all copies or substantial portions of the Software.\n"
          "\n"
          " THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR\n"
          " IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,\n"
          " FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE\n"
          " AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER\n"
          " LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING\n"
          " FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS\n"
          " IN THE SOFTWARE.\n"},
  };
  if (auto it = licenses.find(which); it != licenses.end()){
    std::cout << fmt::format("{}\n", it->second);
  } else {
    std::cerr << fmt::format("No license information for {}. Known licenses for [", which);
    for (auto & [k, v] : licenses){
      std::cerr << fmt::format("{}, ", k);
    }
    std::cerr << fmt::format("]\n");
  }
}

int main(int argc, char *argv[]){
    args::ArgumentParser parser("fylgje - your mythical instrument follower",
        "Times are UTC: YYYY-MM-DDThh:mm:ssZ, e.g. 2026-07-15T09:30:00Z.\n"
        "Durations are an integer with a unit: d, h, m, s, ms, us, ns;\n"
        "a bare number means seconds, e.g. 90m, 30s, 5400.\n"
        "Examples (TIME as above; add -b HOST:PORT or -f config.json):\n"
        "fixed window: fylgje-cli -t TOPIC --from TIME --to TIME -e\n"
        "by duration: fylgje-cli -t TOPIC --from TIME --duration 90m -d\n"
        "until Ctrl-C: fylgje-cli -t TOPIC --write-every 30s -e -o run42.h5\n"
        "Background from Wikipedia on fylgje: https://en.wikipedia.org/wiki/Fylgja");

    args::HelpFlag help(parser, "help", "Display this help menu", {'h', "help"});
    args::Flag verbose(parser, "verbose", "Print additional information", {'v', "verbose"});
    args::Flag version_flag(parser, "version", "Print the fylgje version and exit", {'V', "version"});

    auto now = std::time({});
    char timeString[std::size("yyyy-mm-ddThh:mm:ssZ")];
    std::strftime(std::data(timeString), std::size(timeString), "%FT%TZ", std::gmtime(&now));
    Configuration Config;
    Calibration calibration;

    args::ValueFlag<std::string> file_flag(parser, "file", "JSON configuration <file> with instrument/Kafka/plot settings.", {'f', "file"});
    args::ValueFlag<std::string> broker_flag(parser, "broker", "Kafka <broker> as host:port, e.g. localhost:9092.", {'b', "broker"});
    args::ValueFlag<std::string> topic_flag(parser, "topic", "Kafka <topic> holding ar51 raw readout messages, e.g. bifrost_detector_samples.", {'t', "topic"});
    args::ValueFlag<std::string> config_flag(parser, "config", "Kafka <configuration> JSON file: {\"KafkaParms\": [{\"<option>\": \"<value>\"}, ...]} entries passed to librdkafka.", {'k', "config"});
    args::ValueFlag<std::string> calibration_flag(parser, "calibration", "Detector calibration JSON file.", {'c', "calibration"});
    args::ValueFlag<std::string> output_flag(parser, "output", "Output HDF5 file path; must not already contain a /fylgje group (default: fylgje.h5).", {'o', "output"});
    args::ValueFlag<std::string> from_flag(parser, "from", "Start time for accumulation, UTC as YYYY-MM-DDThh:mm:ssZ (default: now).", {"from"}, timeString);
    args::ValueFlag<std::string> to_flag(parser, "to", "End time for accumulation, same format as --from; may lie in the future. CLI mode consumes until interrupted (Ctrl-C stops and saves) when omitted.", {"to"}, timeString);
    args::ValueFlag<std::string> duration_flag(parser, "duration", "Duration for accumulation, e.g. 90m, 30s, or bare seconds; combines with exactly one of --from/--to.", {"duration"}, "1h");
    args::ValueFlag<std::string> write_flag(parser, "write-every",
        "Periodically write collected data to the output file every <write-every>, e.g. 30s, 5m, or a bare number"
        " of seconds (CLI mode; 0 disables periodic writing). While enabled the file is written in HDF5 SWMR mode,"
        " readable by other processes but requiring HDF5 >= 1.10 tools.", {"write-every"}, "60s");
    args::ValueFlag<std::string> license_flag(parser, "license", "Print license information for <license>: fylgje or args.", {'l', "license"});
    args::Flag events_flag(parser, "events", "Store events in HDF5 file", {'e', "events"});
    args::Flag pixels_flag(parser, "pixels", "Store pixel data in HDF5 file", {'p', "pixels"});
    args::Flag histograms_flag(parser, "histograms", "Store histogram data in HDF5 file", {'d', "histograms"});
#ifdef FYLGJE_GUI
    args::Flag cli_flag(parser, "cli", "Run in CLI mode", {'x', "cli"});
#endif

    try {
      parser.ParseCLI(argc, argv);
    }
    catch (const args::Help&) {
      std::cout << parser;
      return 0;
    }
    catch (const args::ParseError& e) {
      std::cerr << e.what() << std::endl;
      std::cerr << parser;
      return 1;
    }

    if (version_flag) {
      std::cout << fmt::format("{} {}\n", fylgje::creator, fylgje::version);
      return 0;
    }

    if (license_flag) {
      auto name = args::get(license_flag);
      print_licenses(name);
      return 0;
    }

    if (file_flag) {
      if (auto file = args::get(file_flag); !file.empty()) {
        Config.fromJsonFile(file);
      }
    }
    if (broker_flag){
      if (auto broker = args::get(broker_flag); !broker.empty()) {
        Config.Kafka.Broker = broker;
        std::cout << fmt::format("<<<<\n WARNING Override kafka broker to {} \n>>>>\n", Config.Kafka.Broker);
      }
    }
    if (topic_flag){
      if (auto topic = args::get(topic_flag); !topic.empty()) {
        Config.Kafka.Topic = topic;
        std::cout << fmt::format("<<<<\n WARNING Override kafka topic to {} \n>>>>\n", Config.Kafka.Topic);
      }
    }
    if (config_flag){
      if (auto config = args::get(config_flag); !config.empty()) {
        Config.KafkaConfigFile = config;
      }
    }
    if (calibration_flag){
      if (auto calib = args::get(calibration_flag); !calib.empty()) {
        calibration = from_json_file(calib);
      }
    }

    std::optional<std::string> output_file;
    if (output_flag){
      if (auto output = args::get(output_flag); !output.empty()) {
        output_file = output;
      }
    }

    kafka::time::milliseconds from_time{kafka::time::time_t_to_milliseconds(now)};
    std::optional<kafka::time::milliseconds> to_time;

    if (from_flag && to_flag && duration_flag) {
      std::cout << "Setting all of from, to, and duration is likely to cause inconsistencies. Duration ignored\n";
    }
    if (from_flag) {
      from_time = kafka::time::time_string_to_milliseconds(args::get(from_flag));
    }
    if (to_flag) {
      to_time = kafka::time::time_string_to_milliseconds(args::get(to_flag));
    }
    if (duration_flag && (from_flag ^ to_flag)){
      auto duration = kafka::time::duration_string_to_milliseconds(args::get(duration_flag));
      if (to_time.has_value()){
        from_time = to_time.value() - duration;
      } else {
        to_time = from_time + duration;
      }
    }

    bool gui{false};
#ifdef FYLGJE_GUI
    gui = !cli_flag;
#endif

    auto write_every = kafka::time::duration_string_to_milliseconds(args::get(write_flag));

    return fylgje_app(Config, calibration, from_time, to_time, output_file, gui,
                      events_flag, pixels_flag, histograms_flag, write_every);
}


