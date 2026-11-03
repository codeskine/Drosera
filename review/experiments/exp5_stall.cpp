// Where does EchoDelay's float one-pole (lag 0.5 s at 48 kHz) stop when gliding from Init's 5 s to the knob?
#include <cstdio>
#include <initializer_list>
int main() {
    const float coef = 1.0f / (0.5f * 48000.0f);
    for (float target : {0.248f, 1.2875f, 2.5f}) {
        float cur = 5.0f; long n = 0;
        for (; n < 48000L * 60; ++n) { const float next = cur + coef * (target - cur); if (next == cur) break; cur = next; }
        std::printf("target %.4f s: stops at %.6f s after %.1f s, short by %.3f ms = %.1f samples\n", target, cur, n / 48000.0, (cur - target) * 1000.0, (cur - target) * 48000.0);
    }
}
