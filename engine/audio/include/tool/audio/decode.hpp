// ToolEngine - importacao de audio: WAV, MP3 (e FLAC/OGG nao, so o que o miniaudio embutido decodifica).
//   auto clip = tool::decodeAudioFile("Project/Assets/passos.mp3", &erro);
// O resultado e um SoundData (PCM float intercalado, 1 ou 2 canais) pronto para Mixer::play().
// Implementado em src/miniaudio_device.cpp (miniaudio e um arquivo unico, entao fica isolado ali).
// Seguranca: arquivo nao confiavel -> limite de tamanho e de duracao (10 min); nunca lanca excecao.
#pragma once
#include "tool/audio/audio.hpp"

#include <string>

namespace tool {

// Extensoes aceitas pelo editor.
inline bool isAudioExtension(const std::string& ext) {
  std::string e = ext;
  for (auto& c : e) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return e == ".wav" || e == ".mp3";
}

// Decodifica um arquivo de audio. Retorna nullptr e preenche *err se falhar.
SoundPtr decodeAudioFile(const std::string& path, std::string* err = nullptr);

} // namespace tool
