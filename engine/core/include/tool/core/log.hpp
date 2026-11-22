// ToolEngine - log simples com sink substituivel (editor, arquivo, testes).
#pragma once
#include "tool/core/types.hpp"

#include <cstdio>
#include <functional>
#include <string>
#include <string_view>

namespace tool {

// Severidade da mensagem, do menos ao mais grave.
enum class LogLevel : u8 { Trace, Info, Warn, Error };

// Texto fixo do nivel (ex.: "WARN") para impressao.
inline const char* toString(LogLevel l) {
  switch (l) {
  case LogLevel::Trace: return "TRACE";
  case LogLevel::Info: return "INFO";
  case LogLevel::Warn: return "WARN";
  case LogLevel::Error: return "ERROR";
  }
  return "?";
}

// Logger global (singleton). Sem sink definido, imprime em stdout (Trace/Info) ou stderr (Warn/Error).
class Logger {
public:
  using Sink = std::function<void(LogLevel, std::string_view)>;

  // Instancia unica do logger.
  static Logger& get() {
    static Logger inst;
    return inst;
  }
  // Define o destino das mensagens (ex.: console do editor ou arquivo). Passe vazio para voltar a imprimir na tela.
  void setSink(Sink s) { sink_ = std::move(s); }
  // Mensagens abaixo deste nivel sao descartadas. Padrao: Info.
  void setMinLevel(LogLevel l) { min_ = l; }

  // Envia uma mensagem ao sink ou ao console. Atenção: nao ha mutex; chamadas concorrentes (ex.: de jobs do parallelFor) podem intercalar linhas ou correr com setSink.
  void log(LogLevel l, std::string_view msg) {
    if (l < min_) return;
    if (sink_) {
      sink_(l, msg);
      return;
    }
    std::fprintf(l >= LogLevel::Warn ? stderr : stdout, "[%s] %.*s\n", toString(l), static_cast<int>(msg.size()),
                 msg.data());
  }

private:
  Sink sink_;
  LogLevel min_ = LogLevel::Info;
};

// Atalho para log de nivel Info.
inline void logInfo(std::string_view m) { Logger::get().log(LogLevel::Info, m); }
// Atalho para log de nivel Warn.
inline void logWarn(std::string_view m) { Logger::get().log(LogLevel::Warn, m); }
// Atalho para log de nivel Error.
inline void logError(std::string_view m) { Logger::get().log(LogLevel::Error, m); }

} // namespace tool
