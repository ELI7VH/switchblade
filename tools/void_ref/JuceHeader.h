/* SPDX-License-Identifier: GPL-3.0-only */
/* The few JUCE names the VOID WAVE plugin's DSP files use (tools/void_ref.cpp builds them without JUCE). */
#pragma once
#include <algorithm>
#include <cmath>
#include <vector>
namespace juce {
template <typename T> T jlimit(T lo, T hi, T v) { return v < lo ? lo : v > hi ? hi : v; }
template <typename T> T jmin(T a, T b) { return a < b ? a : b; }
template <typename T> T jmax(T a, T b) { return a > b ? a : b; }
template <typename T> struct MathConstants {
    static constexpr T pi = (T)3.141592653589793238;
    static constexpr T twoPi = (T)6.283185307179586477;
    static constexpr T halfPi = (T)1.570796326794896619;
};
template <typename... T> void ignoreUnused(T &&...) {}
}
#define JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(x)
