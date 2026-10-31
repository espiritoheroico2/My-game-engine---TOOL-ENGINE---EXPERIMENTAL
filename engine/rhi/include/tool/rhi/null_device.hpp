// Dispositivo nulo com camada de validacao: nao desenha nada, mas confere o uso da API
// (handles obsoletos, desenhar sem pipeline/buffer, comandos fora de begin/end) e conta estatisticas.
// Serve para testes, servidor dedicado e como referencia para os backends reais.
#pragma once
#include "tool/rhi/rhi.hpp"

#include <unordered_map>

namespace tool::rhi {

class NullDevice final : public IDevice {
public:
  Backend backend() const override { return Backend::Null; }

  BufferId createBuffer(const BufferDesc& d, const void*) override {
    if (d.size == 0) { debug().report("TOOL-E130", Severity::Error, "buffer de tamanho 0"); return kNullHandle; }
    Handle h = ids_.create(); kind_[h.index] = Kind::Buffer; bufSize_[h.index] = d.size; return h;
  }
  TextureId createTexture(const TextureDesc& d, const void*) override {
    if (d.width == 0 || d.height == 0) { debug().report("TOOL-E131", Severity::Error, "textura com dimensao 0"); return kNullHandle; }
    Handle h = ids_.create(); kind_[h.index] = Kind::Texture; return h;
  }
  ShaderId createShader(const ShaderDesc& d) override {
    if (d.bytecode.empty() && d.source.empty()) { debug().report("TOOL-E132", Severity::Error, "shader sem codigo"); return kNullHandle; }
    Handle h = ids_.create(); kind_[h.index] = Kind::Shader; return h;
  }
  PipelineId createPipeline(const PipelineDesc& d) override {
    if (!valid(d.vs, Kind::Shader) || !valid(d.ps, Kind::Shader)) {
      debug().report("TOOL-E133", Severity::Error, "pipeline com shader invalido"); return kNullHandle;
    }
    Handle h = ids_.create(); kind_[h.index] = Kind::Pipeline; return h;
  }
  void destroy(Handle h) override {
    if (!ids_.destroy(h)) debug().report(diag::kStaleHandle, Severity::Warning, "RHI destroy() com handle obsoleto");
  }
  void updateBuffer(BufferId b, const void*, size_t size) override {
    if (!valid(b, Kind::Buffer)) { debug().report(diag::kStaleHandle, Severity::Error, "updateBuffer em handle invalido"); return; }
    if (size > bufSize_[b.index]) { debug().report("TOOL-E134", Severity::Error, "updateBuffer maior que o buffer"); return; }
    ++stats_.bufferUploads;
  }

  std::unique_ptr<ICommandList> beginCommands() override { return std::make_unique<Cmd>(*this); }
  void submit(ICommandList&) override {}
  void present() override { last_ = stats_; stats_ = {}; }
  DrawStats frameStats() const override { return last_; }

  size_t liveResources() const { return ids_.liveCount(); }

private:
  enum class Kind : u8 { Buffer, Texture, Shader, Pipeline };
  bool valid(Handle h, Kind k) const { auto it = kind_.find(h.index); return ids_.alive(h) && it != kind_.end() && it->second == k; }

  class Cmd final : public ICommandList {
  public:
    explicit Cmd(NullDevice& d) : d_(d) {}
    void begin(TextureId t, TextureId, f32, f32, f32, f32) override {
      if (open_) d_.err("begin() dentro de begin()");
      open_ = true;
      if (!d_.valid(t, Kind::Texture)) d_.err("begin() com alvo invalido");
    }
    void setPipeline(PipelineId p) override {
      if (!check()) return;
      if (!d_.valid(p, Kind::Pipeline)) { d_.err("setPipeline com handle invalido"); pipeline_ = false; return; }
      pipeline_ = true; ++d_.stats_.pipelineSwitches;
    }
    void setVertexBuffer(BufferId b) override { if (check()) vb_ = d_.valid(b, Kind::Buffer); if (open_ && !vb_) d_.err("vertex buffer invalido"); }
    void setIndexBuffer(BufferId b) override { if (check()) ib_ = d_.valid(b, Kind::Buffer); if (open_ && !ib_) d_.err("index buffer invalido"); }
    void setUniform(u32, BufferId b) override { if (check() && !d_.valid(b, Kind::Buffer)) d_.err("uniform buffer invalido"); }
    void setTexture(u32, TextureId t) override { if (check() && !d_.valid(t, Kind::Texture)) d_.err("textura invalida"); }
    void drawIndexed(u32 n, u32 inst) override {
      if (!check()) return;
      if (!pipeline_ || !vb_ || !ib_) { d_.err("draw sem pipeline/vertex/index buffer"); return; }
      ++d_.stats_.draws; d_.stats_.triangles += (n / 3) * inst;
    }
    void end() override { if (!open_) d_.err("end() sem begin()"); open_ = false; }

  private:
    bool check() { if (!open_) d_.err("comando fora de begin()/end()"); return open_; }
    NullDevice& d_;
    bool open_ = false, pipeline_ = false, vb_ = false, ib_ = false;
  };

  void err(const char* m) { debug().report("TOOL-E135", Severity::Error, std::string("RHI: ") + m); }

  HandleAllocator ids_;
  std::unordered_map<u32, Kind> kind_;
  std::unordered_map<u32, size_t> bufSize_;
  DrawStats stats_, last_;
};

inline std::unique_ptr<IDevice> createNullDevice() { return std::make_unique<NullDevice>(); }

} // namespace tool::rhi
