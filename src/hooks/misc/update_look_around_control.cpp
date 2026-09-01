#include "hooking/hooking.hpp"

#include "util/command_line.hpp"

namespace big {
    namespace {
        const auto mouse_sensitivity_multiplier = command_line::get(L"-mouseSensitivityMultiplier", 1.0f);
    }

    void hooks::update_look_around_control(void *self) {
        const auto original = g_hooking->get_original<hooks::update_look_around_control>();
        if (mouse_sensitivity_multiplier == 1.0f) {
            original(self);
            return;
        }

        if (!self) {
            original(self);
            return;
        }

        auto *base = reinterpret_cast<std::uint8_t *>(self);
        auto *metadata = *reinterpret_cast<std::uint8_t **>(base + 0x18);
        if (!metadata) {
            original(self);
            return;
        }

        auto *heading_min = reinterpret_cast<float *>(metadata + 0x9C);
        auto *pitch_min = reinterpret_cast<float *>(metadata + 0xA4);
        const auto heading_value = *heading_min;
        const auto pitch_value = *pitch_min;

        *heading_min = heading_value * mouse_sensitivity_multiplier;
        *pitch_min = pitch_value * mouse_sensitivity_multiplier;

        original(self);

        *heading_min = heading_value;
        *pitch_min = pitch_value;
    }
}
