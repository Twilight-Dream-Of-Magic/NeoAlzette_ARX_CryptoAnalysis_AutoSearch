#pragma once
#include <cstdint>
#include <vector>
#include <string>
#include <fstream>
#include <limits>
#include <algorithm>

namespace neoalz {

// Very compact "feature" compressing (dA,dB) for highway LB lookup.
// You can refine this later (e.g., include low-bit runs, leading carry window).
struct Feature {
    // pack: [ wt(dA) : 6 ][ wt(dB) : 6 ][ parity(dA) :1 ][ parity(dB):1 ] -> 14 bits
    static inline uint16_t pack(uint32_t dA, uint32_t dB) noexcept {
        auto wt = [](uint32_t x){ return __builtin_popcount(x); };
        uint16_t fa = (uint16_t)std::min(wt(dA), 63);
        uint16_t fb = (uint16_t)std::min(wt(dB), 63);
        uint16_t pa = (uint16_t)(dA & 1u);
        uint16_t pb = (uint16_t)(dB & 1u);
        return (fa<<8) | (fb<<2) | (pa<<1) | pb;
    }
};

// HighwayTable: LB suffix table indexed by feature and round index r.
// We store for r=1..R a vector of size 2^14 (~16k) per r.
struct HighwayTable {
    int R = 0;
    std::vector<uint16_t> data; // concatenated: [r=1][r=2]...[r=R], each block 16384 entries

    static constexpr size_t BLOCK = 1u<<14;

    void init(int R_) {
        R = R_;
        data.assign((size_t)R * BLOCK, 0);
    }

    inline uint16_t& at(int r, uint16_t key) {
        return data[(size_t)(r-1) * BLOCK + key];
    }
    inline uint16_t  get(int r, uint16_t key) const {
        return data[(size_t)(r-1) * BLOCK + key];
    }

    // binary IO
    bool save(const std::string& path) const {
        std::ofstream ofs(path, std::ios::binary);
        if (!ofs) return false;
        ofs.write((const char*)&R, sizeof(R));
        ofs.write((const char*)data.data(), (std::streamsize)(data.size()*sizeof(uint16_t)));
        return (bool)ofs;
    }
    bool load(const std::string& path) {
        std::ifstream ifs(path, std::ios::binary);
        if (!ifs) return false;
        int Rfile=0;
        ifs.read((char*)&Rfile, sizeof(Rfile));
        if (!ifs) return false;
        std::vector<uint16_t> buf;
        ifs.seekg(0, std::ios::end);
        auto len = ifs.tellg();
        std::streamoff payload = len - std::streamoff(sizeof(int));
        size_t cnt = (size_t)payload / sizeof(uint16_t);
        ifs.seekg(sizeof(int), std::ios::beg);
        buf.resize(cnt);
        ifs.read((char*)buf.data(), (std::streamsize)(cnt*sizeof(uint16_t)));
        if (!ifs) return false;
        R = Rfile;
        data.swap(buf);
        return true;
    }

    // Query suffix LB: remaining rounds = rem; key from (dA,dB).
    // Safe: if table not filled, returns 0 (still a valid lower bound).
    inline int query(uint32_t dA, uint32_t dB, int rem) const noexcept {
        if (rem <= 0 || R == 0) return 0;
        if (rem > R) rem = R;
        uint16_t k = Feature::pack(dA,dB);
        return (int)get(rem, k);
    }
};

} // namespace neoalz
