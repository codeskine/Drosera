#pragma once
#ifndef INFS_CONTROLS_H
#define INFS_CONTROLS_H

#include <array>
#include <functional>
#include <optional>
#include <daisysp.h>
#include "SmoothedValue.h"
#include "DSPUtils.h"

namespace infrasonic {

/**
 * @brief
 * Control parameter registry with built in update smoothing. Fixed storage: no allocation after Register.
 *
 * Process applies the handlers from the highest id down to the lowest. That is the order the firmware's
 * unordered_map happened to give, and the reference renders depend on it: a handler may read what another
 * wrote in the same tick (see DroseraCore::applyPitchAndBody).
 *
 * @tparam ParamId Enum whose values are 0 .. NumParams - 1
 */
template<typename ParamId, size_t NumParams>
class ParameterRegistry {

    public:

        using Handler = std::function<void(const float)>;

        void Init(const float control_rate)
        {
            control_rate_ = control_rate;
        }

        // Registering an id again leaves the first registration, smoothing state included.
        void Register(const ParamId id, const float initial_value, const float min, const float max,
                      Handler handler, const float smooth_time = 0.05f,
                      const daisysp::Mapping mapping = daisysp::Mapping::LINEAR)
        {
            auto &state = param_states_[(size_t) id];
            if (state) return;
            state.emplace(initial_value, min, max, onepole_coef_t60(smooth_time, control_rate_), mapping, std::move(handler));
        }

        // Called in response to new control input. Updates target, does not apply value.
        void Update(const ParamId id, const float value, const bool immediate = false)
        {
            if (auto &state = param_states_[(size_t) id]) {
                state->value.set(daisysp::fclamp(value, state->min, state->max), immediate);
            }
        }

        void UpdateNormalized(const ParamId id, const float normValue, const bool immediate = false)
        {
            if (auto &state = param_states_[(size_t) id]) {
                state->value.set(daisysp::fmap(daisysp::fclamp(normValue, 0.0f, 1.0f), state->min, state->max, state->mapping), immediate);
            }
        }

        // Updates param values with smoothing and applies using handler.
        void Process()
        {
            for (size_t i = NumParams; i-- > 0;) {
                if (auto &state = param_states_[i]) state->handler(state->value.get());
            }
        }

    private:

        struct ParamState {
            SmoothedValue value;

            const float min;
            const float max;
            const daisysp::Mapping mapping;
            const Handler handler;

            ParamState(const float initial, const float min_, const float max_,
                       const float coef, const daisysp::Mapping mapping_, Handler handler_)
                : value(initial, coef)
                , min(min_)
                , max(max_)
                , mapping(mapping_)
                , handler(std::move(handler_))
            {
            }
        };

        float control_rate_ = 0.0f;
        std::array<std::optional<ParamState>, NumParams> param_states_;
};

}

#endif
