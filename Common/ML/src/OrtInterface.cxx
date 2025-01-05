// Copyright 2019-2020 CERN and copyright holders of ALICE O2.
// See https://alice-o2.web.cern.ch/copyright for details of the copyright holders.
// All rights not expressly granted are reserved.
//
// This software is distributed under the terms of the GNU General Public
// License v3 (GPL Version 3), copied verbatim in the file "COPYING".
//
// In applying this license CERN does not waive the privileges and immunities
// granted to it by virtue of its status as an Intergovernmental Organization
// or submit itself to any jurisdiction.

/// \file     OrtInterface.cxx
/// \author   Christian Sonnabend <christian.sonnabend@cern.ch>
/// \brief    A header library for loading ONNX models and inferencing them on CPU and GPU

#include "ML/OrtInterface.h"
#include "ML/3rdparty/GPUORTFloat16.h"

// ONNX includes
#include <onnxruntime_cxx_api.h>

namespace o2
{

namespace ml
{

struct OrtModel::OrtVariables { // The actual implementation is hidden in the .cxx file
  // ORT runtime objects
  Ort::RunOptions runOptions;
  std::shared_ptr<Ort::Env> env = nullptr;
  std::shared_ptr<Ort::Session> session = nullptr; ///< ONNX session
  Ort::SessionOptions sessionOptions;
  Ort::AllocatorWithDefaultOptions allocator;
  Ort::MemoryInfo memoryInfo = Ort::MemoryInfo("Cpu", OrtAllocatorType::OrtDeviceAllocator, 0, OrtMemType::OrtMemTypeDefault);
};

void OrtModel::reset(std::unordered_map<std::string, std::string> optionsMap)
{

  pImplOrt = new OrtVariables();

  // Load from options map
  if (!optionsMap.contains("model-path")) {
    LOG(fatal) << "(ORT) Model path cannot be empty!";
  }
  modelPath = optionsMap["model-path"];
  device = (optionsMap.contains("device") ? optionsMap["device"] : "CPU");
  dtype = (optionsMap.contains("dtype") ? optionsMap["dtype"] : "float");
  deviceId = (optionsMap.contains("device-id") ? std::stoi(optionsMap["device-id"]) : 0);
  allocateDeviceMemory = (optionsMap.contains("allocate-device-memory") ? std::stoi(optionsMap["allocate-device-memory"]) : 0);
  intraOpNumThreads = (optionsMap.contains("intra-op-num-threads") ? std::stoi(optionsMap["intra-op-num-threads"]) : 0);
  loggingLevel = (optionsMap.contains("logging-level") ? std::stoi(optionsMap["logging-level"]) : 2);
  enableProfiling = (optionsMap.contains("enable-profiling") ? std::stoi(optionsMap["enable-profiling"]) : 0);
  enableOptimizations = (optionsMap.contains("enable-optimizations") ? std::stoi(optionsMap["enable-optimizations"]) : 0);

  std::string dev_mem_str = "Hip";
#if defined(ORT_ROCM_BUILD)
#if ORT_ROCM_BUILD == 1
  if (device == "ROCM") {
    Ort::ThrowOnError(OrtSessionOptionsAppendExecutionProvider_ROCM(pImplOrt->sessionOptions, deviceId));
    LOG(info) << "(ORT) ROCM execution provider set";
  }
#endif
#endif
#if defined(ORT_MIGRAPHX_BUILD)
#if ORT_MIGRAPHX_BUILD == 1
  if (device == "MIGRAPHX") {
    Ort::ThrowOnError(OrtSessionOptionsAppendExecutionProvider_MIGraphX(pImplOrt->sessionOptions, deviceId));
    LOG(info) << "(ORT) MIGraphX execution provider set";
  }
#endif
#endif
#if defined(ORT_CUDA_BUILD)
#if ORT_CUDA_BUILD == 1
  if (device == "CUDA") {
    Ort::ThrowOnError(OrtSessionOptionsAppendExecutionProvider_CUDA(pImplOrt->sessionOptions, deviceId));
    LOG(info) << "(ORT) CUDA execution provider set";
    dev_mem_str = "Cuda";
  }
#endif
#endif

  if (allocateDeviceMemory) {
    pImplOrt->memoryInfo = Ort::MemoryInfo(dev_mem_str.c_str(), OrtAllocatorType::OrtDeviceAllocator, deviceId, OrtMemType::OrtMemTypeDefault);
    LOG(info) << "(ORT) Memory info set to on-device memory";
  }

  if (device == "CPU") {
    (pImplOrt->sessionOptions).SetIntraOpNumThreads(intraOpNumThreads);
    if (intraOpNumThreads > 1) {
      (pImplOrt->sessionOptions).SetExecutionMode(ExecutionMode::ORT_PARALLEL);
    } else if (intraOpNumThreads == 1) {
      (pImplOrt->sessionOptions).SetExecutionMode(ExecutionMode::ORT_SEQUENTIAL);
    }
    LOG(info) << "(ORT) CPU execution provider set with " << intraOpNumThreads << " threads";
  }

  (pImplOrt->sessionOptions).DisableMemPattern();
  (pImplOrt->sessionOptions).DisableCpuMemArena();

  if (enableProfiling) {
    if (optionsMap.contains("profiling-output-path")) {
      (pImplOrt->sessionOptions).EnableProfiling((optionsMap["profiling-output-path"] + "/ORT_LOG_").c_str());
    } else {
      LOG(warning) << "(ORT) If profiling is enabled, optionsMap[\"profiling-output-path\"] should be set. Disabling profiling for now.";
      (pImplOrt->sessionOptions).DisableProfiling();
    }
  } else {
    (pImplOrt->sessionOptions).DisableProfiling();
  }
  (pImplOrt->sessionOptions).SetGraphOptimizationLevel(GraphOptimizationLevel(enableOptimizations));
  (pImplOrt->sessionOptions).SetLogSeverityLevel(OrtLoggingLevel(loggingLevel));

  pImplOrt->env = std::make_shared<Ort::Env>(
    OrtLoggingLevel(loggingLevel),
    (optionsMap["onnx-environment-name"].empty() ? "onnx_model_inference" : optionsMap["onnx-environment-name"].c_str()),
    // Integrate ORT logging into Fairlogger
    [](void* param, OrtLoggingLevel severity, const char* category, const char* logid, const char* code_location, const char* message) {
      if (severity == ORT_LOGGING_LEVEL_VERBOSE) {
        LOG(debug) << "(ORT) [" << logid << "|" << category << "|" << code_location << "]: " << message;
      } else if (severity == ORT_LOGGING_LEVEL_INFO) {
        LOG(info) << "(ORT) [" << logid << "|" << category << "|" << code_location << "]: " << message;
      } else if (severity == ORT_LOGGING_LEVEL_WARNING) {
        LOG(warning) << "(ORT) [" << logid << "|" << category << "|" << code_location << "]: " << message;
      } else if (severity == ORT_LOGGING_LEVEL_ERROR) {
        LOG(error) << "(ORT) [" << logid << "|" << category << "|" << code_location << "]: " << message;
      } else if (severity == ORT_LOGGING_LEVEL_FATAL) {
        LOG(fatal) << "(ORT) [" << logid << "|" << category << "|" << code_location << "]: " << message;
      } else {
        LOG(info) << "(ORT) [" << logid << "|" << category << "|" << code_location << "]: " << message;
      }
    },
    (void*)3);
  (pImplOrt->env)->DisableTelemetryEvents(); // Disable telemetry events
  pImplOrt->session = std::make_shared<Ort::Session>(*(pImplOrt->env), modelPath.c_str(), pImplOrt->sessionOptions);

  for (size_t i = 0; i < (pImplOrt->session)->GetInputCount(); ++i) {
    mInputNames.push_back((pImplOrt->session)->GetInputNameAllocated(i, pImplOrt->allocator).get());
  }
  for (size_t i = 0; i < (pImplOrt->session)->GetInputCount(); ++i) {
    mInputShapes.emplace_back((pImplOrt->session)->GetInputTypeInfo(i).GetTensorTypeAndShapeInfo().GetShape());
  }
  for (size_t i = 0; i < (pImplOrt->session)->GetOutputCount(); ++i) {
    mOutputNames.push_back((pImplOrt->session)->GetOutputNameAllocated(i, pImplOrt->allocator).get());
  }
  for (size_t i = 0; i < (pImplOrt->session)->GetOutputCount(); ++i) {
    mOutputShapes.emplace_back((pImplOrt->session)->GetOutputTypeInfo(i).GetTensorTypeAndShapeInfo().GetShape());
  }

  inputNamesChar.resize(mInputNames.size(), nullptr);
  std::transform(std::begin(mInputNames), std::end(mInputNames), std::begin(inputNamesChar),
                 [&](const std::string& str) { return str.c_str(); });
  outputNamesChar.resize(mOutputNames.size(), nullptr);
  std::transform(std::begin(mOutputNames), std::end(mOutputNames), std::begin(outputNamesChar),
                 [&](const std::string& str) { return str.c_str(); });

  // Print names
  LOG(info) << "\tInput Nodes:";
  for (size_t i = 0; i < mInputNames.size(); i++) {
    LOG(info) << "\t\t" << mInputNames[i] << " : " << printShape(mInputShapes[i]);
  }

  LOG(info) << "\tOutput Nodes:";
  for (size_t i = 0; i < mOutputNames.size(); i++) {
    LOG(info) << "\t\t" << mOutputNames[i] << " : " << printShape(mOutputShapes[i]);
  }
}

void OrtModel::resetSession()
{
  pImplOrt->session = std::make_shared<Ort::Session>(*(pImplOrt->env), modelPath.c_str(), pImplOrt->sessionOptions);
}

template <class I, class O>
std::vector<O> OrtModel::v2v(std::vector<I>& input, bool clearInput)
{
  if constexpr (std::is_same_v<I, O>) {
    return input;
  } else {
    std::vector<O> output(input.size());
    std::transform(std::begin(input), std::end(input), std::begin(output), [](I f) { return O(f); });
    if (clearInput) {
      input.clear();
    }
    return output;
  }
}

template <class I, class O> // class I is the input data type, e.g. float, class O is the output data type, e.g. O2::gpu::OrtDataType::Float16_t from O2/GPU/GPUTracking/ML/convert_float16.h
std::vector<O> OrtModel::inference(std::vector<I>& input)
{
  std::vector<int64_t> inputShape{(int64_t)(input.size() / mInputShapes[0][1]), (int64_t)mInputShapes[0][1]};
  std::vector<Ort::Value> inputTensor;
  inputTensor.emplace_back(Ort::Value::CreateTensor<O>(pImplOrt->memoryInfo, reinterpret_cast<O*>(input.data()), input.size(), inputShape.data(), inputShape.size()));
  // input.clear();
  auto outputTensors = (pImplOrt->session)->Run(pImplOrt->runOptions, inputNamesChar.data(), inputTensor.data(), inputTensor.size(), outputNamesChar.data(), outputNamesChar.size());
  O* outputValues = reinterpret_cast<O*>(outputTensors[0].template GetTensorMutableData<O>());
  std::vector<O> outputValuesVec{outputValues, outputValues + inputShape[0] * mOutputShapes[0][1]};
  outputTensors.clear();
  return outputValuesVec;
}

template <class I, class O> // class I is the input data type, e.g. float, class O is the output data type, e.g. O2::gpu::OrtDataType::Float16_t from O2/GPU/GPUTracking/ML/convert_float16.h
std::vector<O> OrtModel::inference(std::vector<std::vector<I>>& input)
{
  std::vector<Ort::Value> inputTensor;
  for (auto i : input) {
    std::vector<int64_t> inputShape{(int64_t)(i.size() / mInputShapes[0][1]), (int64_t)mInputShapes[0][1]};
    inputTensor.emplace_back(Ort::Value::CreateTensor<O>(pImplOrt->memoryInfo, reinterpret_cast<O*>(i.data()), i.size(), inputShape.data(), inputShape.size()));
  }
  // input.clear();
  auto outputTensors = (pImplOrt->session)->Run(pImplOrt->runOptions, inputNamesChar.data(), inputTensor.data(), inputTensor.size(), outputNamesChar.data(), outputNamesChar.size());
  O* outputValues = reinterpret_cast<O*>(outputTensors[0].template GetTensorMutableData<O>());
  std::vector<O> outputValuesVec{outputValues, outputValues + inputTensor.size() / mInputShapes[0][1] * mOutputShapes[0][1]};
  outputTensors.clear();
  return outputValuesVec;
}

std::string OrtModel::printShape(const std::vector<int64_t>& v)
{
  std::stringstream ss("");
  for (size_t i = 0; i < v.size() - 1; i++) {
    ss << v[i] << "x";
  }
  ss << v[v.size() - 1];
  return ss.str();
}

template <>
std::vector<float> OrtModel::inference<float, float>(std::vector<float>& input)
{
  std::vector<int64_t> inputShape{(int64_t)(input.size() / mInputShapes[0][1]), (int64_t)mInputShapes[0][1]};
  std::vector<Ort::Value> inputTensor;
  inputTensor.emplace_back(Ort::Value::CreateTensor<float>(pImplOrt->memoryInfo, input.data(), input.size(), inputShape.data(), inputShape.size()));
  // input.clear();
  auto outputTensors = (pImplOrt->session)->Run(pImplOrt->runOptions, inputNamesChar.data(), inputTensor.data(), inputTensor.size(), outputNamesChar.data(), outputNamesChar.size());
  float* outputValues = outputTensors[0].template GetTensorMutableData<float>();
  std::vector<float> outputValuesVec{outputValues, outputValues + inputShape[0] * mOutputShapes[0][1]};
  outputTensors.clear();
  return outputValuesVec;
}

template <>
std::vector<float> OrtModel::inference<OrtDataType::Float16_t, float>(std::vector<OrtDataType::Float16_t>& input)
{
  std::vector<int64_t> inputShape{(int64_t)(input.size() / mInputShapes[0][1]), (int64_t)mInputShapes[0][1]};
  std::vector<Ort::Value> inputTensor;
  inputTensor.emplace_back(Ort::Value::CreateTensor<Ort::Float16_t>(pImplOrt->memoryInfo, reinterpret_cast<Ort::Float16_t*>(input.data()), input.size(), inputShape.data(), inputShape.size()));
  // input.clear();
  auto outputTensors = (pImplOrt->session)->Run(pImplOrt->runOptions, inputNamesChar.data(), inputTensor.data(), inputTensor.size(), outputNamesChar.data(), outputNamesChar.size());
  float* outputValues = outputTensors[0].template GetTensorMutableData<float>();
  std::vector<float> outputValuesVec{outputValues, outputValues + inputShape[0] * mOutputShapes[0][1]};
  outputTensors.clear();
  return outputValuesVec;
}

template <>
std::vector<OrtDataType::Float16_t> OrtModel::inference<OrtDataType::Float16_t, OrtDataType::Float16_t>(std::vector<OrtDataType::Float16_t>& input)
{
  std::vector<int64_t> inputShape{(int64_t)(input.size() / mInputShapes[0][1]), (int64_t)mInputShapes[0][1]};
  std::vector<Ort::Value> inputTensor;
  inputTensor.emplace_back(Ort::Value::CreateTensor<Ort::Float16_t>(pImplOrt->memoryInfo, reinterpret_cast<Ort::Float16_t*>(input.data()), input.size(), inputShape.data(), inputShape.size()));
  // input.clear();
  auto outputTensors = (pImplOrt->session)->Run(pImplOrt->runOptions, inputNamesChar.data(), inputTensor.data(), inputTensor.size(), outputNamesChar.data(), outputNamesChar.size());
  OrtDataType::Float16_t* outputValues = reinterpret_cast<OrtDataType::Float16_t*>(outputTensors[0].template GetTensorMutableData<Ort::Float16_t>());
  std::vector<OrtDataType::Float16_t> outputValuesVec{outputValues, outputValues + inputShape[0] * mOutputShapes[0][1]};
  outputTensors.clear();
  return outputValuesVec;
}

template <>
std::vector<OrtDataType::Float16_t> OrtModel::inference<float, OrtDataType::Float16_t>(std::vector<float>& input)
{
  std::vector<int64_t> inputShape{(int64_t)(input.size() / mInputShapes[0][1]), (int64_t)mInputShapes[0][1]};
  std::vector<Ort::Value> inputTensor;
  inputTensor.emplace_back(Ort::Value::CreateTensor<Ort::Float16_t>(pImplOrt->memoryInfo, reinterpret_cast<Ort::Float16_t*>(input.data()), input.size(), inputShape.data(), inputShape.size()));
  // input.clear();
  auto outputTensors = (pImplOrt->session)->Run(pImplOrt->runOptions, inputNamesChar.data(), inputTensor.data(), inputTensor.size(), outputNamesChar.data(), outputNamesChar.size());
  OrtDataType::Float16_t* outputValues = reinterpret_cast<OrtDataType::Float16_t*>(outputTensors[0].template GetTensorMutableData<Ort::Float16_t>());
  std::vector<OrtDataType::Float16_t> outputValuesVec{outputValues, outputValues + inputShape[0] * mOutputShapes[0][1]};
  outputTensors.clear();
  return outputValuesVec;
}

template <>
std::vector<OrtDataType::Float16_t> OrtModel::inference<OrtDataType::Float16_t, OrtDataType::Float16_t>(std::vector<std::vector<OrtDataType::Float16_t>>& input)
{
  std::vector<Ort::Value> inputTensor;
  for (auto i : input) {
    std::vector<int64_t> inputShape{(int64_t)(i.size() / mInputShapes[0][1]), (int64_t)mInputShapes[0][1]};
    inputTensor.emplace_back(Ort::Value::CreateTensor<Ort::Float16_t>(pImplOrt->memoryInfo, reinterpret_cast<Ort::Float16_t*>(i.data()), i.size(), inputShape.data(), inputShape.size()));
  }
  // input.clear();
  auto outputTensors = (pImplOrt->session)->Run(pImplOrt->runOptions, inputNamesChar.data(), inputTensor.data(), inputTensor.size(), outputNamesChar.data(), outputNamesChar.size());
  OrtDataType::Float16_t* outputValues = reinterpret_cast<OrtDataType::Float16_t*>(outputTensors[0].template GetTensorMutableData<Ort::Float16_t>());
  std::vector<OrtDataType::Float16_t> outputValuesVec{outputValues, outputValues + inputTensor.size() / mInputShapes[0][1] * mOutputShapes[0][1]};
  outputTensors.clear();
  return outputValuesVec;
}

} // namespace ml

} // namespace o2