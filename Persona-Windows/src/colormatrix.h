#pragma once
// 5x5 colour matrices for the Windows Magnification API (MAGCOLOREFFECT),
// replacing the Hyprland screen shaders. Row-vector convention:
//   [r g b a 1] * M = [r' g' b' a' 1]
// Pure C++ so it can be tested without Windows.

#include <array>

enum class ScreenFilterKind { Bluelight = 0, Greyscale = 1, Invert = 2 };

using Matrix5 = std::array<float, 25>; // row-major, m[row*5 + col]

inline Matrix5 identityMatrix5()
{
    Matrix5 m{};
    for (int i = 0; i < 5; ++i)
        m[i * 5 + i] = 1.0f;
    return m;
}

inline Matrix5 makeFilterMatrix(int kind)
{
    Matrix5 m = identityMatrix5();
    switch (kind) {
    case 0: // bluelight.frag: pixColor[2] *= 0.8
        m[2 * 5 + 2] = 0.8f;
        break;
    case 1: { // grey.glsl: luma -> mix(ink, paper, luma)  (grain/dither can't be done with a matrix)
        const float w[3] = {0.299f, 0.587f, 0.114f};
        const float paper[3] = {0.94f, 0.92f, 0.86f};
        const float ink[3] = {0.10f, 0.10f, 0.12f};
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
                m[i * 5 + j] = w[i] * (paper[j] - ink[j]);
        for (int j = 0; j < 3; ++j)
            m[4 * 5 + j] = ink[j];
        break;
    }
    case 2: // invert.glsl: 1 - rgb
        for (int i = 0; i < 3; ++i)
            m[i * 5 + i] = -1.0f;
        for (int j = 0; j < 3; ++j)
            m[4 * 5 + j] = 1.0f;
        break;
    default:
        break;
    }
    return m;
}

// helper used by tests
inline void applyMatrix5(const Matrix5 &m, float r, float g, float b, float out[3])
{
    const float in[5] = {r, g, b, 1.0f, 1.0f};
    for (int j = 0; j < 3; ++j) {
        float v = 0.0f;
        for (int i = 0; i < 5; ++i)
            v += in[i] * m[i * 5 + j];
        out[j] = v;
    }
}
