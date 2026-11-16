// Standalone driver for mm/2s2h/NewDawn/TextureSynth.cpp (dev tool; the game links the same file).
//   synth_cli --lib DIR --gen masonry|direct [--rgba ORIGINAL.rgba] --w 1024 --h 1024 --seed 3 --out PREFIX [key=value ...]
// ORIGINAL.rgba = u32 w, u32 h, RGBA8 (a decoded original texture, only used for the palette; game-derived: keep local).
// Writes PREFIX.albedo.rgba (w*h*4), PREFIX.n (mw*mh*2), PREFIX.r (mw*mh*2) as raw bytes.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>

#include "TextureSynth.h"

int main(int argc, char** argv) {
    std::string lib, gen, rgba, out;
    int w = 1024, h = 1024;
    unsigned seed = 1;
    NewDawnSynth::Params p;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() { return std::string(argv[++i]); };
        if (a == "--lib") lib = next();
        else if (a == "--gen") gen = next();
        else if (a == "--rgba") rgba = next();
        else if (a == "--out") out = next();
        else if (a == "--w") w = atoi(next().c_str());
        else if (a == "--h") h = atoi(next().c_str());
        else if (a == "--seed") seed = (unsigned)atoi(next().c_str());
        else if (auto eq = a.find('='); eq != std::string::npos) {
            std::string k = a.substr(0, eq), v = a.substr(eq + 1);
            char* end = nullptr;
            double d = strtod(v.c_str(), &end);
            if (end && *end == 0 && !v.empty()) p.num[k] = d; else p.str[k] = v;
        }
    }
    NewDawnSynth::Palette pal = NewDawnSynth::Palette::Gray();
    if (!rgba.empty()) {
        std::ifstream in(rgba, std::ios::binary);
        uint32_t dims[2];
        in.read((char*)dims, 8);
        std::vector<uint8_t> px((size_t)dims[0] * dims[1] * 4);
        in.read((char*)px.data(), px.size());
        pal = NewDawnSynth::Palette::FromRgba(px.data(), (size_t)dims[0] * dims[1], (float)p.Get("sat", 1.12));
    }
    NewDawnSynth::Library L(lib);
    NewDawnSynth::Mat m;
    auto t0 = std::chrono::steady_clock::now();
    if (!NewDawnSynth::Generate(gen, L, pal, p, w, h, seed, p.Str("region", ""), m)) { fprintf(stderr, "generate failed\n"); return 1; }
    auto e = NewDawnSynth::Encode(m);
    double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    fprintf(stderr, "%dx%d %s %.2fs (incl. library load)\n", w, h, gen.c_str(), s);
    for (auto& miss : L.Missing()) fprintf(stderr, "missing material %s\n", miss.c_str());
    std::ofstream(out + ".albedo.rgba", std::ios::binary).write((const char*)e.rgba.data(), e.rgba.size());
    std::ofstream(out + ".n", std::ios::binary).write((const char*)e.normal.data(), e.normal.size());
    std::ofstream(out + ".r", std::ios::binary).write((const char*)e.rough.data(), e.rough.size());
    return 0;
}
