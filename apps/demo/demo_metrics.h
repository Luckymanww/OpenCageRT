#pragma once

#include "opencagert/demo_state.h"

#include <string>

opencagert::DemoMetrics compute_placeholder_metrics(opencagert::TriLadder level, float frame_ms);

std::wstring format_demo_title(const opencagert::DemoState& state, const opencagert::DemoMetrics& metrics,
                               const std::wstring& base_title);
