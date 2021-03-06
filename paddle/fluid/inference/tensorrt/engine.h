/* Copyright (c) 2018 PaddlePaddle Authors. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License. */

#pragma once

#include <NvInfer.h>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include "paddle/fluid/inference/engine.h"
#include "paddle/fluid/inference/tensorrt/helper.h"

namespace paddle {
namespace inference {
namespace tensorrt {

/*
 * TensorRT Engine.
 *
 * There are two alternative ways to use it, one is  to build from a paddle
 * protobuf model, another way is to manully construct the network.
 */
class TensorRTEngine : public EngineBase {
 public:
  // Weight is model parameter.
  class Weight {
   public:
    Weight(nvinfer1::DataType dtype, void* value, int num_elem) {
      w_.type = dtype;
      w_.values = value;
      w_.count = num_elem;
    }
    const nvinfer1::Weights& get() { return w_; }

   private:
    nvinfer1::Weights w_;
  };

  TensorRTEngine(int max_batch, int max_workspace, cudaStream_t* stream,
                 nvinfer1::ILogger& logger = NaiveLogger::Global())
      : max_batch_(max_batch),
        max_workspace_(max_workspace),
        stream_(stream),
        logger_(logger) {}

  virtual ~TensorRTEngine();

  // TODO(Superjomn) implement it later when graph segmentation is supported.
  void Build(const DescType& paddle_model) override;

  void Execute(int batch_size) override;

  // Initialize the inference network, so that TensorRT layers can add to this
  // network.
  void InitNetwork() {
    infer_builder_.reset(createInferBuilder(&logger_));
    infer_network_.reset(infer_builder_->createNetwork());
  }
  // After finishing adding ops, freeze this network and creates the executation
  // environment.
  void FreezeNetwork();

  // Add an input and set its name, data type and dimention.
  nvinfer1::ITensor* DeclareInput(const std::string& name,
                                  nvinfer1::DataType dtype,
                                  const nvinfer1::Dims& dim);
  // Set the offset-th output from a layer as the network's output, and set its
  // name.
  void DeclareOutput(const nvinfer1::ILayer* layer, int offset,
                     const std::string& name);

  // GPU memory address for an ITensor with specific name. One can operate on
  // these memory directly for acceleration, for example, output the converted
  // data directly to the buffer to save data copy overhead.
  // NOTE this should be used after calling `FreezeNetwork`.
  void*& buffer(const std::string& name);

  // Fill an input from CPU memory with name and size.
  void SetInputFromCPU(const std::string& name, void* data, size_t size);
  // TODO(Superjomn) is this method necessary given that buffer(xxx) can be
  // accessed directly. Fill an input from GPU memory with name and size.
  void SetInputFromGPU(const std::string& name, void* data, size_t size);
  // Get an output called name, the output of tensorrt is in GPU, so this method
  // will just return the output's GPU memory address.
  void* GetOutputInGPU(const std::string& name);
  // LOW EFFICENCY! Get output to CPU, this will trigger a memory copy from GPU
  // to CPU.
  void GetOutputInCPU(const std::string& name, void* dst, size_t max_size);

  nvinfer1::ICudaEngine* engine() { return infer_engine_.get(); }
  nvinfer1::INetworkDefinition* network() { return infer_network_.get(); }

 private:
  // the max batch size
  int max_batch_;
  // the max memory size the engine uses
  int max_workspace_;
  cudaStream_t* stream_;
  nvinfer1::ILogger& logger_;

  std::vector<void*> buffers_;
  // max data size for the buffers.
  std::unordered_map<std::string /*name*/, size_t /*max size*/> buffer_sizes_;

  // TensorRT related internal members
  template <typename T>
  struct Destroyer {
    void operator()(T* x) { x->destroy(); }
  };
  template <typename T>
  using infer_ptr = std::unique_ptr<T, Destroyer<T>>;
  infer_ptr<nvinfer1::IBuilder> infer_builder_;
  infer_ptr<nvinfer1::INetworkDefinition> infer_network_;
  infer_ptr<nvinfer1::ICudaEngine> infer_engine_;
  infer_ptr<nvinfer1::IExecutionContext> infer_context_;
};  // class TensorRTEngine

// Add an layer__ into engine__ with args ARGS.
// For example:
//   TRT_ENGINE_ADD_LAYER(xxx, FullyConnected, input, dim, weights, bias)
//
// Reference
// https://docs.nvidia.com/deeplearning/sdk/tensorrt-developer-guide/index.html#charRNN_define_network
//
// will add a fully connected layer into the engine.
// TensorRT has too many layers, so that is not wise to add member functions for
// them, and an macro like this is more extensible when underlying TensorRT
// library add new layer supports.
#define TRT_ENGINE_ADD_LAYER(engine__, layer__, ARGS...) \
  engine__->network()->add##layer__(ARGS);

}  // namespace tensorrt
}  // namespace inference
}  // namespace paddle
