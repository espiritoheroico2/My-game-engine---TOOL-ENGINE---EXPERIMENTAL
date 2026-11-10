#pragma once
// Slot map com geração (uso interno). Inserção/remoção O(1), iteração densa sobre slots vivos.
#include <cstdint>
#include <vector>

namespace LightPrism {

template <class T>
class SlotMap {
public:
    struct Id { uint32_t index; uint32_t generation; };

    Id Insert(const T& value) {
        uint32_t i;
        if (!m_free.empty()) { i = m_free.back(); m_free.pop_back(); }
        else { i = static_cast<uint32_t>(m_slots.size()); m_slots.emplace_back(); }
        Slot& s = m_slots[i];
        s.value = value;
        s.alive = true;
        ++m_alive;
        return {i, s.generation};
    }

    bool Remove(Id id) {
        if (!Valid(id)) return false;
        Slot& s = m_slots[id.index];
        s.alive = false;
        if (++s.generation == 0) s.generation = 1; // 0 reservado a "inválido"
        m_free.push_back(id.index);
        --m_alive;
        return true;
    }

    [[nodiscard]] bool Valid(Id id) const noexcept {
        return id.generation != 0 && id.index < m_slots.size() &&
               m_slots[id.index].alive && m_slots[id.index].generation == id.generation;
    }

    [[nodiscard]] T* Get(Id id) noexcept { return Valid(id) ? &m_slots[id.index].value : nullptr; }
    [[nodiscard]] const T* Get(Id id) const noexcept { return Valid(id) ? &m_slots[id.index].value : nullptr; }

    template <class F>
    void ForEach(F&& fn) const {
        for (const Slot& s : m_slots) if (s.alive) fn(s.value);
    }

    [[nodiscard]] size_t Count() const noexcept { return m_alive; }

private:
    struct Slot { T value{}; uint32_t generation = 1; bool alive = false; };
    std::vector<Slot> m_slots;
    std::vector<uint32_t> m_free;
    size_t m_alive = 0;
};

} // namespace LightPrism
