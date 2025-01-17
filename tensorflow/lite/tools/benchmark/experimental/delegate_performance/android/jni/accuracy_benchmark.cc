/* Copyright 2022 The TensorFlow Authors. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/

#include "tensorflow/lite/tools/benchmark/experimental/delegate_performance/android/jni/accuracy_benchmark.h"

#include <errno.h>
#include <stdio.h>
#include <sys/stat.h>

#include <cstddef>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "flatbuffers/flatbuffer_builder.h"  // from @flatbuffers
#include "tensorflow/lite/delegates/utils/experimental/stable_delegate/tflite_settings_json_parser.h"
#include "tensorflow/lite/experimental/acceleration/configuration/configuration_generated.h"
#include "tensorflow/lite/experimental/acceleration/mini_benchmark/blocking_validator_runner.h"
#include "tensorflow/lite/experimental/acceleration/mini_benchmark/status_codes.h"
#include "tensorflow/lite/experimental/acceleration/mini_benchmark/validator_runner_options.h"
#include "tensorflow/lite/logger.h"
#include "tensorflow/lite/minimal_logging.h"
#include "tensorflow/lite/tools/command_line_flags.h"
#include "tensorflow/lite/tools/tool_params.h"

namespace tflite {
namespace benchmark {
namespace accuracy {
namespace {

template <typename T>
Flag CreateFlag(const char* name, tools::ToolParams* params,
                const std::string& usage) {
  return Flag(
      name,
      [params, name](const T& val, int argv_position) {
        params->Set<T>(name, val, argv_position);
      },
      params->Get<T>(name), usage, Flag::kRequired);
}

AccuracyBenchmarkStatus ParseTfLiteSettingsFilePathFromArgs(
    const std::vector<std::string>& args,
    std::string& tflite_settings_file_path) {
  tools::ToolParams params;
  params.AddParam("stable_delegate_settings_file",
                  tools::ToolParam::Create<std::string>(""));

  std::vector<const char*> argv;
  std::string arg0 = "(MiniBenchmarkAndroid)";
  argv.push_back(const_cast<char*>(arg0.data()));
  for (auto& arg : args) {
    argv.push_back(arg.data());
  }
  int argc = argv.size();
  if (!Flags::Parse(
          &argc, argv.data(),
          {CreateFlag<std::string>("stable_delegate_settings_file", &params,
                                   "Path to the JSON-formatted stable delegate "
                                   "TFLiteSettings file.")})) {
    return kAccuracyBenchmarkArgumentParsingFailed;
  }
  tflite_settings_file_path =
      params.Get<std::string>("stable_delegate_settings_file");
  return kAccuracyBenchmarkSuccess;
}

}  // namespace

AccuracyBenchmarkStatus Benchmark(const std::vector<std::string>& args,
                                  int model_fd, size_t model_offset,
                                  size_t model_size,
                                  const char* result_path_chars) {
  std::string result_path(result_path_chars);
  acceleration::ValidatorRunnerOptions options;
  options.model_fd = model_fd;
  options.model_offset = model_offset;
  options.model_size = model_size;
  options.data_directory_path = result_path;
  options.storage_path = result_path + "/storage_path.fb";
  int return_code = std::remove(options.storage_path.c_str());
  if (return_code) {
    TFLITE_LOG_PROD(TFLITE_LOG_WARNING,
                    "Failed to remove storage file (%s): %s.",
                    options.storage_path.c_str(), strerror(errno));
  }
  options.per_test_timeout_ms = 5000;

  acceleration::BlockingValidatorRunner runner(options);
  acceleration::MinibenchmarkStatus status = runner.Init();
  if (status != acceleration::kMinibenchmarkSuccess) {
    TFLITE_LOG_PROD(
        TFLITE_LOG_ERROR,
        "MiniBenchmark BlockingValidatorRunner initialization failed with "
        "error code %d",
        status);
    return kAccuracyBenchmarkRunnerInitializationFailed;
  }

  std::string tflite_settings_file_path;
  AccuracyBenchmarkStatus parse_status =
      ParseTfLiteSettingsFilePathFromArgs(args, tflite_settings_file_path);
  delegates::utils::TfLiteSettingsJsonParser parser;

  if (parse_status != kAccuracyBenchmarkSuccess) {
    TFLITE_LOG_PROD(TFLITE_LOG_ERROR,
                    "Failed to parse arguments with error code %d",
                    parse_status);
    return parse_status;
  }
  const TFLiteSettings* tflite_settings =
      parser.Parse(tflite_settings_file_path);
  if (tflite_settings == nullptr) {
    TFLITE_LOG_PROD(
        TFLITE_LOG_ERROR,
        "Failed to parse TFLiteSettings from the input JSON file %s",
        tflite_settings_file_path.c_str());
    return kAccuracyBenchmarkTfLiteSettingsParsingFailed;
  }
  std::vector<const TFLiteSettings*> settings = {tflite_settings};
  std::vector<flatbuffers::FlatBufferBuilder> results =
      runner.TriggerValidation(settings);
  if (results.size() != settings.size()) {
    TFLITE_LOG_PROD(
        TFLITE_LOG_ERROR,
        "Number of result events (%zu) doesn't match the expectation (%zu).",
        results.size(), settings.size());
    return kAccuracyBenchmarkResultCountMismatch;
  }
  // The settings contains one test only. Therefore, the benchmark checks for
  // the first result only.
  const BenchmarkEvent* result_event =
      flatbuffers::GetRoot<BenchmarkEvent>(results[0].GetBufferPointer());
  if (!result_event->result() || !result_event->result()->ok()) {
    return kAccuracyBenchmarkFail;
  }
  return kAccuracyBenchmarkPass;
}

}  // namespace accuracy
}  // namespace benchmark
}  // namespace tflite
