#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace rawlab::ai {

struct OnnxTensorInfo
{
  std::string name;
  std::string type;
  std::vector<std::int64_t> shape;
};

struct OnnxModelInfo
{
  std::string runtimeVersion;
  std::string executionProvider;
  std::vector<OnnxTensorInfo> inputs;
  std::vector<OnnxTensorInfo> outputs;
};

struct OnnxSessionOptions
{
  std::string provider = "auto";
  int deviceId = 0;
  std::string runtimeLibrary;
};

struct OnnxProviderInfo
{
  std::string id;
  std::string name;
};

struct OnnxRuntimeInfo
{
  std::string version;
  std::vector<OnnxProviderInfo> providers;
};

struct OnnxFloatRunResult
{
  std::vector<float> values;
  std::vector<std::int64_t> shape;
};

struct OnnxNamedFloatTensor
{
  std::string name;
  std::vector<float> values;
  std::vector<std::int64_t> shape;
};

class OnnxSession
{
public:
  explicit OnnxSession(const std::filesystem::path &modelPath,
                       const OnnxSessionOptions &options = {});
  ~OnnxSession();

  OnnxSession(const OnnxSession &) = delete;
  OnnxSession &operator=(const OnnxSession &) = delete;
  OnnxSession(OnnxSession &&) = delete;
  OnnxSession &operator=(OnnxSession &&) = delete;

  const OnnxModelInfo &modelInfo() const { return modelInfo_; }
  static std::vector<OnnxProviderInfo> availableProviders(const std::string &runtimeLibrary = {});
  static OnnxRuntimeInfo probeRuntime(const std::filesystem::path &runtimeLibrary);
  OnnxFloatRunResult runSingleFloat(const std::vector<float> &input,
                                    const std::vector<std::int64_t> &inputShape) const;
  std::vector<OnnxNamedFloatTensor> runFloat(
    const std::vector<OnnxNamedFloatTensor> &inputs) const;

private:
  struct Impl;
  Impl *impl_ = nullptr;
  OnnxModelInfo modelInfo_;
};

}
