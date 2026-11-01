// Renders presets of the host-independent core to 32-bit float stereo WAV files (48 kHz).
// usage: render_cli <presets.txt> <out_dir> [seconds]
// preset line: name freq fbgain body lpf hpf revmix revdecay echosend echotime echofb volume halve
// (11 normalized knob values 0..1 plus halve 0/1; '#' starts a comment)
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include "DroseraCore.h"

using infrasonic::DroseraCore;

static void writeWavF32(const std::string& path, const std::vector<float>& L, const std::vector<float>& R) {
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) { std::perror(path.c_str()); return; }
    uint32_t n = (uint32_t)L.size(), dataBytes = n * 8, rate = 48000, byteRate = rate * 8, riff = 36 + dataBytes, sixteen = 16;
    uint16_t fmt = 3, ch = 2, bits = 32, align = 8;
    fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f); fwrite(&sixteen, 4, 1, f);
    fwrite(&fmt, 2, 1, f); fwrite(&ch, 2, 1, f); fwrite(&rate, 4, 1, f); fwrite(&byteRate, 4, 1, f);
    fwrite(&align, 2, 1, f); fwrite(&bits, 2, 1, f); fwrite("data", 1, 4, f); fwrite(&dataBytes, 4, 1, f);
    for (uint32_t i = 0; i < n; ++i) { fwrite(&L[i], 4, 1, f); fwrite(&R[i], 4, 1, f); }
    fclose(f);
}

int main(int argc, char** argv) {
    if (argc < 3) { std::printf("usage: render_cli <presets.txt> <out_dir> [seconds]\n"); return 2; }
    const double seconds = argc > 3 ? std::atof(argv[3]) : 20.0;
    std::ifstream in(argv[1]);
    std::string line;
    while (std::getline(in, line)) {
        if (auto h = line.find('#'); h != std::string::npos) line.resize(h);
        std::istringstream ss(line);
        std::string name; float p[DroseraCore::NumParams]; int halve = 0;
        if (!(ss >> name)) continue;
        bool good = true;
        for (float& v : p) good &= (bool)(ss >> v);
        good &= (bool)(ss >> halve);
        if (!good) { std::printf("bad preset line for %s\n", name.c_str()); return 1; }

        DroseraCore core; core.Init();
        for (int i = 0; i < DroseraCore::NumParams; ++i) core.SetParam((DroseraCore::Param)i, p[i]);
        core.SetEchoWarp(halve != 0);
        const size_t total = (size_t)(seconds * 48000);
        std::vector<float> L(total), R(total);
        for (size_t pos = 0; pos < total; pos += 4)   // firmware block size
            core.Render(nullptr, &L[pos], &R[pos], std::min<size_t>(4, total - pos));
        double rms = 0, peak = 0;
        for (size_t i = total / 4; i < total; ++i) { rms += (double)L[i] * L[i] + (double)R[i] * R[i]; peak = std::max({peak, (double)std::fabs(L[i]), (double)std::fabs(R[i])}); }
        rms = std::sqrt(rms / (2.0 * (total - total / 4)));
        writeWavF32(std::string(argv[2]) + "/" + name + ".wav", L, R);
        std::printf("%-18s rms=%.4f (%.1f dBFS) peak=%.3f\n", name.c_str(), rms, 20 * std::log10(rms + 1e-12), peak);
    }
    return 0;
}
