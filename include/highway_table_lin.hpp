#pragma once
#include <cstdint>
#include <vector>
#include <string>
#include <fstream>
#include <algorithm>

namespace neoalz {

struct FeatureLin {
    static inline uint16_t pack(uint32_t mA, uint32_t mB) noexcept {
        auto wt = [](uint32_t x){ return __builtin_popcount(x); };
        uint16_t fa = (uint16_t)std::min(wt(mA), 63);
        uint16_t fb = (uint16_t)std::min(wt(mB), 63);
        uint16_t pa = (uint16_t)(mA & 1u);
        uint16_t pb = (uint16_t)(mB & 1u);
        return (fa<<8) | (fb<<2) | (pa<<1) | pb;
    }
};

struct HighwayTableLin {
    int R = 0;
    std::vector<uint16_t> data; // [r=1..R], each block 2^14
    static constexpr size_t BLOCK = 1u<<14;

    void init(int R_) { R = R_; data.assign((size_t)R * BLOCK, 0); }
    inline uint16_t& at(int r, uint16_t key){ return data[(size_t)(r-1)*BLOCK + key]; }
    inline uint16_t  get(int r, uint16_t key) const { return data[(size_t)(r-1)*BLOCK + key]; }

    bool save(const std::string& path) const {
        std::ofstream ofs(path, std::ios::binary);
        if (!ofs) return false;
        ofs.write((const char*)&R, sizeof(R));
        ofs.write((const char*)data.data(), (std::streamsize)(data.size()*sizeof(uint16_t)));
        return (bool)ofs;
    }
    bool load(const std::string& path){
        std::ifstream ifs(path, std::ios::binary);
        if (!ifs) return false;
        int Rfile=0; ifs.read((char*)&Rfile, sizeof(Rfile)); if (!ifs) return false;
        ifs.seekg(0, std::ios::end); auto len = ifs.tellg(); std::streamoff payload = len - std::streamoff(sizeof(int));
        size_t cnt = (size_t)payload / sizeof(uint16_t);
        ifs.seekg(sizeof(int), std::ios::beg);
        std::vector<uint16_t> buf(cnt);
        ifs.read((char*)buf.data(), (std::streamsize)(cnt*sizeof(uint16_t)));
        if (!ifs) return false;
        R = Rfile; data.swap(buf); return true;
    }

    inline int query(uint32_t mA, uint32_t mB, int rem) const noexcept {
        if (rem <= 0 || R==0) return 0;
        if (rem > R) rem = R;
        uint16_t k = FeatureLin::pack(mA,mB);
        return (int)get(rem, k);
    }
};

} // namespace neoalz

