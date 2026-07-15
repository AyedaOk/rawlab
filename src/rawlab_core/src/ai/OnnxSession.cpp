#include "rawlab/ai/OnnxSession.h"

#include <onnxruntime_c_api.h>

#include <memory>
#include <numeric>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace rawlab::ai {
namespace {

const OrtApi &ortApi()
{
  const OrtApi *api = OrtGetApiBase()->GetApi(ORT_API_VERSION);
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

} // namespace

struct OnnxSession::Impl
{
  OrtEnv *env = nullptr;
  OrtSessionOptions *options = nullptr;
  OrtSession *session = nullptr;
  OrtAllocator *allocator = nullptr;
  OrtMemoryInfo *memoryInfo = nullptr;
};

OnnxSession::OnnxSession(const std::filesystem::path &modelPath)
  : impl_{new Impl{}}
{
  const OrtApi &api = ortApi();
  modelInfo_.runtimeVersion = OrtGetApiBase()->GetVersionString();

  throwIfOrtError(api.CreateEnv(ORT_LOGGING_LEVEL_WARNING, "rawlab", &impl_->env), "Ort CreateEnv");
  throwIfOrtError(api.CreateSessionOptions(&impl_->options), "Ort CreateSessionOptions");
  throwIfOrtError(api.SetIntraOpNumThreads(impl_->options, 1), "Ort SetIntraOpNumThreads");
  throwIfOrtError(api.SetSessionGraphOptimizationLevel(impl_->options, ORT_ENABLE_ALL),
                  "Ort SetSessionGraphOptimizationLevel");
  throwIfOrtError(api.CreateSession(impl_->env, modelPath.string().c_str(), impl_->options,
                                    &impl_->session),
                  "Ort CreateSession");
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
  inputNames.reserve(modelInfo_.inputs.size());
  inputValues.reserve(modelInfo_.inputs.size());
  outputNames.reserve(modelInfo_.outputs.size());

  try
  {
    for(const OnnxTensorInfo &expected : modelInfo_.inputs)
    {
      if(expected.type != "tensor(float)") throw std::runtime_error{"runFloat requires float tensors"};
      const auto found = byName.find(expected.name);
      if(found == byName.end()) throw std::runtime_error{"Missing ONNX input: " + expected.name};
      const OnnxNamedFloatTensor &input = *found->second;
      if(input.values.size() != static_cast<std::size_t>(product(input.shape)))
      {
        throw std::runtime_error{"ONNX input size does not match shape: " + input.name};
      }

      OrtValue *value = nullptr;
      throwIfOrtError(api.CreateTensorWithDataAsOrtValue(
                        impl_->memoryInfo,
                        const_cast<float *>(input.values.data()),
                        input.values.size() * sizeof(float),
                        input.shape.data(),
                        input.shape.size(),
                        ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT,
                        &value),
                      "Ort CreateTensorWithDataAsOrtValue input");
      inputNames.push_back(expected.name.c_str());
      inputValues.push_back(value);
    }

    for(const OnnxTensorInfo &output : modelInfo_.outputs)
    {
      if(output.type != "tensor(float)") throw std::runtime_error{"runFloat requires float tensors"};
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

      float *data = nullptr;
      throwIfOrtError(api.GetTensorMutableData(outputValues[i], reinterpret_cast<void **>(&data)),
                      "Ort GetTensorMutableData");
      outputs.push_back(OnnxNamedFloatTensor{
        modelInfo_.outputs[i].name,
        std::vector<float>{data, data + count},
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

}
