#pragma once
#include <cstdint>
#include <string>
#include <fstream>

namespace neoalz {

struct TrailExport {
    // Append a CSV line safely (opens/closes per write; simple and portable)
    static inline void append_csv(const std::string& path, const std::string& line){
        if (path.empty()) return;
        std::ofstream ofs(path, std::ios::app);
        if (!ofs) return;
        ofs << line << "\n";
    }
};

} // namespace neoalz

