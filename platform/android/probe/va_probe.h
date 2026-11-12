// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <cstdarg>
#include <cstdint>
#include <string>

namespace ps4droid {

/// Runs experiment E1 and returns a plain-text report. Safe to call in any process:
/// reservations are PROT_NONE, never replace existing mappings, and are released.
std::string RunVaProbe();

} // namespace ps4droid
