#include "rawlab/ai/OnnxSession.h"

#include <onnxruntime_c_api.h>

#include <QLibrary>
#include <QString>

#include <algorithm>
#include <memory>
#include <bit>
#include <cmath>
#include <iterator>
#include <numeric>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace rawlab::ai {
namespace {

struct RuntimeState
{
  QLibrary library;
  const OrtApi *api = nullptr;
  std::string version;
};

RuntimeState &runtimeState()
{
  static RuntimeState state;
  return state;
}

const OrtApi &ortApi(const std::string &runtimeLibrary = {})
{
  RuntimeState &state = runtimeState();
  if(!state.api)
  {
    const QString name = runtimeLibrary.empty()
      ? QStringLiteral("onnxruntime") : QString::fromStdString(runtimeLibrary);
    state.library.setFileName(name);
    if(!state.library.load())
      throw std::runtime_error{"Could not load ONNX Runtime library '" + name.toStdString()
                               + "': " + state.library.errorString().toStdString()};
    using GetApiBase = const OrtApiBase *(*)();
    const auto getApiBase = reinterpret_cast<GetApiBase>(state.library.resolve("OrtGetApiBase"));
    if(!getApiBase) throw std::runtime_error{"ONNX Runtime library does not export OrtGetApiBase"};
    const OrtApiBase *base = getApiBase();
    if(!base) throw std::runtime_error{"ONNX Runtime returned no API base"};
    state.version = base->GetVersionString() ? base->GetVersionString() : "unknown";
    state.api = base->GetApi(ORT_API_VERSION);
  }
  const OrtApi *api = state.api;
  if(!api) throw std::runtime_error{"ONNX Runtime API is unavailable"};
  return *api;
}

void throwIfOrtError(OrtStatus *status, const char *operation)
{
  if(!status) return;
  const OrtApi &api = ortApi();
  std::string message = std::string{operation} + " failed: " + api.GetErrorMessage(status);
  api.ReleaseStatus(status);
  throw std::runtime_error{message};
}

std::string tensorElementTypeName(const ONNXTensorElementDataType type)
{
  switch(type)
  {
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT: return "tensor(float)";
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT8: return "tensor(uint8)";
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT8: return "tensor(int8)";
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT16: return "tensor(uint16)";
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT16: return "tensor(int16)";
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT32: return "tensor(int32)";
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64: return "tensor(int64)";
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_STRING: return "tensor(string)";
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_BOOL: return "tensor(bool)";
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT16: return "tensor(float16)";
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_DOUBLE: return "tensor(double)";
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT32: return "tensor(uint32)";
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT64: return "tensor(uint64)";
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_COMPLEX64: return "tensor(complex64)";
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_COMPLEX128: return "tensor(complex128)";
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_BFLOAT16: return "tensor(bfloat16)";
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_UNDEFINED: return "tensor(undefined)";
    default: return "tensor(other)";
  }
}

std::uint16_t floatToHalf(const float value)
{
  const std::uint32_t bits = std::bit_cast<std::uint32_t>(value);
  const std::uint32_t sign = (bits >> 16U) & 0x8000U;
  const std::uint32_t exponent = (bits >> 23U) & 0xffU;
  const std::uint32_t mantissa = bits & 0x7fffffU;
  if(exponent == 0xffU)
    return static_cast<std::uint16_t>(sign | (mantissa ? 0x7e00U : 0x7c00U));
  const int halfExponent = static_cast<int>(exponent) - 127 + 15;
  if(halfExponent >= 31) return static_cast<std::uint16_t>(sign | 0x7c00U);
  if(halfExponent <= 0)
  {
    if(halfExponent < -10) return static_cast<std::uint16_t>(sign);
    const std::uint32_t normalized = mantissa | 0x800000U;
    const int shift = 14 - halfExponent;
    return static_cast<std::uint16_t>(sign + ((normalized + (1U << (shift - 1))) >> shift));
  }
  const std::uint32_t rounded = (mantissa + 0x1000U) >> 13U;
  if(rounded == 0x400U)
  {
    const int carriedExponent = halfExponent + 1;
    return static_cast<std::uint16_t>(sign | (carriedExponent >= 31
      ? 0x7c00U : static_cast<std::uint32_t>(carriedExponent) << 10U));
  }
  return static_cast<std::uint16_t>(sign | (static_cast<std::uint32_t>(halfExponent) << 10U)
                                    | rounded);
}

float halfToFloat(const std::uint16_t value)
{
  const std::uint32_t sign = static_cast<std::uint32_t>(value & 0x8000U) << 16U;
  std::uint32_t exponent = (value >> 10U) & 0x1fU;
  std::uint32_t mantissa = value & 0x3ffU;
  std::uint32_t bits = 0;
  if(exponent == 0)
  {
    if(mantissa == 0) bits = sign;
    else
    {
      int adjusted = -14;
      while((mantissa & 0x400U) == 0) { mantissa <<= 1U; --adjusted; }
      mantissa &= 0x3ffU;
      bits = sign | (static_cast<std::uint32_t>(adjusted + 127) << 23U) | (mantissa << 13U);
    }
  }
  else if(exponent == 0x1fU) bits = sign | 0x7f800000U | (mantissa << 13U);
  else bits = sign | ((exponent - 15U + 127U) << 23U) | (mantissa << 13U);
  return std::bit_cast<float>(bits);
}

struct ProviderMapping
{
  const char *id;
  const char *advertised;
  const char *appendName;
  const char *displayName;
  bool hasDevice;
};

constexpr ProviderMapping providers[] = {
  {"cuda", "CUDAExecutionProvider", "CUDA", "CUDA", true},
  {"tensorrt", "TensorrtExecutionProvider", "TensorRT", "TensorRT", true},
  {"rocm", "ROCMExecutionProvider", "ROCM", "ROCm", true},
  {"migraphx", "MIGraphXExecutionProvider", "MIGraphX", "MIGraphX", true},
  {"directml", "DmlExecutionProvider", "DML", "DirectML", true},
  {"coreml", "CoreMLExecutionProvider", "CoreML", "CoreML", false},
  {"openvino", "OpenVINOExecutionProvider", "OpenVINO", "OpenVINO", false},
};

std::vector<std::string> advertisedProviders(const OrtApi &api)
{
  char **names = nullptr;
  int count = 0;
  throwIfOrtError(api.GetAvailableProviders(&names, &count), "Ort GetAvailableProviders");
  std::vector<std::string> result;
  result.reserve(static_cast<std::size_t>(std::max(0, count)));
  for(int i = 0; i < count; ++i) result.emplace_back(names[i] ? names[i] : "");
  throwIfOrtError(api.ReleaseAvailableProviders(names, count), "Ort ReleaseAvailableProviders");
  return result;
}

const ProviderMapping *mappingForId(const std::string &id)
{
  const auto found = std::find_if(std::begin(providers), std::end(providers),
                                  [&id](const ProviderMapping &item) { return id == item.id; });
  return found == std::end(providers) ? nullptr : found;
}

bool isAdvertised(const std::vector<std::string> &available, const ProviderMapping &mapping)
{
  return std::find(available.begin(), available.end(), mapping.advertised) != available.end();
}

bool appendProvider(const OrtApi &api, OrtSessionOptions *options,
                    const ProviderMapping &mapping, const int deviceId, std::string *error)
{
  const std::string device = std::to_string(std::max(0, deviceId));
  const char *keys[] = {"device_id"};
  const char *values[] = {device.c_str()};
  if(std::string{mapping.id} == "cuda" && api.CreateCUDAProviderOptions
     && api.UpdateCUDAProviderOptions && api.SessionOptionsAppendExecutionProvider_CUDA_V2
     && api.ReleaseCUDAProviderOptions)
  {
    OrtCUDAProviderOptionsV2 *providerOptions = nullptr;
    OrtStatus *status = api.CreateCUDAProviderOptions(&providerOptions);
    if(!status) status = api.UpdateCUDAProviderOptions(providerOptions, keys, values, 1);
    if(!status) status = api.SessionOptionsAppendExecutionProvider_CUDA_V2(options, providerOptions);
    if(providerOptions) api.ReleaseCUDAProviderOptions(providerOptions);
    if(!status) return true;
    if(error) *error = api.GetErrorMessage(status);
    api.ReleaseStatus(status);
    return false;
  }
  if(std::string{mapping.id} == "tensorrt" && api.CreateTensorRTProviderOptions
     && api.UpdateTensorRTProviderOptions && api.SessionOptionsAppendExecutionProvider_TensorRT_V2
     && api.ReleaseTensorRTProviderOptions)
  {
    OrtTensorRTProviderOptionsV2 *providerOptions = nullptr;
    OrtStatus *status = api.CreateTensorRTProviderOptions(&providerOptions);
    if(!status) status = api.UpdateTensorRTProviderOptions(providerOptions, keys, values, 1);
    if(!status) status = api.SessionOptionsAppendExecutionProvider_TensorRT_V2(options, providerOptions);
    if(providerOptions) api.ReleaseTensorRTProviderOptions(providerOptions);
    if(!status) return true;
    if(error) *error = api.GetErrorMessage(status);
    api.ReleaseStatus(status);
    return false;
  }
  if(!api.SessionOptionsAppendExecutionProvider)
  {
    if(error) *error = "ONNX Runtime does not expose generic provider configuration";
    return false;
  }
  OrtStatus *status = api.SessionOptionsAppendExecutionProvider(
    options, mapping.appendName, mapping.hasDevice ? keys : nullptr,
    mapping.hasDevice ? values : nullptr, mapping.hasDevice ? 1 : 0);
  if(!status) return true;
  if(error) *error = api.GetErrorMessage(status);
  api.ReleaseStatus(status);
  return false;
}

} // namespace

struct OnnxSession::Impl
{
  OrtEnv *env = nullptr;
  OrtSessionOptions *options = nullptr;
  OrtSession *session = nullptr;
  OrtAllocator *allocator = nullptr;
  OrtMemoryInfo *memoryInfo = nullptr;
};

OnnxSession::OnnxSession(const std::filesystem::path &modelPath,
                         const OnnxSessionOptions &options)
  : impl_{new Impl{}}
{
  const OrtApi &api = ortApi(options.runtimeLibrary);
  const auto cleanupImpl = [&api](Impl *value) {
    if(!value) return;
    if(value->memoryInfo) api.ReleaseMemoryInfo(value->memoryInfo);
    if(value->session) api.ReleaseSession(value->session);
    if(value->options) api.ReleaseSessionOptions(value->options);
    if(value->env) api.ReleaseEnv(value->env);
    delete value;
  };
  std::unique_ptr<Impl, decltype(cleanupImpl)> constructionGuard{impl_, cleanupImpl};
  modelInfo_.runtimeVersion = runtimeState().version;

  throwIfOrtError(api.CreateEnv(ORT_LOGGING_LEVEL_WARNING, "rawlab", &impl_->env), "Ort CreateEnv");
  throwIfOrtError(api.CreateSessionOptions(&impl_->options), "Ort CreateSessionOptions");
  throwIfOrtError(api.SetIntraOpNumThreads(impl_->options, 0), "Ort SetIntraOpNumThreads");
  throwIfOrtError(api.SetSessionGraphOptimizationLevel(impl_->options, ORT_ENABLE_ALL),
                  "Ort SetSessionGraphOptimizationLevel");
  const std::vector<std::string> available = advertisedProviders(api);
  modelInfo_.executionProvider = "CPU";
  if(options.provider == "auto")
  {
    for(const ProviderMapping &mapping : providers)
    {
      if(!isAdvertised(available, mapping)) continue;
      if(appendProvider(api, impl_->options, mapping, options.deviceId, nullptr))
      {
        modelInfo_.executionProvider = mapping.displayName;
        break;
      }
    }
  }
  else if(options.provider != "cpu")
  {
    const ProviderMapping *mapping = mappingForId(options.provider);
    if(!mapping || !isAdvertised(available, *mapping))
      throw std::runtime_error{"Requested ONNX execution provider is unavailable: " + options.provider};
    std::string providerError;
    if(!appendProvider(api, impl_->options, *mapping, options.deviceId, &providerError))
      throw std::runtime_error{"Could not enable " + std::string{mapping->displayName}
                               + ": " + providerError};
    modelInfo_.executionProvider = mapping->displayName;
  }
   OrtStatus *sessionStatus = api.CreateSession(impl_->env, modelPath.native().c_str(),
                                               impl_->options, &impl_->session);
  if(sessionStatus && options.provider == "auto" && modelInfo_.executionProvider != "CPU")
  {
    api.ReleaseStatus(sessionStatus);
    sessionStatus = nullptr;
    api.ReleaseSessionOptions(impl_->options);
    impl_->options = nullptr;
    throwIfOrtError(api.CreateSessionOptions(&impl_->options), "Ort CreateSessionOptions CPU fallback");
    throwIfOrtError(api.SetIntraOpNumThreads(impl_->options, 0), "Ort SetIntraOpNumThreads CPU fallback");
    throwIfOrtError(api.SetSessionGraphOptimizationLevel(impl_->options, ORT_ENABLE_ALL),
                    "Ort SetSessionGraphOptimizationLevel CPU fallback");
    modelInfo_.executionProvider = "CPU";
     sessionStatus = api.CreateSession(impl_->env, modelPath.native().c_str(),
                                      impl_->options, &impl_->session);
  }
  throwIfOrtError(sessionStatus, "Ort CreateSession");
  throwIfOrtError(api.GetAllocatorWithDefaultOptions(&impl_->allocator),
                  "Ort GetAllocatorWithDefaultOptions");
  throwIfOrtError(api.CreateCpuMemoryInfo(OrtArenaAllocator, OrtMemTypeDefault,
                                          &impl_->memoryInfo),
                  "Ort CreateCpuMemoryInfo");

  auto readTensorInfo = [&](const bool input, const std::size_t index) {
    OnnxTensorInfo info;

    char *name = nullptr;
    if(input)
      throwIfOrtError(api.SessionGetInputName(impl_->session, index, impl_->allocator, &name),
                      "Ort SessionGetInputName");
    else
      throwIfOrtError(api.SessionGetOutputName(impl_->session, index, impl_->allocator, &name),
                      "Ort SessionGetOutputName");
    info.name = name ? name : "";
    impl_->allocator->Free(impl_->allocator, name);

    OrtTypeInfo *typeInfo = nullptr;
    if(input)
      throwIfOrtError(api.SessionGetInputTypeInfo(impl_->session, index, &typeInfo),
                      "Ort SessionGetInputTypeInfo");
    else
      throwIfOrtError(api.SessionGetOutputTypeInfo(impl_->session, index, &typeInfo),
                      "Ort SessionGetOutputTypeInfo");

    const OrtTensorTypeAndShapeInfo *tensorInfo = nullptr;
    throwIfOrtError(api.CastTypeInfoToTensorInfo(typeInfo, &tensorInfo),
                    "Ort CastTypeInfoToTensorInfo");

    ONNXTensorElementDataType elementType = ONNX_TENSOR_ELEMENT_DATA_TYPE_UNDEFINED;
    throwIfOrtError(api.GetTensorElementType(tensorInfo, &elementType),
                    "Ort GetTensorElementType");
    info.type = tensorElementTypeName(elementType);

    std::size_t dimCount = 0;
    throwIfOrtError(api.GetDimensionsCount(tensorInfo, &dimCount), "Ort GetDimensionsCount");
    info.shape.resize(dimCount);
    throwIfOrtError(api.GetDimensions(tensorInfo, info.shape.data(), dimCount), "Ort GetDimensions");

    api.ReleaseTypeInfo(typeInfo);
    return info;
  };

  std::size_t inputCount = 0;
  std::size_t outputCount = 0;
  throwIfOrtError(api.SessionGetInputCount(impl_->session, &inputCount), "Ort SessionGetInputCount");
  throwIfOrtError(api.SessionGetOutputCount(impl_->session, &outputCount), "Ort SessionGetOutputCount");
  for(std::size_t i = 0; i < inputCount; ++i) modelInfo_.inputs.push_back(readTensorInfo(true, i));
  for(std::size_t i = 0; i < outputCount; ++i) modelInfo_.outputs.push_back(readTensorInfo(false, i));
  constructionGuard.release();
}

OnnxSession::~OnnxSession()
{
  if(!impl_) return;
  const OrtApi &api = ortApi();
  if(impl_->memoryInfo) api.ReleaseMemoryInfo(impl_->memoryInfo);
  if(impl_->session) api.ReleaseSession(impl_->session);
  if(impl_->options) api.ReleaseSessionOptions(impl_->options);
  if(impl_->env) api.ReleaseEnv(impl_->env);
  delete impl_;
}

OnnxFloatRunResult OnnxSession::runSingleFloat(const std::vector<float> &input,
                                               const std::vector<std::int64_t> &inputShape) const
{
  if(modelInfo_.inputs.size() != 1 || modelInfo_.outputs.size() != 1)
  {
    throw std::runtime_error{"runSingleFloat requires exactly one input and one output"};
  }
  if(modelInfo_.inputs[0].type != "tensor(float)" || modelInfo_.outputs[0].type != "tensor(float)")
  {
    throw std::runtime_error{"runSingleFloat requires float input and output tensors"};
  }

  const auto product = [](const std::vector<std::int64_t> &shape) {
    return std::accumulate(shape.begin(), shape.end(), std::int64_t{1},
                           [](const std::int64_t a, const std::int64_t b) {
                             if(b <= 0) throw std::runtime_error{"Dynamic tensor shapes are not supported yet"};
                             return a * b;
                           });
  };

  const std::int64_t inputCount = product(inputShape);
  if(inputCount < 0 || input.size() != static_cast<std::size_t>(inputCount))
  {
    throw std::runtime_error{"Input tensor size does not match input shape"};
  }

  OnnxFloatRunResult result;
  result.shape = modelInfo_.outputs[0].shape;
  const std::int64_t outputCount = product(result.shape);
  result.values.assign(static_cast<std::size_t>(outputCount), 0.0f);

  const OrtApi &api = ortApi();
  OrtValue *inputTensor = nullptr;
  OrtValue *outputTensor = nullptr;

  try
  {
    throwIfOrtError(api.CreateTensorWithDataAsOrtValue(
                      impl_->memoryInfo,
                      const_cast<float *>(input.data()),
                      input.size() * sizeof(float),
                      inputShape.data(),
                      inputShape.size(),
                      ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT,
                      &inputTensor),
                    "Ort CreateTensorWithDataAsOrtValue input");

    throwIfOrtError(api.CreateTensorWithDataAsOrtValue(
                      impl_->memoryInfo,
                      result.values.data(),
                      result.values.size() * sizeof(float),
                      result.shape.data(),
                      result.shape.size(),
                      ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT,
                      &outputTensor),
                    "Ort CreateTensorWithDataAsOrtValue output");

    const char *inputNames[] = { modelInfo_.inputs[0].name.c_str() };
    const char *outputNames[] = { modelInfo_.outputs[0].name.c_str() };
    throwIfOrtError(api.Run(impl_->session,
                            nullptr,
                            inputNames,
                            &inputTensor,
                            1,
                            outputNames,
                            1,
                            &outputTensor),
                    "Ort Run");
  }
  catch(...)
  {
    if(outputTensor) api.ReleaseValue(outputTensor);
    if(inputTensor) api.ReleaseValue(inputTensor);
    throw;
  }

  api.ReleaseValue(outputTensor);
  api.ReleaseValue(inputTensor);
  return result;
}

std::vector<OnnxNamedFloatTensor> OnnxSession::runFloat(
  const std::vector<OnnxNamedFloatTensor> &inputs) const
{
  if(inputs.size() != modelInfo_.inputs.size())
  {
    throw std::runtime_error{"ONNX float input count does not match model"};
  }

  const auto product = [](const std::vector<std::int64_t> &shape) {
    return std::accumulate(shape.begin(), shape.end(), std::int64_t{1},
                           [](const std::int64_t a, const std::int64_t b) {
                             if(b <= 0) throw std::runtime_error{"Runtime tensor shape must be concrete"};
                             return a * b;
                           });
  };

  std::unordered_map<std::string, const OnnxNamedFloatTensor *> byName;
  for(const OnnxNamedFloatTensor &input : inputs)
  {
    if(!byName.emplace(input.name, &input).second)
    {
      throw std::runtime_error{"Duplicate ONNX input name: " + input.name};
    }
  }

  const OrtApi &api = ortApi();
  std::vector<const char *> inputNames;
  std::vector<OrtValue *> inputValues;
  std::vector<const char *> outputNames;
  std::vector<OrtValue *> outputValues(modelInfo_.outputs.size(), nullptr);
  std::vector<std::vector<std::uint16_t>> halfInputs;
  inputNames.reserve(modelInfo_.inputs.size());
  inputValues.reserve(modelInfo_.inputs.size());
  outputNames.reserve(modelInfo_.outputs.size());
  halfInputs.reserve(modelInfo_.inputs.size());

  try
  {
    for(const OnnxTensorInfo &expected : modelInfo_.inputs)
    {
      if(expected.type != "tensor(float)" && expected.type != "tensor(float16)")
        throw std::runtime_error{"runFloat requires float or float16 tensors"};
      const auto found = byName.find(expected.name);
      if(found == byName.end()) throw std::runtime_error{"Missing ONNX input: " + expected.name};
      const OnnxNamedFloatTensor &input = *found->second;
      if(input.values.size() != static_cast<std::size_t>(product(input.shape)))
      {
        throw std::runtime_error{"ONNX input size does not match shape: " + input.name};
      }

      OrtValue *value = nullptr;
      void *inputData = const_cast<float *>(input.values.data());
      std::size_t inputBytes = input.values.size() * sizeof(float);
      ONNXTensorElementDataType inputType = ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT;
      if(expected.type == "tensor(float16)")
      {
        halfInputs.emplace_back();
        auto &half = halfInputs.back();
        half.reserve(input.values.size());
        for(const float item : input.values) half.push_back(floatToHalf(item));
        inputData = half.data();
        inputBytes = half.size() * sizeof(std::uint16_t);
        inputType = ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT16;
      }
      throwIfOrtError(api.CreateTensorWithDataAsOrtValue(
                        impl_->memoryInfo, inputData, inputBytes,
                        input.shape.data(), input.shape.size(), inputType, &value),
                      "Ort CreateTensorWithDataAsOrtValue input");
      inputNames.push_back(expected.name.c_str());
      inputValues.push_back(value);
    }

    for(const OnnxTensorInfo &output : modelInfo_.outputs)
    {
      if(output.type != "tensor(float)" && output.type != "tensor(float16)")
        throw std::runtime_error{"runFloat requires float or float16 tensors"};
      outputNames.push_back(output.name.c_str());
    }

    throwIfOrtError(api.Run(impl_->session,
                            nullptr,
                            inputNames.data(),
                            inputValues.data(),
                            inputValues.size(),
                            outputNames.data(),
                            outputNames.size(),
                            outputValues.data()),
                    "Ort Run");

    std::vector<OnnxNamedFloatTensor> outputs;
    outputs.reserve(outputValues.size());
    for(std::size_t i = 0; i < outputValues.size(); ++i)
    {
      OrtTensorTypeAndShapeInfo *shapeInfo = nullptr;
      throwIfOrtError(api.GetTensorTypeAndShape(outputValues[i], &shapeInfo),
                      "Ort GetTensorTypeAndShape");
      std::size_t dimensions = 0;
      throwIfOrtError(api.GetDimensionsCount(shapeInfo, &dimensions), "Ort GetDimensionsCount");
      std::vector<std::int64_t> shape(dimensions);
      throwIfOrtError(api.GetDimensions(shapeInfo, shape.data(), dimensions), "Ort GetDimensions");
      std::size_t count = 0;
      throwIfOrtError(api.GetTensorShapeElementCount(shapeInfo, &count),
                      "Ort GetTensorShapeElementCount");
      api.ReleaseTensorTypeAndShapeInfo(shapeInfo);

      void *data = nullptr;
      throwIfOrtError(api.GetTensorMutableData(outputValues[i], &data),
                       "Ort GetTensorMutableData");
      std::vector<float> values(count);
      if(modelInfo_.outputs[i].type == "tensor(float)")
      {
        const auto *floats = static_cast<const float *>(data);
        values.assign(floats, floats + count);
      }
      else
      {
        const auto *halves = static_cast<const std::uint16_t *>(data);
        for(std::size_t j = 0; j < count; ++j) values[j] = halfToFloat(halves[j]);
      }
      outputs.push_back(OnnxNamedFloatTensor{
        modelInfo_.outputs[i].name,
        std::move(values),
        std::move(shape),
      });
    }

    for(OrtValue *value : outputValues) api.ReleaseValue(value);
    for(OrtValue *value : inputValues) api.ReleaseValue(value);
    return outputs;
  }
  catch(...)
  {
    for(OrtValue *value : outputValues) if(value) api.ReleaseValue(value);
    for(OrtValue *value : inputValues) if(value) api.ReleaseValue(value);
    throw;
  }
}

std::vector<OnnxProviderInfo> OnnxSession::availableProviders(const std::string &runtimeLibrary)
{
  const OrtApi &api = ortApi(runtimeLibrary);
  const std::vector<std::string> available = advertisedProviders(api);
  std::vector<OnnxProviderInfo> result{{"cpu", "CPU"}};
  for(const ProviderMapping &mapping : providers)
    if(isAdvertised(available, mapping)) result.push_back({mapping.id, mapping.displayName});
  return result;
}

OnnxRuntimeInfo OnnxSession::probeRuntime(const std::filesystem::path &runtimeLibrary)
{
  QLibrary library{QString::fromStdString(runtimeLibrary.string())};
  if(!library.load())
    throw std::runtime_error{"Could not load ONNX Runtime library '" + runtimeLibrary.string()
                             + "': " + library.errorString().toStdString()};
  using GetApiBase = const OrtApiBase *(*)();
  const auto getApiBase = reinterpret_cast<GetApiBase>(library.resolve("OrtGetApiBase"));
  if(!getApiBase) throw std::runtime_error{"ONNX Runtime library does not export OrtGetApiBase"};
  const OrtApiBase *base = getApiBase();
  if(!base || !base->GetApi(ORT_API_VERSION))
    throw std::runtime_error{"ONNX Runtime library has no compatible API"};
  OnnxRuntimeInfo result;
  result.version = base->GetVersionString() ? base->GetVersionString() : "unknown";
  const OrtApi &api = *base->GetApi(ORT_API_VERSION);
  const std::vector<std::string> advertised = advertisedProviders(api);
  result.providers.push_back({"cpu", "CPU"});
  for(const ProviderMapping &mapping : providers)
    if(isAdvertised(advertised, mapping)) result.providers.push_back({mapping.id, mapping.displayName});
  return result;
}

}
