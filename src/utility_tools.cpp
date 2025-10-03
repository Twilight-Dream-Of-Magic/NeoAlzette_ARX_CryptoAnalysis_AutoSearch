#include "utility_tools.hpp"
#include <iomanip>
#include <cmath>
#include <random>
#include <thread>

namespace neoalz {

// Static member initialization
std::mutex UtilityTools::TrailExporter::file_mutex_;

// ============================================================================
// Canonicalizer implementation
// ============================================================================

std::pair<std::uint32_t, std::uint32_t> 
UtilityTools::Canonicalizer::canonical_rotate_pair(std::uint32_t a, std::uint32_t b) {
    std::uint32_t best_a = a, best_b = b;
    
    for (int r = 0; r < 32; ++r) {
        std::uint32_t rot_a = NeoAlzetteCore::rotl(a, r);
        std::uint32_t rot_b = NeoAlzetteCore::rotl(b, r);
        
        if (std::make_pair(rot_a, rot_b) < std::make_pair(best_a, best_b)) {
            best_a = rot_a;
            best_b = rot_b;
        }
    }
    
    return {best_a, best_b};
}

std::pair<std::uint32_t, std::uint32_t> 
UtilityTools::Canonicalizer::canonical_rotate_pair_fast(std::uint32_t a, std::uint32_t b) {
    if (a == 0 || b == 0) {
        if (a > b) std::swap(a, b);
        return {a, b};
    }
    
    // Use leading zero count to limit search space
    int lz_a = __builtin_clz(a);
    int lz_b = __builtin_clz(b);
    int max_useful_rot = std::min(lz_a, lz_b);
    
    std::uint32_t best_a = a, best_b = b;
    int check_rotations = std::min(32, max_useful_rot + 8);
    
    for (int r = 1; r < check_rotations; ++r) {
        std::uint32_t rot_a = NeoAlzetteCore::rotl(a, r);
        std::uint32_t rot_b = NeoAlzetteCore::rotl(b, r);
        
        if (std::make_pair(rot_a, rot_b) < std::make_pair(best_a, best_b)) {
            best_a = rot_a;
            best_b = rot_b;
        }
    }
    
    return {best_a, best_b};
}

std::pair<std::uint32_t, std::uint32_t> 
UtilityTools::Canonicalizer::canonical_rotate_pair_optimized(std::uint32_t a, std::uint32_t b) {
    // Heuristic: check only power-of-2 rotations and a few others
    std::uint32_t best_a = a, best_b = b;
    
    const std::vector<int> check_rotations = {0, 1, 2, 4, 8, 16, 24, 31};
    
    for (int r : check_rotations) {
        std::uint32_t rot_a = NeoAlzetteCore::rotl(a, r);
        std::uint32_t rot_b = NeoAlzetteCore::rotl(b, r);
        
        if (std::make_pair(rot_a, rot_b) < std::make_pair(best_a, best_b)) {
            best_a = rot_a;
            best_b = rot_b;
        }
    }
    
    return {best_a, best_b};
}

std::vector<std::pair<std::uint32_t, std::uint32_t>>
UtilityTools::Canonicalizer::canonicalize_states(
    const std::vector<std::pair<std::uint32_t, std::uint32_t>>& states) {
    
    std::vector<std::pair<std::uint32_t, std::uint32_t>> result;
    result.reserve(states.size());
    
    for (const auto& state : states) {
        result.push_back(canonical_rotate_pair(state.first, state.second));
    }
    
    return result;
}

// ============================================================================
// TrailExporter implementation
// ============================================================================

bool UtilityTools::TrailExporter::export_trail_csv(const std::string& filename, 
                                                   const std::vector<TrailEntry>& trail) {
    std::lock_guard<std::mutex> lock(file_mutex_);
    
    std::ofstream file(filename, std::ios::trunc);
    if (!file.is_open()) return false;
    
    // Write CSV header
    file << "Round,StateA,StateB,Weight,Algorithm,Timestamp\n";
    
    // Write trail entries
    for (const auto& entry : trail) {
        file << entry.round << ","
             << hex_format(entry.state_a) << ","
             << hex_format(entry.state_b) << ","
             << entry.weight << ","
             << entry.algorithm << ","
             << std::chrono::duration_cast<std::chrono::seconds>(
                    entry.timestamp.time_since_epoch()).count() << "\n";
    }
    
    return true;
}

bool UtilityTools::TrailExporter::append_csv_line(const std::string& filename, 
                                                  const std::string& line) {
    std::lock_guard<std::mutex> lock(file_mutex_);
    
    std::ofstream file(filename, std::ios::app);
    if (!file.is_open()) return false;
    
    file << line << "\n";
    return true;
}

bool UtilityTools::TrailExporter::ensure_csv_header(const std::string& filename, 
                                                    const std::string& header) {
    std::ifstream test(filename);
    if (!test.good()) {
        std::lock_guard<std::mutex> lock(file_mutex_);
        std::ofstream file(filename, std::ios::trunc);
        if (file.is_open()) {
            file << header << "\n";
            return true;
        }
        return false;
    }
    return true;
}

bool UtilityTools::TrailExporter::export_histogram_csv(const std::string& filename, 
                                                       const std::vector<std::pair<int, int>>& histogram) {
    std::lock_guard<std::mutex> lock(file_mutex_);
    
    std::ofstream file(filename, std::ios::trunc);
    if (!file.is_open()) return false;
    
    file << "Weight,Count\n";
    for (const auto& [weight, count] : histogram) {
        file << weight << "," << count << "\n";
    }
    
    return true;
}

std::string UtilityTools::TrailExporter::hex_format(std::uint32_t value, bool prefix) {
    std::ostringstream ss;
    if (prefix) ss << "0x";
    ss << std::hex << std::uppercase << value;
    return ss.str();
}

// ============================================================================
// BasicBounds implementation
// ============================================================================

UtilityTools::BasicBounds::BoundResult 
UtilityTools::BasicBounds::hamming_weight_bound(std::uint32_t dA, std::uint32_t dB) {
    int hw_total = __builtin_popcount(dA | dB);
    int lower = std::max(1, hw_total / 4);
    int upper = hw_total * 2;
    
    return {lower, upper, "hamming_weight"};
}

UtilityTools::BasicBounds::BoundResult 
UtilityTools::BasicBounds::conservative_diff_bound(std::uint32_t dA, std::uint32_t dB, int rounds) {
    auto single_round = hamming_weight_bound(dA, dB);
    
    // Conservative estimate: each round adds at least 1 weight
    int lower = single_round.lower_bound + (rounds - 1);
    int upper = single_round.upper_bound + rounds * 3;
    
    return {lower, upper, "conservative_differential"};
}

UtilityTools::BasicBounds::BoundResult 
UtilityTools::BasicBounds::conservative_linear_bound(std::uint32_t mA, std::uint32_t mB, int rounds) {
    auto single_round = hamming_weight_bound(mA, mB);
    
    // Linear bounds are typically tighter than differential
    int lower = single_round.lower_bound + (rounds - 1) / 2;
    int upper = single_round.upper_bound + rounds * 2;
    
    return {lower, upper, "conservative_linear"};
}

UtilityTools::BasicBounds::BoundResult 
UtilityTools::BasicBounds::combined_bound(std::uint32_t stateA, std::uint32_t stateB, 
                                         int rounds, bool is_differential) {
    if (is_differential) {
        return conservative_diff_bound(stateA, stateB, rounds);
    } else {
        return conservative_linear_bound(stateA, stateB, rounds);
    }
}

// ============================================================================
// SimplePDDT implementation
// ============================================================================

void UtilityTools::SimplePDDT::build(int n_bits, int weight_threshold) {
    entries_.clear();
    
    const std::uint32_t max_val = std::min(static_cast<std::uint32_t>(1ULL << n_bits), static_cast<std::uint32_t>(1ULL << 16)); // Limit for performance
    
    for (std::uint32_t input = 0; input < max_val; ++input) {
        for (std::uint32_t output = 0; output < max_val; ++output) {
            double prob = compute_differential_probability(input, output);
            
            if (prob > 0.0) {
                int weight = -static_cast<int>(std::log2(prob));
                
                if (weight <= weight_threshold) {
                    entries_.push_back({input, output, weight, prob});
                }
            }
        }
    }
    
    build_index();
}

std::vector<UtilityTools::SimplePDDT::Entry> 
UtilityTools::SimplePDDT::query(std::uint32_t input_diff, int max_weight) const {
    std::vector<Entry> result;
    
    auto it = index_.find(input_diff);
    if (it != index_.end()) {
        for (std::size_t idx : it->second) {
            if (entries_[idx].weight <= max_weight) {
                result.push_back(entries_[idx]);
            }
        }
    }
    
    return result;
}

UtilityTools::SimplePDDT::Stats UtilityTools::SimplePDDT::get_stats() const {
    if (entries_.empty()) {
        return {0, 0, 0, 0.0};
    }
    
    int min_w = std::numeric_limits<int>::max();
    int max_w = 0;
    double sum_w = 0.0;
    
    for (const auto& entry : entries_) {
        min_w = std::min(min_w, entry.weight);
        max_w = std::max(max_w, entry.weight);
        sum_w += entry.weight;
    }
    
    return {entries_.size(), max_w, min_w, sum_w / entries_.size()};
}

bool UtilityTools::SimplePDDT::save(const std::string& filename) const {
    std::ofstream file(filename, std::ios::binary);
    if (!file.is_open()) return false;
    
    std::uint32_t count = entries_.size();
    file.write(reinterpret_cast<const char*>(&count), sizeof(count));
    
    for (const auto& entry : entries_) {
        file.write(reinterpret_cast<const char*>(&entry.input_diff), sizeof(entry.input_diff));
        file.write(reinterpret_cast<const char*>(&entry.output_diff), sizeof(entry.output_diff));
        file.write(reinterpret_cast<const char*>(&entry.weight), sizeof(entry.weight));
        file.write(reinterpret_cast<const char*>(&entry.probability), sizeof(entry.probability));
    }
    
    return true;
}

bool UtilityTools::SimplePDDT::load(const std::string& filename) {
    std::ifstream file(filename, std::ios::binary);
    if (!file.is_open()) return false;
    
    std::uint32_t count;
    file.read(reinterpret_cast<char*>(&count), sizeof(count));
    
    entries_.clear();
    entries_.reserve(count);
    
    for (std::uint32_t i = 0; i < count; ++i) {
        Entry entry;
        file.read(reinterpret_cast<char*>(&entry.input_diff), sizeof(entry.input_diff));
        file.read(reinterpret_cast<char*>(&entry.output_diff), sizeof(entry.output_diff));
        file.read(reinterpret_cast<char*>(&entry.weight), sizeof(entry.weight));
        file.read(reinterpret_cast<char*>(&entry.probability), sizeof(entry.probability));
        entries_.push_back(entry);
    }
    
    build_index();
    return true;
}

void UtilityTools::SimplePDDT::build_index() {
    index_.clear();
    
    for (std::size_t i = 0; i < entries_.size(); ++i) {
        index_[entries_[i].input_diff].push_back(i);
    }
}

double UtilityTools::SimplePDDT::compute_differential_probability(std::uint32_t input, 
                                                                 std::uint32_t output) const {
    // Simplified differential probability model
    if (input == 0) {
        return (output == 0) ? 1.0 : 0.0;
    }
    
    int hw_in = __builtin_popcount(input);
    int hw_out = __builtin_popcount(output);
    
    // Simple heuristic model
    if (hw_out > hw_in + 3) return 0.0;
    
    return std::pow(0.5, hw_in + hw_out / 2);
}

// ============================================================================
// ConfigValidator implementation
// ============================================================================

UtilityTools::ConfigValidator::ValidationResult 
UtilityTools::ConfigValidator::validate_diff_params(int rounds, int weight_cap, 
                                                    std::uint32_t start_dA, std::uint32_t start_dB) {
    ValidationResult result;
    result.valid = true;
    
    if (rounds <= 0 || rounds > 20) {
        result.errors.push_back("Rounds must be between 1 and 20");
        result.valid = false;
    }
    
    if (weight_cap <= 0 || weight_cap > 100) {
        result.errors.push_back("Weight cap must be between 1 and 100");
        result.valid = false;
    }
    
    if (start_dA == 0 && start_dB == 0) {
        result.warnings.push_back("Starting with zero difference may not be meaningful");
    }
    
    if (rounds > 8 && weight_cap > 30) {
        result.warnings.push_back("High rounds + high weight cap may require significant computing resources");
    }
    
    return result;
}

UtilityTools::ConfigValidator::ValidationResult 
UtilityTools::ConfigValidator::validate_linear_params(int rounds, int weight_cap, 
                                                     std::uint32_t start_mA, std::uint32_t start_mB) {
    ValidationResult result;
    result.valid = true;
    
    if (rounds <= 0 || rounds > 15) {
        result.errors.push_back("Rounds must be between 1 and 15 for linear analysis");
        result.valid = false;
    }
    
    if (weight_cap <= 0 || weight_cap > 80) {
        result.errors.push_back("Weight cap must be between 1 and 80 for linear analysis");
        result.valid = false;
    }
    
    if (start_mA == 0 && start_mB == 0) {
        result.warnings.push_back("Starting with zero mask may not be meaningful");
    }
    
    return result;
}

UtilityTools::ConfigValidator::ResourceEstimate 
UtilityTools::ConfigValidator::estimate_resources(int rounds, int weight_cap, 
                                                 const std::string& algorithm) {
    ResourceEstimate estimate;
    
    // Rough estimation based on exponential growth
    double complexity_factor = std::pow(2.0, rounds * 0.8 + weight_cap * 0.2);
    
    estimate.estimated_memory_mb = static_cast<std::uint64_t>(complexity_factor / 1000);
    estimate.estimated_time_seconds = static_cast<std::uint64_t>(complexity_factor / 100);
    
    estimate.recommended_threads = std::min(8, static_cast<int>(complexity_factor / 10000) + 1);
    
    // Personal computer suitability
    estimate.suitable_for_personal_computer = 
        (rounds <= 6 && weight_cap <= 25) || 
        (rounds <= 4 && weight_cap <= 35);
    
    return estimate;
}

// ============================================================================
// PerformanceUtils implementation
// ============================================================================

std::uint64_t UtilityTools::PerformanceUtils::estimate_memory_usage(std::uint64_t num_states, 
                                                                    std::uint64_t state_size_bytes) {
    // Account for overhead: hash tables, priority queues, etc.
    const double overhead_factor = 2.5;
    return static_cast<std::uint64_t>(num_states * state_size_bytes * overhead_factor);
}

int UtilityTools::PerformanceUtils::detect_cpu_cores() {
    int cores = std::thread::hardware_concurrency();
    return (cores > 0) ? cores : 1;
}

bool UtilityTools::PerformanceUtils::has_popcount_instruction() {
    // Check if builtin popcount is available (GCC/Clang specific)
    return __builtin_popcount(0xFFFFFFFF) == 32;
}

int UtilityTools::PerformanceUtils::suggest_weight_cap_for_personal_computer(int rounds) {
    // Conservative suggestions based on rounds
    switch (rounds) {
        case 1: case 2: case 3: return 30;
        case 4: return 25;
        case 5: return 22;
        case 6: return 20;
        default: return 18;
    }
}

int UtilityTools::PerformanceUtils::suggest_max_rounds_for_personal_computer(int weight_cap) {
    // Conservative suggestions based on weight cap
    if (weight_cap >= 30) return 4;
    if (weight_cap >= 25) return 5;
    if (weight_cap >= 20) return 6;
    return 7;
}

// ============================================================================
// StringUtils implementation
// ============================================================================

std::string UtilityTools::StringUtils::to_hex(std::uint32_t value, bool uppercase, bool prefix) {
    std::ostringstream ss;
    if (prefix) ss << "0x";
    ss << std::hex;
    if (uppercase) ss << std::uppercase;
    ss << value;
    return ss.str();
}

std::uint32_t UtilityTools::StringUtils::from_hex(const std::string& hex_str) {
    std::uint32_t value;
    std::istringstream ss(hex_str);
    ss >> std::hex >> value;
    return value;
}

std::string UtilityTools::StringUtils::format_number(std::uint64_t number, char separator) {
    std::string result = std::to_string(number);
    
    int pos = result.length() - 3;
    while (pos > 0) {
        result.insert(pos, 1, separator);
        pos -= 3;
    }
    
    return result;
}

std::string UtilityTools::StringUtils::format_duration(std::uint64_t milliseconds) {
    if (milliseconds < 1000) return std::to_string(milliseconds) + "ms";
    if (milliseconds < 60000) return std::to_string(milliseconds / 1000) + "s";
    if (milliseconds < 3600000) return std::to_string(milliseconds / 60000) + "m";
    return std::to_string(milliseconds / 3600000) + "h";
}

std::string UtilityTools::StringUtils::progress_bar(double percentage, int width) {
    int filled = static_cast<int>(percentage * width / 100.0);
    std::string bar = "[";
    
    for (int i = 0; i < width; ++i) {
        if (i < filled) bar += "=";
        else if (i == filled) bar += ">";
        else bar += " ";
    }
    
    bar += "] " + std::to_string(static_cast<int>(percentage)) + "%";
    return bar;
}

std::string UtilityTools::StringUtils::trim(const std::string& str) {
    size_t start = str.find_first_not_of(" \t\n\r");
    if (start == std::string::npos) return "";
    
    size_t end = str.find_last_not_of(" \t\n\r");
    return str.substr(start, end - start + 1);
}

std::vector<std::string> UtilityTools::StringUtils::split(const std::string& str, char delimiter) {
    std::vector<std::string> result;
    std::istringstream ss(str);
    std::string item;
    
    while (std::getline(ss, item, delimiter)) {
        result.push_back(item);
    }
    
    return result;
}

} // namespace neoalz