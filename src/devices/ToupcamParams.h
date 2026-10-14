#pragma once

// Pure encoding/unit helpers for the Toupcam parameter surface. Kept free of
// SDK and Qt dependencies so the hardware conventions stay unit-testable:
// temperatures travel in 0.1 °C, gain in percent, exposure in microseconds.

namespace CamParams {

template <typename T>
T clamped(T value, T minimum, T maximum)
{
    return value < minimum ? minimum : value > maximum ? maximum : value;
}

// TOUPCAM_OPTION_TECTARGET_RANGE packs min into the low 16 bits and max into
// the high 16 bits, each a signed short in 0.1 °C.
inline int tecTargetRangeMin(unsigned packed)
{
    return static_cast<short>(packed & 0xFFFF);
}

inline int tecTargetRangeMax(unsigned packed)
{
    return static_cast<short>((packed >> 16) & 0xFFFF);
}

// TOUPCAM_OPTION_BINNING: 0x01 disables binning; the plain value n (0x02..0x08)
// selects saturating-add n*n (the astronomy default). Factor is capped at 8x8.
inline int binningOptionValue(int factor)
{
    if (factor <= 1) return 1;
    return factor > 8 ? 8 : factor;
}

// Recovers the n of an n*n binning from any known encoding (add, average
// 0x80|n, or RAW add 0x40|n); unknown values read as no binning.
inline int binningFactor(int optionValue)
{
    if (optionValue >= 0x02 && optionValue <= 0x08) return optionValue;
    if (optionValue >= 0x42 && optionValue <= 0x48) return optionValue & 0x3F;
    if (optionValue >= 0x82 && optionValue <= 0x88) return optionValue & 0x7F;
    return 1;
}

// TOUPCAM_OPTION_BLACKLEVEL spans 0..31 scaled by the bit depth above 8 bits.
inline int blackLevelMaxForBitDepth(int bitDepth)
{
    if (bitDepth < 8) bitDepth = 8;
    if (bitDepth > 16) bitDepth = 16;
    return 31 << (bitDepth - 8);
}

// TEC voltage and its maximum both read in 0.1 V; the duty ratio is reported
// as a percentage and never leaves [0, 100].
inline double tecPowerPercent(int voltage, int voltageMax)
{
    if (voltageMax <= 0) return 0.0;
    const double percent = voltage * 100.0 / voltageMax;
    return percent < 0.0 ? 0.0 : percent > 100.0 ? 100.0 : percent;
}

inline int temperatureToTenths(double celsius)
{
    return static_cast<int>(celsius < 0.0 ? celsius * 10.0 - 0.5 : celsius * 10.0 + 0.5);
}

inline double tenthsToCelsius(int tenths)
{
    return tenths / 10.0;
}

} // namespace CamParams
