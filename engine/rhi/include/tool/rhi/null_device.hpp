// Dispositivo nulo com camada de validacao: nao desenha nada, mas confere o uso da API
// (handles obsoletos, desenhar sem pipeline/buffer, comandos fora de begin/end) e conta estatisticas.
// Serve para testes, servidor dedicado e como referencia para os backends reais.
#pragma once
#include "tool/rhi/rhi.hpp"

#include <unordered_map>

namespace tool::rhi {

// Backend Null: implementa IDevice sem desenhar nada. So valida o uso da API e conta estatisticas.
class NullDevice final : public IDevice {
public:
  // Sempre Backend::Null.
  Backend backend() const override { return Backend::Null; }

  // Registra um buffer (sem GPU). Tamanho 0 gera erro TOOL-E130 e handle nulo.
  BufferId createBuffer(const BufferDesc& d, const void*) override {
    if (d.size == 0) { debug().report("TOOL-E130", Severity::Error, "buffer de tamanho 0"); return kNullHandle; }
    Handle h = ids_.create(); kind_[h.index] = Kind::Buffer; bufSize_[h.index] = d.size; return h;
  }
  // Registra uma textura. Dimensao 0 gera erro TOOL-E131. Os dados iniciais sao ignorados.
  TextureId createTexture(const TextureDesc& d, const void*) override {
    if (d.width == 0 || d.height == 0) { debug().report("TOOL-E131", Severity::Error, "textura com dimensao 0"); return kNullHandle; }
    Handle h = ids_.create(); kind_[h.index] = Kind::Texture; return h;
  }
  // Registra um shader. Sem bytecode nem codigo gera erro TOOL-E132.
  ShaderId createShader(const ShaderDesc& d) override {
    if (d.bytecode.empty() && d.source.empty()) { debug().report("TOOL-E132", Severity::Error, "shader sem codigo"); return kNullHandle; }
    Handle h = ids_.create(); kind_[h.index] = Kind::Shader; return h;
  }
  // Registra um pipeline. Exige vs e ps validos, senao erro TOOL-E133.
  PipelineId createPipeline(const PipelineDesc& d) override {
    if (!valid(d.vs, Kind::Shader) || !valid(d.ps, Kind::Shader)) {
      debug().report("TOOL-E133", Severity::Error, "pipeline com shader invalido"); return kNullHandle;
    }
    Handle h = ids_.create(); kind_[h.index] = Kind::Pipeline; return h;
  }
  // Libera o handle. Handle obsoleto gera aviso, nao erro.
  void destroy(Handle h) override {
    if (!ids_.destroy(h)) debug().report(diag::kStaleHandle, Severity::Warning, "RHI destroy() com handle obsoleto");
  }
  // Valida o upload (handle e tamanho) e conta bufferUploads, sem copiar nada.
  void updateBuffer(BufferId b, const void*, size_t size) override {
    if (!valid(b, Kind::Buffer)) { debug().report(diag::kStaleHandle, Severity::Error, "updateBuffer em handle invalido"); return; }
    if (size > bufSize_[b.index]) { debug().report("TOOL-E134", Severity::Error, "updateBuffer maior que o buffer"); return; }
    ++stats_.bufferUploads;
  }

  // Cria uma lista de comandos que valida o uso (begin/end, handles, pipeline).
  std::unique_ptr<ICommandList> beginCommands() override { return std::make_unique<Cmd>(*this); }
  // Conta a submissao. Nao executa nada.
  void submit(ICommandList&) override { ++stats_.submitCount; }
  // Zera as estatisticas e marca o quadro como ativo.
  void beginFrame() override { stats_ = {}; frameActive_ = true; }
  // Finaliza o quadro: chama present() e desativa a acumulacao.
  void endFrame() override { present(); frameActive_ = false; }
  // Copia as estatisticas para last_ e zera as atuais.
  void present() override { last_ = stats_; stats_ = {}; }
  // Durante o quadro, as estatisticas atuais. Fora dele, as do ultimo quadro apresentado.
  DrawStats frameStats() const override { return frameActive_ ? stats_ : last_; }

  // Quantidade de handles ainda vivos (util para achar vazamentos em testes).
  size_t liveResources() const { return ids_.liveCount(); }

private:
  bool frameActive_ = false;
  // Tipo do recurso associado a cada handle.
  enum class Kind : u8 { Buffer, Texture, Shader, Pipeline };
  // Verdadeiro se o handle esta vivo e e do tipo k.
  bool valid(Handle h, Kind k) const { auto it = kind_.find(h.index); return ids_.alive(h) && it != kind_.end() && it->second == k; }

  // Lista de comandos do NullDevice: valida cada chamada entre begin() e end().
  // Atenção: setTargets() e setStorage() nao sao sobrescritos aqui, entao nao sao validados.
  class Cmd final : public ICommandList {
  public:
    // Liga a lista ao dispositivo que a criou.
    explicit Cmd(NullDevice& d) : d_(d) {}
    // Abre a lista. Valida alvo e profundidade. begin() dentro de begin() gera erro.
    void begin(TextureId t, TextureId depth, f32, f32, f32, f32) override {
      if (open_) d_.err("begin() dentro de begin()");
      open_ = true;
      if (!d_.valid(t, Kind::Texture)) d_.err("begin() com alvo invalido");
      hasDepth_ = !depth.isNull();
      if (hasDepth_ && !d_.valid(depth, Kind::Texture)) d_.err("begin() com profundidade invalida");
    }
    // Como begin(), mas sem limpar a cor.
    void beginKeep(TextureId t, TextureId d) override {
      if (open_) d_.err("beginKeep() dentro de begin()");
      open_ = true;
      if (!d_.valid(t, Kind::Texture)) d_.err("beginKeep() com alvo invalido");
      hasDepth_ = !d.isNull();
      if (hasDepth_ && !d_.valid(d, Kind::Texture)) d_.err("beginKeep() com profundidade invalida");
    }
    // Valida que existe alvo de profundidade.
    void clearDepth(float) override { if (check() && !hasDepth_) d_.err("clearDepth sem alvo de profundidade"); }
    // Valida o pipeline e marca que ha um pipeline ligado.
    void setPipeline(PipelineId p) override {
      if (!check()) return;
      if (!d_.valid(p, Kind::Pipeline)) { d_.err("setPipeline com handle invalido"); pipeline_ = false; return; }
      pipeline_ = true; ++d_.stats_.pipelineSwitches;
    }
    // Valida o vertex buffer.
    void setVertexBuffer(BufferId b) override { if (check()) vb_ = d_.valid(b, Kind::Buffer); if (open_ && !vb_) d_.err("vertex buffer invalido"); }
    // Valida o index buffer.
    void setIndexBuffer(BufferId b) override { if (check()) ib_ = d_.valid(b, Kind::Buffer); if (open_ && !ib_) d_.err("index buffer invalido"); }
    // Valida o uniform buffer (o slot nao e checado aqui).
    void setUniform(u32, BufferId b) override { if (check() && !d_.valid(b, Kind::Buffer)) d_.err("uniform buffer invalido"); }
    // Valida a textura (o slot nao e checado aqui).
    void setTexture(u32, TextureId t) override { if (check() && !d_.valid(t, Kind::Texture)) d_.err("textura invalida"); }
    // So verifica o estado da lista. O viewport nao e validado.
    void setViewport(u32, u32, u32, u32) override { check(); }
    // Conta um draw e seus triangulos (n/3 * instancias), se pipeline e buffers estao ligados.
    void drawIndexed(u32 n, u32 inst) override {
      if (!check()) return;
      if (!pipeline_ || !vb_ || !ib_) { d_.err("draw sem pipeline/vertex/index buffer"); return; }
      ++d_.stats_.draws; d_.stats_.triangles += (n / 3) * inst;
    }
    // Valida que o scissor nao tem area vazia.
    void setScissor(u32, u32, u32 w, u32 h) override { if (check() && (w == 0 || h == 0)) d_.err("setScissor com area vazia"); }
    // Como drawIndexed(), mas exige n > 0 indices.
    void drawIndexedRange(u32, u32 n, u32 inst) override {
      if (!check()) return;
      if (!pipeline_ || !vb_ || !ib_) { d_.err("draw sem pipeline/vertex/index buffer"); return; }
      if (n == 0) { d_.err("drawIndexedRange sem indices"); return; }
      ++d_.stats_.draws; d_.stats_.triangles += (n / 3) * inst;
    }
    // Fecha a lista. end() sem begin() gera erro.
    void end() override { if (!open_) d_.err("end() sem begin()"); open_ = false; hasDepth_ = false; }

  private:
    // Confirma que a lista esta aberta entre begin() e end(). Senao gera erro.
    bool check() { if (!open_) d_.err("comando fora de begin()/end()"); return open_; }
    NullDevice& d_;
    bool open_ = false, hasDepth_ = false, pipeline_ = false, vb_ = false, ib_ = false;
  };

  // Reporta erro de uso da API (codigo TOOL-E135).
  void err(const char* m) { debug().report("TOOL-E135", Severity::Error, std::string("RHI: ") + m); }

  HandleAllocator ids_;
  std::unordered_map<u32, Kind> kind_;
  std::unordered_map<u32, size_t> bufSize_;
  DrawStats stats_, last_;
};

// Cria um NullDevice (sem GPU).
inline std::unique_ptr<IDevice> createNullDevice() { return std::make_unique<NullDevice>(); }

} // namespace tool::rhi
