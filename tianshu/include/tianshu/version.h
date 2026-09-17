// Copyright 2026 Pride Leong.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// TIANSHU version info (C ABI-safe, per ADR-0007).

#pragma once

#include <cstdint>

// Version macros (compile-time usable in static_assert / preprocessor).
// NOLINTBEGIN(modernize-macro-to-enum): must be macros for C preprocessor.
#define TIANSHU_VERSION_MAJOR 0
#define TIANSHU_VERSION_MINOR 1
#define TIANSHU_VERSION_PATCH 1
#define TIANSHU_VERSION_STRING "0.1.1"
// NOLINTEND(modernize-macro-to-enum)

// Compile-time profile (set via build system, per ADR-0005).
// Exactly one of the following is defined to 1:
//   TIANSHU_PROFILE_DESKTOP / TIANSHU_PROFILE_SERVER / TIANSHU_PROFILE_VEHICLE
//   TIANSHU_PROFILE_EMBEDDED / TIANSHU_PROFILE_MCU
// Default (if none set) is TIANSHU_PROFILE_DESKTOP.
// Uses arithmetic sum trick: defined(X) returns 0 or 1 in preprocessor.
#if defined(TIANSHU_PROFILE_DESKTOP) + defined(TIANSHU_PROFILE_SERVER) +       \
        defined(TIANSHU_PROFILE_VEHICLE) + defined(TIANSHU_PROFILE_EMBEDDED) + \
        defined(TIANSHU_PROFILE_MCU) ==                                        \
    0
#define TIANSHU_PROFILE_DESKTOP 1
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Returns the major version number.
int32_t tianshu_version_major(void);

// Returns the minor version number.
int32_t tianshu_version_minor(void);

// Returns the patch version number.
int32_t tianshu_version_patch(void);

// Returns the version string (e.g., "0.1.0"). Pointer has static lifetime.
const char* tianshu_version_string(void);

// Returns the build profile name (e.g., "desktop", "vehicle", "mcu").
const char* tianshu_build_profile(void);

#ifdef __cplusplus
}  // extern "C"
#endif
