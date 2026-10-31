// ToolEngine - descompressor zlib/DEFLATE (RFC 1950/1951) proprio, sem dependencias.
//
// Por que existe: arrays de vertices em arquivos FBX vem comprimidos com zlib. Em vez de adicionar uma
// biblioteca (a engine precisa ser leve e portatil para PC/Android/Web), este arquivo implementa o
// algoritmo "puff" (Mark Adler) de forma compacta e segura.
//
// Seguranca (arquivos de modelo sao entrada nao confiavel, vide documento, secao de seguranca):
//  * `maxOut` limita a saida: um "zip bomb" nao consegue estourar a memoria;
//  * todo acesso a entrada/saida e conferido; qualquer inconsistencia devolve false (fail-closed);
//  * o Adler-32 do trailer e conferido, entao dados corrompidos sao rejeitados.
#pragma once
#include "tool/core/types.hpp"

#include <string>
#include <vector>

namespace tool::assets {

namespace detail {

// Tabela de Huffman canonica: `count[len]` = quantos codigos tem esse comprimento;
// `symbol` = simbolos ordenados por (comprimento, valor). Decodificar = percorrer os comprimentos.
struct Huffman {
  u16 count[16] = {};
  u16 symbol[288] = {};
};

// Constroi a tabela a partir dos comprimentos. Retorna 0 se completa, <0 se super-assinada (invalida),
// >0 se incompleta (permitido so em casos especiais do formato).
inline int buildHuffman(Huffman& h, const u16* lengths, int n) {
  for (auto& c : h.count) c = 0;
  for (int s = 0; s < n; ++s) ++h.count[lengths[s]];
  if (h.count[0] == n) return 0; // sem codigos
  int left = 1;
  for (int len = 1; len < 16; ++len) {
    left <<= 1;
    left -= h.count[len];
    if (left < 0) return left; // mais codigos do que o possivel
  }
  u16 offs[16] = {};
  for (int len = 1; len < 15; ++len) offs[len + 1] = static_cast<u16>(offs[len] + h.count[len]);
  for (int s = 0; s < n; ++s)
    if (lengths[s] != 0) h.symbol[offs[lengths[s]]++] = static_cast<u16>(s);
  return left;
}

class BitReader {
public:
  BitReader(const u8* d, size_t n) : d_(d), n_(n) {}
  bool ok() const { return ok_; }
  void fail() { ok_ = false; }
  // Le `need` bits (LSB primeiro, como o DEFLATE exige).
  u32 bits(int need) {
    u32 val = bitbuf_;
    while (bitcnt_ < need) {
      if (pos_ >= n_) { ok_ = false; return 0; }
      val |= static_cast<u32>(d_[pos_++]) << bitcnt_;
      bitcnt_ += 8;
    }
    bitbuf_ = val >> need;
    bitcnt_ -= need;
    return val & ((1u << need) - 1u);
  }
  // Descarta os bits restantes do byte atual (blocos "stored" comecam alinhados).
  void alignByte() { bitbuf_ = 0; bitcnt_ = 0; }
  const u8* cursor() const { return d_ + pos_; }
  size_t remaining() const { return n_ - pos_; }
  void skip(size_t k) { pos_ += k; }
  size_t position() const { return pos_; }

private:
  const u8* d_;
  size_t n_, pos_ = 0;
  u32 bitbuf_ = 0;
  int bitcnt_ = 0;
  bool ok_ = true;
};

// Decodifica um simbolo; -1 = codigo invalido / fim dos dados.
inline int decodeSymbol(BitReader& br, const Huffman& h) {
  int code = 0, first = 0, index = 0;
  for (int len = 1; len < 16; ++len) {
    code |= static_cast<int>(br.bits(1));
    if (!br.ok()) return -1;
    const int count = h.count[len];
    if (code - count < first) return h.symbol[index + (code - first)];
    index += count;
    first += count;
    first <<= 1;
    code <<= 1;
  }
  return -1;
}

// Decodifica um bloco comprimido (literais + pares comprimento/distancia).
inline bool inflateCodes(BitReader& br, std::vector<u8>& out, size_t maxOut, const Huffman& lc, const Huffman& dc) {
  static const u16 lbase[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
  static const u16 lext[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
  static const u16 dbase[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
  static const u16 dext[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
  for (;;) {
    int sym = decodeSymbol(br, lc);
    if (sym < 0) return false;
    if (sym < 256) { // literal
      if (out.size() >= maxOut) return false;
      out.push_back(static_cast<u8>(sym));
    } else if (sym == 256) { // fim do bloco
      return true;
    } else { // copia (comprimento, distancia)
      sym -= 257;
      if (sym >= 29) return false;
      const size_t len = lbase[sym] + br.bits(lext[sym]);
      const int ds = decodeSymbol(br, dc);
      if (ds < 0 || ds >= 30) return false;
      const size_t dist = dbase[ds] + br.bits(dext[ds]);
      if (!br.ok() || dist > out.size() || out.size() + len > maxOut) return false;
      for (size_t i = 0; i < len; ++i) out.push_back(out[out.size() - dist]); // byte a byte: a copia pode se sobrepor
    }
  }
}

} // namespace detail

// DEFLATE cru (sem cabecalho zlib). Retorna false em qualquer erro; `out` fica com o que foi produzido.
inline bool inflateRaw(const u8* in, size_t n, std::vector<u8>& out, size_t maxOut, size_t* consumed = nullptr) {
  using namespace detail;
  BitReader br(in, n);
  int last = 0;
  do {
    last = static_cast<int>(br.bits(1));
    const u32 type = br.bits(2);
    if (!br.ok()) return false;
    if (type == 0) { // bloco "stored": copia bruta com LEN/NLEN
      br.alignByte();
      if (br.remaining() < 4) return false;
      const u8* p = br.cursor();
      const u32 len = static_cast<u32>(p[0] | (p[1] << 8)), nlen = static_cast<u32>(p[2] | (p[3] << 8));
      if (len != (~nlen & 0xFFFFu)) return false;
      br.skip(4);
      if (br.remaining() < len || out.size() + len > maxOut) return false;
      out.insert(out.end(), br.cursor(), br.cursor() + len);
      br.skip(len);
    } else if (type == 1) { // Huffman fixo
      static const Huffman* fixedL = nullptr;
      static const Huffman* fixedD = nullptr;
      if (!fixedL) {
        static Huffman l, d;
        u16 lens[288];
        int s = 0;
        for (; s < 144; ++s) lens[s] = 8;
        for (; s < 256; ++s) lens[s] = 9;
        for (; s < 280; ++s) lens[s] = 7;
        for (; s < 288; ++s) lens[s] = 8;
        buildHuffman(l, lens, 288);
        for (s = 0; s < 30; ++s) lens[s] = 5;
        buildHuffman(d, lens, 30);
        fixedD = &d;
        fixedL = &l;
      }
      if (!inflateCodes(br, out, maxOut, *fixedL, *fixedD)) return false;
    } else if (type == 2) { // Huffman dinamico: as tabelas vem no proprio fluxo
      static const u8 order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
      const int nlen = static_cast<int>(br.bits(5)) + 257, ndist = static_cast<int>(br.bits(5)) + 1, ncode = static_cast<int>(br.bits(4)) + 4;
      if (!br.ok() || nlen > 286 || ndist > 30) return false;
      u16 lengths[320] = {};
      for (int i = 0; i < ncode; ++i) lengths[order[i]] = static_cast<u16>(br.bits(3));
      for (int i = ncode; i < 19; ++i) lengths[order[i]] = 0;
      Huffman lencode;
      if (buildHuffman(lencode, lengths, 19) != 0) return false;
      int idx = 0;
      while (idx < nlen + ndist) {
        int sym = decodeSymbol(br, lencode);
        if (sym < 0) return false;
        if (sym < 16) {
          lengths[idx++] = static_cast<u16>(sym);
        } else {
          u16 prev = 0;
          int rep = 0;
          if (sym == 16) {
            if (idx == 0) return false;
            prev = lengths[idx - 1];
            rep = 3 + static_cast<int>(br.bits(2));
          } else if (sym == 17) {
            rep = 3 + static_cast<int>(br.bits(3));
          } else {
            rep = 11 + static_cast<int>(br.bits(7));
          }
          if (idx + rep > nlen + ndist) return false;
          while (rep--) lengths[idx++] = prev;
        }
      }
      if (lengths[256] == 0) return false; // precisa existir o codigo de fim de bloco
      Huffman lc, dc;
      const int el = buildHuffman(lc, lengths, nlen);
      if (el < 0 || (el > 0 && nlen - lc.count[0] != 1)) return false;
      const int ed = buildHuffman(dc, lengths + nlen, ndist);
      if (ed < 0 || (ed > 0 && ndist - dc.count[0] != 1)) return false;
      if (!inflateCodes(br, out, maxOut, lc, dc)) return false;
    } else {
      return false; // tipo 3 e reservado/invalido
    }
    if (!br.ok()) return false;
  } while (!last);
  if (consumed) *consumed = br.position();
  return true;
}

inline u32 adler32(const u8* d, size_t n) {
  u32 a = 1, b = 0;
  for (size_t i = 0; i < n; ++i) {
    a = (a + d[i]) % 65521u;
    b = (b + a) % 65521u;
  }
  return (b << 16) | a;
}

// Fluxo zlib completo: cabecalho de 2 bytes + DEFLATE + Adler-32.
inline bool zlibDecompress(const u8* in, size_t n, std::vector<u8>& out, size_t maxOut, std::string* err = nullptr) {
  auto fail = [&](const char* m) { if (err) *err = m; return false; };
  if (n < 6) return fail("zlib: fluxo curto demais");
  if ((in[0] & 0x0F) != 8 || ((in[0] << 8) | in[1]) % 31 != 0) return fail("zlib: cabecalho invalido");
  if (in[1] & 0x20) return fail("zlib: dicionario predefinido nao suportado");
  out.clear();
  size_t used = 0;
  if (!inflateRaw(in + 2, n - 2, out, maxOut, &used)) return fail("zlib: dados DEFLATE invalidos ou acima do limite");
  if (2 + used + 4 > n) return fail("zlib: falta o trailer Adler-32");
  const u8* t = in + 2 + used;
  const u32 want = (static_cast<u32>(t[0]) << 24) | (static_cast<u32>(t[1]) << 16) | (static_cast<u32>(t[2]) << 8) | t[3];
  if (want != adler32(out.data(), out.size())) return fail("zlib: Adler-32 nao confere (dados corrompidos)");
  return true;
}

} // namespace tool::assets
