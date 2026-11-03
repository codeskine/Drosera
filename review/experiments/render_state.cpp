// Renders the state the Standalone restored (decoded from Drosera.settings) with the engine, 25 s at 48 kHz.
#include <cstdio>
#include <vector>
#include "DroseraCore.h"
using infrasonic::DroseraCore;
int main(int argc, char** argv) {
    DroseraCore c; c.Init();
    const float knobs[11] = {0.765f, 0.929f, 0.745f, 0.799f, 0.458f, 0.300f, 0.500f, 0.882f, 0.203f, 0.818f, 0.700f};
    for (int i = 0; i < 11; ++i) c.SetParam((DroseraCore::Param) i, knobs[i]);
    c.SetEchoWarp(false); c.SetPolyphony(true); c.SetCoupling(0.597f); c.SetIntervals(0, -7); c.SetStringLock(true);
    c.SetEchoPong(true); c.SetEchoDrive(1.0f); c.SetEchoTone(0.640f); c.SetEchoWidth(0.865f); c.SetEchoLevel(0.833f);
    const size_t n = 48000 * 25; std::vector<float> L(n), R(n);
    for (size_t p = 0; p < n; p += 512) c.Render(nullptr, &L[p], &R[p], 512);
    FILE* f = fopen(argv[1], "wb"); char hdr[44] = {}; fwrite(hdr, 1, 44, f);
    for (size_t i = 0; i < n; ++i) { fwrite(&L[i], 4, 1, f); fwrite(&R[i], 4, 1, f); }
    fclose(f); std::printf("rendered %zu samples, recoveries %d\n", n, c.RecoveryCount());
}
