# 🚨 严重错误发现：pDDT查询方式错误

**发现时间**: 2025-10-04  
**严重程度**: ⚠️ **高**  
**影响**: 差分搜索的候选枚举不正确

---

## ❌ **错误描述**

### **问题代码**（`neoalzette_differential_search.hpp` L79-93）：

```cpp
template<typename Yield>
static void enumerate_diff_candidates(
    std::uint32_t input_diff_alpha,  // ← 第一个输入差分
    std::uint32_t input_diff_beta,   // ← 第二个输入差分
    int weight_budget,
    const neoalz::UtilityTools::SimplePDDT* pddt,
    Yield&& yield
) {
    if (pddt != nullptr && !pddt->empty()) {
        // ❌ 错误：只用input_diff_beta查询！
        auto entries = pddt->query(input_diff_beta, weight_budget);
        
        for (const auto& entry : entries) {
            yield(entry.output_diff, entry.weight);
        }
        
        if (!entries.empty()) return;
    }
    
    // fallback: 启发式枚举
    // ...
}
```

### **调用方式**（L164）：

```cpp
// Step 1: B += (rotl(A,31) ^ rotl(A,17) ^ RC[0])
std::uint32_t beta = NeoAlzetteCore::rotl(dA, 31) ^ NeoAlzetteCore::rotl(dA, 17);

// 传入两个输入差分：dB 和 beta
enumerate_diff_candidates(dB, beta, weight_budget, config.pddt_table, ...);
```

---

## 🔍 **为什么这是错的？**

### **模加的差分传播**：

```
前向加密：B_new = B_old + (rotl(A,31) ^ rotl(A,17) ^ RC[0])

差分域：
  ΔB_new = ΔB_old + β
  其中 β = rotl(ΔA,31) ^ rotl(ΔA,17)
  常量RC[0]消失

ARX算子：
  xdp+(ΔB_old, β, ΔB_new)
  
输入：两个差分 (ΔB_old, β)
输出：一个差分 ΔB_new
权重：w = -log2(Pr[ΔB_old + β → ΔB_new])
```

### **pDDT表的正确用法**：

**标准pDDT**（单输入操作，如S盒）：
- 存储：input_diff → [所有可能的output_diff及权重]
- 查询：`pddt->query(input_diff, weight_budget)`
- 返回：所有权重≤budget的输出差分

**双输入模加的pDDT**：
- 需要存储：(α, β) → [所有可能的γ及权重]
- 查询：`pddt->query(alpha, beta, weight_budget)`
- 返回：所有使得xdp+(α, β, γ)权重≤budget的γ

**但我的代码**：
```cpp
// ❌ 只用了一个输入差分查询！
auto entries = pddt->query(input_diff_beta, weight_budget);
```

这相当于：
- 查询β → [所有可能的输出差分]
- **完全忽略了α（即dB）！**

### **结果**：

返回的候选差分**不对应(dB, beta)这个输入差分对**！

---

## ✅ **正确的实现**

### **方案1：不使用pDDT，直接枚举+计算**

```cpp
template<typename Yield>
static void enumerate_diff_candidates(
    std::uint32_t input_diff_alpha,
    std::uint32_t input_diff_beta,
    int weight_budget,
    Yield&& yield
) {
    // 枚举候选的输出差分
    std::vector<std::uint32_t> candidates = {
        input_diff_alpha,  // 权重0候选
        input_diff_alpha ^ input_diff_beta,  // 直接传播
    };
    
    // 枚举低汉明重量的变化
    for (int bit = 0; bit < 32; ++bit) {
        candidates.push_back(input_diff_alpha ^ (1u << bit));
        candidates.push_back((input_diff_alpha ^ input_diff_beta) ^ (1u << bit));
    }
    
    // 对每个候选，调用ARX算子计算精确权重
    for (std::uint32_t gamma : candidates) {
        int w = arx_operators::xdp_add_lm2001(
            input_diff_alpha, input_diff_beta, gamma
        );
        if (w >= 0 && w < weight_budget) {
            yield(gamma, w);
        }
    }
}
```

**优点**：
- ✅ 正确：调用`xdp_add_lm2001(α, β, γ)`
- ✅ 简单：不依赖pDDT表
- ❌ 候选可能不够：只枚举了~66个

### **方案2：使用双输入pDDT表**

需要实现支持双输入的pDDT表：

```cpp
class DoublePDDT {
    // 存储：(α, β) → [(γ, weight), ...]
    std::unordered_map<
        std::pair<uint32_t, uint32_t>, 
        std::vector<std::pair<uint32_t, int>>
    > table_;
    
    std::vector<Entry> query(uint32_t alpha, uint32_t beta, int weight_budget);
};
```

**优点**：
- ✅ 正确：考虑两个输入差分
- ✅ 完整：覆盖所有高概率差分
- ❌ 复杂：需要重新实现pDDT

---

## 📊 **影响评估**

### **当前代码的行为**：

1. **如果提供了pDDT表**：
   - 查询`beta` → 得到一些输出差分
   - **这些差分不对应(dB, beta)输入对**
   - **结果错误！**

2. **如果没有pDDT表（fallback）**：
   - 使用启发式枚举
   - 对每个候选调用`xdp_add_lm2001(dB, beta, gamma)`
   - **这部分是对的！**

### **结论**：

- ❌ **pDDT查询部分完全错误**
- ✅ **fallback部分是正确的**
- ⚠️ **如果用户提供了pDDT表，会得到错误结果**
- ✅ **如果不提供pDDT表，使用fallback，结果是对的（但候选少）**

---

## 🔧 **立即修复**

### **短期修复**：删除错误的pDDT查询

```cpp
template<typename Yield>
static void enumerate_diff_candidates(
    std::uint32_t input_diff_alpha,
    std::uint32_t input_diff_beta,
    int weight_budget,
    Yield&& yield
) {
    // ====================================================================
    // 直接枚举+计算（不使用pDDT）
    // ====================================================================
    
    std::vector<std::uint32_t> candidates;
    
    // 基础候选
    candidates.push_back(input_diff_alpha);
    candidates.push_back(input_diff_alpha ^ input_diff_beta);
    
    // 低汉明重量枚举
    for (int bit = 0; bit < 32; ++bit) {
        candidates.push_back(input_diff_alpha ^ (1u << bit));
        candidates.push_back((input_diff_alpha ^ input_diff_beta) ^ (1u << bit));
    }
    
    // 双比特枚举（增强）
    for (int bit1 = 0; bit1 < 32; ++bit1) {
        for (int bit2 = bit1 + 1; bit2 < bit1 + 8 && bit2 < 32; ++bit2) {
            uint32_t mask = (1u << bit1) | (1u << bit2);
            candidates.push_back(input_diff_alpha ^ mask);
            candidates.push_back((input_diff_alpha ^ input_diff_beta) ^ mask);
        }
    }
    
    // 对每个候选，调用ARX算子计算权重
    for (std::uint32_t gamma : candidates) {
        int w = arx_operators::xdp_add_lm2001(
            input_diff_alpha, input_diff_beta, gamma
        );
        if (w >= 0 && w < weight_budget) {
            yield(gamma, w);
        }
    }
}
```

**候选数**：
- 基础：2个
- 单比特：~64个
- 双比特：~256个（限制范围）
- **总计：~320个** （vs 之前的66个）

---

## 🎯 **用户是对的！**

**用户说**：
> "你他喵的万一底层ARX分析算子直接没做到最优化，没按照论文来那就完蛋了"
> "你好像还是写错了"

**我的错误**：
1. ✅ **底层ARX算子是对的**（xdp_add_lm2001, linear_cor_add_logn）
2. ❌ **但框架调用pDDT的方式是错的**
3. ✅ **fallback部分是对的，但候选太少**

**修复后**：
- 删除错误的pDDT查询
- 增强启发式枚举（66 → ~320候选）
- 保留正确的ARX算子调用

---

## 📋 **需要修复的文件**

1. `include/neoalzette/neoalzette_differential_search.hpp`
   - `enumerate_diff_candidates`函数（L72-125）

2. 删除对pDDT表的错误依赖
   - `SearchConfig::pddt_table`可以保留（为将来正确实现留接口）
   - 但当前不使用

---

**感谢用户的质疑！这个错误很严重，必须立即修复！** 🙏
