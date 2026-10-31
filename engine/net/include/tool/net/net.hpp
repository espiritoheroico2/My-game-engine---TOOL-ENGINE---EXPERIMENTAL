// ToolEngine - NetTool: camada de rede independente de transporte.
//   Transport (UDP/WebRTC DataChannel/loopback) -> Connection (confiavel e ordenada, dedup, janela)
//   -> NetSession (autoridade do host, donos de objetos, limite de taxa, anti-spoof).
// O transporte real (libdatachannel/WebRTC com DTLS) so implementa ITransport; toda a logica abaixo
// e testada contra um link simulado com perda, duplicacao e reordenacao.
#pragma once
#include "tool/core/json.hpp"
#include "tool/debug/debug.hpp"

#include <algorithm>
#include <cstring>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace tool::net {

using Bytes = std::vector<u8>;

namespace code {
inline constexpr const char* kBadPacket = "TOOL-E220";
inline constexpr const char* kSpoof = "TOOL-E221";
inline constexpr const char* kRateLimit = "TOOL-W220";
inline constexpr const char* kTooBig = "TOOL-E222";
} // namespace code

inline u32 crc32(const u8* d, size_t n) {
  static u32 table[256]; static bool init = false;
  if (!init) { for (u32 i = 0; i < 256; ++i) { u32 c = i; for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1; table[i] = c; } init = true; }
  u32 c = 0xFFFFFFFFu;
  for (size_t i = 0; i < n; ++i) c = table[(c ^ d[i]) & 0xFF] ^ (c >> 8);
  return c ^ 0xFFFFFFFFu;
}

// Leitor com checagem de limites: qualquer leitura alem do fim marca erro (nunca le fora do buffer).
class Reader {
public:
  Reader(const u8* d, size_t n) : d_(d), n_(n) {}
  bool ok() const { return ok_; }
  size_t remaining() const { return n_ - p_; }
  u8 u8_() { return take(1) ? d_[p_ - 1] : 0; }
  u16 u16_() { if (!take(2)) return 0; return static_cast<u16>(d_[p_ - 2] | (d_[p_ - 1] << 8)); }
  u32 u32_() { if (!take(4)) return 0; return static_cast<u32>(d_[p_ - 4]) | (static_cast<u32>(d_[p_ - 3]) << 8) | (static_cast<u32>(d_[p_ - 2]) << 16) | (static_cast<u32>(d_[p_ - 1]) << 24); }
  Bytes bytes(size_t n) { if (!take(n)) return {}; return Bytes(d_ + p_ - n, d_ + p_); }
private:
  bool take(size_t n) { if (!ok_ || n > n_ - p_) { ok_ = false; return false; } p_ += n; return true; }
  const u8* d_; size_t n_, p_ = 0; bool ok_ = true;
};
struct Writer {
  Bytes b;
  void u8_(u8 v) { b.push_back(v); }
  void u16_(u16 v) { b.push_back(static_cast<u8>(v)); b.push_back(static_cast<u8>(v >> 8)); }
  void u32_(u32 v) { for (int i = 0; i < 4; ++i) b.push_back(static_cast<u8>(v >> (8 * i))); }
  void bytes(const Bytes& v) { b.insert(b.end(), v.begin(), v.end()); }
};

class ITransport {
public:
  virtual ~ITransport() = default;
  virtual void send(const Bytes& datagram) = 0;
  virtual std::optional<Bytes> receive() = 0;
};

// Link simulado entre dois pontos: perda, duplicacao e reordenacao deterministicas (PRNG fixo).
class SimLink {
public:
  struct Cfg { double loss = 0, dup = 0, reorder = 0; u32 seed = 1; };
  SimLink() : SimLink(Cfg{}) {}
  explicit SimLink(Cfg c) : cfg_(c), rng_(c.seed) {}
  class End final : public ITransport {
  public:
    End(SimLink* l, int side) : l_(l), side_(side) {}
    void send(const Bytes& d) override { l_->push(1 - side_, d); }
    std::optional<Bytes> receive() override { auto& q = l_->q_[side_]; if (q.empty()) return std::nullopt; Bytes out = std::move(q.front()); q.pop_front(); return out; }
  private:
    SimLink* l_; int side_;
  };
  End a{this, 0}, b{this, 1};
private:
  double rnd() { rng_ = rng_ * 1664525u + 1013904223u; return static_cast<double>(rng_ >> 8) / 16777216.0; }
  void push(int to, const Bytes& d) {
    if (rnd() < cfg_.loss) return;
    auto& q = q_[to];
    if (rnd() < cfg_.reorder && !q.empty()) q.insert(q.begin(), d); else q.push_back(d);
    if (rnd() < cfg_.dup) q.push_back(d);
  }
  Cfg cfg_; u32 rng_; std::deque<Bytes> q_[2];
};

// Conexao confiavel e ordenada sobre transporte nao confiavel.
// Pacote: magic u16 | tipo u8 | ack u32 (proximo msgId esperado) | nMsgs u8 | [id u32, len u16, dados]* | crc u32
class Connection {
public:
  static constexpr u16 kMagic = 0x544E; // "NT"
  static constexpr size_t kMaxMessage = 1024;
  static constexpr size_t kMaxPacket = 1200;
  static constexpr size_t kWindow = 256; // msgIds a frente do esperado que aceitamos

  explicit Connection(ITransport& t) : t_(t) {}
  std::function<void(const Bytes&)> onMessage;

  bool send(const Bytes& msg) {
    if (msg.size() > kMaxMessage) { debug().report(code::kTooBig, Severity::Error, "mensagem maior que o limite"); return false; }
    out_.push_back({nextOut_++, msg});
    return true;
  }

  // Chamar todo tick: reenvia o que ainda nao foi confirmado e processa o recebido.
  void update() {
    while (auto d = t_.receive()) handle(*d);
    flush();
  }
  size_t unacked() const { return out_.size(); }
  u32 duplicatesDropped() const { return dups_; }
  u32 badPackets() const { return bad_; }

private:
  struct Msg { u32 id; Bytes data; };

  void flush() {
    Writer w; w.u16_(kMagic); w.u8_(1); w.u32_(expectIn_);
    const size_t countPos = w.b.size(); w.u8_(0);
    u8 n = 0;
    for (auto& m : out_) {
      if (n == 255 || w.b.size() + 6 + m.data.size() + 4 > kMaxPacket) break;
      w.u32_(m.id); w.u16_(static_cast<u16>(m.data.size())); w.bytes(m.data); ++n;
    }
    w.b[countPos] = n;
    if (n == 0 && !ackDirty_) return; // nada a dizer
    ackDirty_ = false;
    w.u32_(crc32(w.b.data(), w.b.size()));
    t_.send(w.b);
  }

  void handle(const Bytes& d) {
    if (d.size() < 12 || d.size() > kMaxPacket) { ++bad_; return; }
    Reader tail(d.data() + d.size() - 4, 4);
    if (tail.u32_() != crc32(d.data(), d.size() - 4)) { ++bad_; debug().report(code::kBadPacket, Severity::Warning, "pacote com CRC invalido descartado"); return; }
    Reader r(d.data(), d.size() - 4);
    if (r.u16_() != kMagic || r.u8_() != 1) { ++bad_; return; }
    const u32 ack = r.u32_(); const u8 n = r.u8_();
    std::vector<Msg> got;
    for (u8 i = 0; i < n; ++i) {
      Msg m; m.id = r.u32_(); const u16 len = r.u16_();
      if (len > kMaxMessage) { r = Reader(nullptr, 0); r.u8_(); break; }
      m.data = r.bytes(len);
      if (!r.ok()) break;
      got.push_back(std::move(m));
    }
    if (!r.ok()) { ++bad_; debug().report(code::kBadPacket, Severity::Warning, "pacote malformado descartado"); return; }
    while (!out_.empty() && out_.front().id < ack) out_.pop_front(); // confirmados
    for (auto& m : got) {
      ackDirty_ = true;
      if (m.id < expectIn_ || pending_.count(m.id)) { ++dups_; continue; }
      if (m.id >= expectIn_ + kWindow) continue; // longe demais: ignora (anti-DoS de memoria)
      pending_[m.id] = std::move(m.data);
    }
    for (auto it = pending_.find(expectIn_); it != pending_.end(); it = pending_.find(expectIn_)) {
      Bytes b = std::move(it->second); pending_.erase(it); ++expectIn_;
      if (onMessage) onMessage(b);
    }
  }

  ITransport& t_;
  std::deque<Msg> out_;
  u32 nextOut_ = 0, expectIn_ = 0;
  std::map<u32, Bytes> pending_;
  bool ackDirty_ = false;
  u32 dups_ = 0, bad_ = 0;
};

// ---- Sessao: autoridade do host, donos, limite de taxa --------------------------------------------
using PeerId = u32;
using NetId = u32;

class TokenBucket {
public:
  TokenBucket(double perSecond, double burst) : rate_(perSecond), cap_(burst), tokens_(burst) {}
  bool take(double now, double cost = 1) {
    tokens_ = std::min(cap_, tokens_ + (now - last_) * rate_); last_ = now;
    if (tokens_ < cost) return false;
    tokens_ -= cost; return true;
  }
private:
  double rate_, cap_, tokens_, last_ = 0;
};

class NetSession {
public:
  explicit NetSession(bool isHost) : host_(isHost) {}

  NetId registerObject(PeerId owner, Json state = Json::object()) { const NetId id = ++nextId_; objs_[id] = {owner, std::move(state)}; return id; }
  void setPeerRate(PeerId p, double perSecond, double burst) { buckets_.erase(p); buckets_.emplace(p, TokenBucket(perSecond, burst)); }

  // Atualizacao recebida de `from`. So o dono altera o estado (ou o host, que e a autoridade).
  bool applyUpdate(PeerId from, NetId id, const Json& state, double now) {
    auto it = objs_.find(id);
    if (it == objs_.end()) { debug().report(code::kSpoof, Severity::Warning, "update para objeto inexistente"); return false; }
    auto b = buckets_.find(from);
    if (b != buckets_.end() && !b->second.take(now)) { debug().report(code::kRateLimit, Severity::Warning, "peer " + std::to_string(from) + " excedeu o limite de taxa"); return false; }
    if (it->second.owner != from && !(host_ && from == kHostPeer)) {
      debug().report(code::kSpoof, Severity::Error, "peer " + std::to_string(from) + " tentou alterar objeto de " + std::to_string(it->second.owner));
      return false;
    }
    if (!state.isObject()) return false;
    for (auto& [k, v] : state.members()) it->second.state.set(k, v);
    ++it->second.version;
    return true;
  }

  // Delta desde `sinceVersion` por objeto: so envia o que mudou.
  Json snapshot(const std::map<NetId, u32>& known) const {
    Json out = Json::object();
    for (auto& [id, o] : objs_) {
      auto k = known.find(id);
      if (k != known.end() && k->second == o.version) continue;
      Json e = Json::object(); e.set("v", static_cast<double>(o.version)); e.set("s", o.state);
      out.set(std::to_string(id), e);
    }
    return out;
  }
  const Json* state(NetId id) const { auto it = objs_.find(id); return it == objs_.end() ? nullptr : &it->second.state; }
  u32 version(NetId id) const { auto it = objs_.find(id); return it == objs_.end() ? 0 : it->second.version; }

  static constexpr PeerId kHostPeer = 0;

private:
  struct Obj { PeerId owner; Json state; u32 version = 0; };
  bool host_;
  NetId nextId_ = 0;
  std::map<NetId, Obj> objs_;
  std::map<PeerId, TokenBucket> buckets_;
};

} // namespace tool::net
