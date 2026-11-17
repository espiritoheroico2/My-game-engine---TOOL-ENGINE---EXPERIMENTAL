// Politica de remocao do cache de malhas de chunks (F-08): funcao pura, sem SDL nem GPU.
#include "chunk_cache.hpp"

#include <cstdio>
#include <set>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

using editor::chunkcache::evictionCount;
using editor::chunkcache::leastRecent;

int run = 0;
int failed = 0;

void Check(bool ok, const char* expr) {
    ++run;
    if (!ok) {
        ++failed;
        std::fprintf(stderr, "[FAIL] %s\n", expr);
    }
}
#define CHECK(expr) Check((expr), #expr)

void TestEvictionCount() {
    CHECK(evictionCount(0, 8192) == 0);
    CHECK(evictionCount(8192, 8192) == 0);          // no teto, nada a remover
    CHECK(evictionCount(8193, 8192) == 1025);       // desce ate 8192 - 1024 = 7168
    CHECK(evictionCount(9, 8) == 2);                // max 8 -> alvo 7
    CHECK(evictionCount(100, 0) == 100);            // teto zero: nada retido
    CHECK(evictionCount(5, 1) == 5 - (1 - 0));      // max/8 = 0 -> alvo = max
}

void TestLeastRecent() {
    std::vector<std::pair<int, uint64_t>> e = {{1, 5}, {2, 1}, {3, 9}, {4, 0}};
    auto two = leastRecent(e, 2);
    CHECK(two.size() == 2);
    std::set<int> got(two.begin(), two.end());
    CHECK(got == (std::set<int>{2, 4}));            // as de menor lastUsed
    CHECK(leastRecent(e, 0).empty());
    CHECK(leastRecent(e, 99).size() == 4);          // count acima do tamanho: todas
    CHECK(leastRecent(std::vector<std::pair<int, uint64_t>>{}, 3).empty());
}

// Simulacao do uso real: cache com teto pequeno, acessos em ordem, uma chave quente.
void TestPolicyKeepsRecentAndBounded() {
    const size_t max = 8;
    std::unordered_map<int, uint64_t> cache;     // chave -> lastUsed
    uint64_t tick = 0;
    size_t worst = 0;
    for (int step = 0; step < 200; ++step) {
        const int key = (step % 3 == 0) ? 1000 : step;   // 1000 acessada a cada 3 passos
        cache[key] = ++tick;                             // insere e marca o uso, depois remove (mesma ordem do editor)
        const size_t n = evictionCount(cache.size(), max);
        if (n) {
            std::vector<std::pair<int, uint64_t>> entries(cache.begin(), cache.end());
            for (int k : leastRecent(std::move(entries), n)) cache.erase(k);
        }
        worst = cache.size() > worst ? cache.size() : worst;
    }
    CHECK(worst <= max);                 // nunca passa do teto
    CHECK(cache.count(1000) == 1);       // a chave acessada a cada 3 passos sobrevive a todas as remocoes
    CHECK(cache.count(199) == 1);        // o acesso mais recente tambem
    CHECK(cache.size() <= max);
}

} // namespace

int main() {
    TestEvictionCount();
    TestLeastRecent();
    TestPolicyKeepsRecentAndBounded();
    std::printf("%d checagens, %d falhas\n", run, failed);
    return failed == 0 ? 0 : 1;
}
