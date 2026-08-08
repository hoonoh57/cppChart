#pragma once

#include "../core/stock_pool_engine.h"

#include <vector>

namespace trading::stock_pool::fixture
{
    // Development-only deterministic data. Production sources must never
    // silently fall back to this fixture.
    std::vector<MemberSeries> BuildDeterministicFixture();
}
