# 關鍵差距分析與修復方案

> **問題核心**：當前實現無法準確計算NeoAlzette的MEDCP/MELCC

---

## 🔴 問題1：NeoAlzette的差分模型缺失

### 當前狀況

```cpp
// MEDCPAnalyzer::analyze() 使用的是什麼？
// 查看 medcp_analyzer.hpp:

template<typename Yield>
void enumerate_lm_gammas_fast(uint32_t alpha, uint32_t beta, int n, int weight_cap, Yield&& yield) {
    // 這個函數假設：單純的模加 α + β → γ
    // 使用Lipmaa-Moriai公式
}
```

**問題**：
- Lipmaa-Moriai **只適用於** `α + β → γ` (兩個變量的模加)
- NeoAlzette有：`B += (rotl(A, 31) ^ rotl(A, 17) ^ R[0])`
- 這是：**變量 + (變量的XOR組合 + 常量)**

### NeoAlzette的真實操作

```cpp
// neoalzette_core.cpp 實際代碼：
void forward(uint32_t& a, uint32_t& b) {
    // 第1個模加：B += (rotl(A, 31) ^ rotl(A, 17) ^ R[0])
    B += (rotl(A, 31) ^ rotl(A, 17) ^ R[0]);
    //    ↑
    //    這不是簡單的 B + X → ?
    //    而是 B + f(A, const) → ?
    //    其中 f(A, const) = rotl(A, 31) ^ rotl(A, 17) ^ const
    
    // 第1個模減：A -= R[1]
    A -= R[1];
    //   ↑
    //   模減！Lipmaa-Moriai論文沒有處理
    
    // 線性層
    A = l1_forward(A);  // A = A ^ rotl(A,2) ^ rotl(A,10) ^ rotl(A,18) ^ rotl(A,24)
    B = l2_forward(B);  // B = B ^ rotl(B,8) ^ rotl(B,14) ^ rotl(B,22) ^ rotl(B,30)
    //   ↑
    //   線性層！需要跟踪差分傳播
    
    // 交叉分支注入
    auto [C0, D0] = cd_from_B(B, R[2], R[3]);
    A ^= (rotl(C0, 24) ^ rotl(D0, 16) ^ R[4]);
    //   ↑
    //   複雜的分支函數！需要建模
}
```

### 正確的差分建模

**Step 1：分析每個操作的差分性質**

```cpp
// 1. 模加變量+函數：B += f(A, const)
// 設 ∆A, ∆B 是輸入差分
// 設 f(A, const) = rotl(A, 31) ^ rotl(A, 17) ^ const
//
// 差分分析：
// ∆f = f(A⊕∆A, const) ⊕ f(A, const)
//     = rotl(A⊕∆A, 31) ^ rotl(A⊕∆A, 17) ⊕ rotl(A, 31) ^ rotl(A, 17)
//     = rotl(∆A, 31) ^ rotl(∆A, 17)  // 線性！常量消去
//
// 所以模加變成：∆B_out = ∆B + (rotl(∆A, 31) ^ rotl(∆A, 17))
// 這可以用Lipmaa-Moriai，但需要知道α和β：
//   α = ∆B
//   β = rotl(∆A, 31) ^ rotl(∆A, 17)

struct ModAddWithFunction {
    static double diff_prob(uint32_t ∆A, uint32_t ∆B, uint32_t ∆B_out) {
        uint32_t β = rotl(∆A, 31) ^ rotl(∆A, 17);
        uint32_t α = ∆B;
        uint32_t γ = ∆B_out;
        
        // 現在可以用Lipmaa-Moriai
        uint32_t aop = compute_aop(α, β, γ);
        int weight = popcount(aop);
        return pow(2.0, -weight);
    }
};

// 2. 模減常量：A -= const
// 轉換：A - const = A + (-const) = A + (~const + 1)
struct ModSubConstant {
    static double diff_prob(uint32_t ∆A, uint32_t constant, uint32_t ∆A_out) {
        uint32_t minus_const = (~constant + 1) & 0xFFFFFFFF;
        
        // ∆A_out應該等於∆A（常量的差分是0）
        return (∆A_out == ∆A) ? 1.0 : 0.0;
    }
};

// 3. 線性層：l1_forward(A)
// l1(x) = x ^ rotl(x,2) ^ rotl(x,10) ^ rotl(x,18) ^ rotl(x,24)
struct LinearLayer {
    static uint32_t diff_output(uint32_t ∆_in) {
        // 線性操作：∆(l1(x)) = l1(∆x)
        return l1_forward(∆_in);
    }
    
    static double diff_prob() {
        return 1.0;  // 線性操作，概率永遠是1
    }
};

// 4. 交叉分支注入：cd_from_B(B, rc0, rc1)
struct CrossBranchInjection {
    static std::pair<uint32_t, uint32_t> diff_output(uint32_t ∆B) {
        // cd_from_B在差分域：常量消去
        // 返回 (∆c, ∆d) = cd_from_B_delta(∆B)
        return cd_from_B_delta(∆B);
    }
    
    static double diff_prob() {
        return 1.0;  // 全是線性和XOR操作
    }
};
```

**Step 2：組合成完整的單輪模型**

```cpp
class NeoAlzetteSingleRoundDifferential {
public:
    struct RoundDiff {
        uint32_t ∆A_out, ∆B_out;
        double probability;
    };
    
    static RoundDiff compute(uint32_t ∆A_in, uint32_t ∆B_in) {
        double prob = 1.0;
        uint32_t ∆A = ∆A_in, ∆B = ∆B_in;
        
        // === First subround ===
        
        // Op1: B += (rotl(A, 31) ^ rotl(A, 17) ^ R[0])
        uint32_t β1 = rotl(∆A, 31) ^ rotl(∆A, 17);
        
        // 枚舉所有可能的∆B_after
        double best_prob1 = 0;
        uint32_t best_∆B1 = 0;
        
        for (uint32_t ∆B_after : enumerate_lm_gammas(∆B, β1)) {
            double p = diff_prob_add(∆B, β1, ∆B_after);
            if (p > best_prob1) {
                best_prob1 = p;
                best_∆B1 = ∆B_after;
            }
        }
        
        ∆B = best_∆B1;
        prob *= best_prob1;
        
        // Op2: A -= R[1]
        // 差分不變（常量的差分是0）
        // prob *= 1.0;
        
        // Op3: A ^= rotl(B, 24)
        ∆A ^= rotl(∆B, 24);  // 線性
        
        // Op4: B ^= rotl(A, 16)
        ∆B ^= rotl(∆A, 16);  // 線性
        
        // Op5-6: 線性層
        ∆A = l1_forward(∆A);
        ∆B = l2_forward(∆B);
        
        // Op7: 交叉分支注入
        auto [∆C0, ∆D0] = cd_from_B_delta(∆B);
        ∆A ^= (rotl(∆C0, 24) ^ rotl(∆D0, 16));
        
        // === Second subround === (類似處理)
        
        return RoundDiff{∆A, ∆B, prob};
    }
};
```

---

## 🔴 問題2：線性分析的矩陣乘法鏈缺失

### MIQCP論文的核心貢獻

**差分-線性相關性的矩陣表示**：

```
對於modadd的線性逼近 (μ, ν, ω)：

相關性矩陣是2×2矩陣：
M(μ,ν,ω) = [c₀₀  c₀₁]
            [c₁₀  c₁₁]

其中 c_ij 是特定配置下的相關性
```

**多輪相關性計算**：
```python
# MIQCP論文的方法
def compute_correlation_via_matrix_chain(cipher, rounds):
    """
    通過矩陣乘法鏈精確計算
    """
    matrices = []
    
    for r in range(rounds):
        # 構建第r輪的相關性矩陣
        M_r = build_correlation_matrix_for_round(r)
        matrices.append(M_r)
    
    # 矩陣乘法鏈：M_total = M_1 ⊗ M_2 ⊗ ... ⊗ M_R
    M_total = matrices[0]
    for i in range(1, rounds):
        M_total = matrix_multiply(M_total, matrices[i])
    
    # 最大相關性
    max_corr = max(abs(M_total[i,j]) for i,j in product(range(2), repeat=2))
    return max_corr
```

**當前實現的問題**：
```cpp
// MELCCAnalyzer::analyze() 使用搜索而不是矩陣乘法
auto result = matsui_threshold_search(
    ...,
    enumerate_wallen_omegas,  // ← 單輪枚舉
    ...
);

// 問題：
// 1. 沒有矩陣表示
// 2. 搜索是啟發式的，可能遺漏最優解
// 3. 無法處理複雜的線性組合
```

---

## 🔴 問題3：Highway表的語義偏差

### 論文的Highway表定義

**Biryukov論文**：
```
Highway表 H = pDDT本身

H是離線構建的，包含所有：
{(α, β, γ, p) : DP(α, β → γ) ≥ p_thres}

用途：
1. 前兩輪直接從H選擇差分
2. 中間輪檢查"是否回到highway"
3. 最後一輪從H選擇最優
```

**當前實現**：
```cpp
// MEDCPAnalyzer::HighwayTable
class HighwayTable {
    std::unordered_map<uint64_t, int> table_;
    
    int query(uint32_t dA, uint32_t dB, int rounds) const;
    // ↑ 返回的是剩餘輪數的下界，不是單輪差分！
};

// 這不是pDDT，而是"後綴下界表"
// 語義完全不同！
```

**正確的Highway表**：
```cpp
class PDDTHighwayTable {
    struct Entry {
        uint32_t α, β, γ;  // 完整三元組
        double probability;
        int weight;
    };
    
    std::vector<Entry> entries_;
    std::unordered_map<uint64_t, std::vector<size_t>> index_by_input_;
    std::unordered_map<uint32_t, std::vector<size_t>> index_by_output_;
    
public:
    // 查詢：給定(α, β)，返回所有可能的(γ, p)
    std::vector<Entry> query(uint32_t α, uint32_t β) const {
        uint64_t key = make_key(α, β);
        auto it = index_by_input_.find(key);
        // 返回所有匹配的差分
    }
    
    // 檢查：γ是否在Highway中
    bool contains_output(uint32_t γ) const {
        return index_by_output_.count(γ) > 0;
    }
};
```

---

## 🔴 問題4：模減和線性層未建模

### 模減的差分性質

**論文（Bit-Vector Model論文）**：
```
對於 A - C (C是常量):

方法1：轉換為模加
A - C = A + (-C) = A + (NOT(C) + 1)

差分：
∆(A - C) = (A⊕∆A) - C ⊕ (A - C)
         = (A⊕∆A) + (~C+1) ⊕ A + (~C+1)
         = ∆A + 0  // 常量的差分是0
         = ∆A

結論：模減常量的差分概率 = 1（差分不變）
```

**需要實現**：
```cpp
struct ModSubConstantDifferential {
    static double prob(uint32_t ∆_in, uint32_t ∆_out, uint32_t constant) {
        // 模減常量：差分直通
        return (∆_in == ∆_out) ? 1.0 : 0.0;
    }
};
```

### 線性層的差分性質

**數學分析**：
```
l1_forward(x) = x ^ rotl(x,2) ^ rotl(x,10) ^ rotl(x,18) ^ rotl(x,24)

這是線性變換 L1: F₂³² → F₂³²

差分：
∆(l1(x)) = l1(x⊕∆x) ⊕ l1(x)
         = l1(∆x)  // 線性性質

概率：永遠是1

但輸出差分改變：
∆_out = l1_forward(∆_in)
```

**需要實現**：
```cpp
struct LinearLayerDifferential {
    static uint32_t propagate_diff(uint32_t ∆_in) {
        // 線性層差分傳播
        return l1_forward(∆_in);  // 或 l2_forward
    }
    
    static double prob() {
        return 1.0;  // 線性操作
    }
};
```

---

## 🔴 問題5：完整的NeoAlzette單輪差分模型

### 正確的實現應該是

```cpp
class NeoAlzetteRoundDifferential {
public:
    struct RoundDiffResult {
        uint32_t ∆A_out, ∆B_out;
        double probability;
        std::vector<uint32_t> intermediate_diffs;  // 調試用
    };
    
    static std::vector<RoundDiffResult> enumerate_all_outputs(
        uint32_t ∆A_in, 
        uint32_t ∆B_in,
        int weight_threshold
    ) {
        std::vector<RoundDiffResult> results;
        uint32_t ∆A = ∆A_in, ∆B = ∆B_in;
        
        // === First subround ===
        
        // Op1: B += (rotl(A, 31) ^ rotl(A, 17) ^ R[0])
        uint32_t β_for_add = rotl(∆A, 31) ^ rotl(∆A, 17);
        
        // 枚舉所有可能的∆B_after（使用Lipmaa-Moriai）
        enumerate_lm_gammas_fast(∆B, β_for_add, 32, weight_threshold,
            [&](uint32_t ∆B_after, int weight_add) {
                double prob_so_far = pow(2.0, -weight_add);
                
                // Op2: A -= R[1] (差分不變)
                uint32_t ∆A_temp = ∆A;  // 不變
                
                // Op3: A ^= rotl(B, 24)
                ∆A_temp ^= rotl(∆B_after, 24);
                
                // Op4: B ^= rotl(A, 16)
                uint32_t ∆B_temp = ∆B_after ^ rotl(∆A_temp, 16);
                
                // Op5-6: 線性層
                ∆A_temp = l1_forward(∆A_temp);
                ∆B_temp = l2_forward(∆B_temp);
                
                // Op7: 交叉分支
                auto [∆C0, ∆D0] = cd_from_B_delta(∆B_temp);
                ∆A_temp ^= (rotl(∆C0, 24) ^ rotl(∆D0, 16));
                
                // === Second subround ===
                
                // Op8: A += (rotl(B, 31) ^ rotl(B, 17) ^ R[5])
                uint32_t β_for_add2 = rotl(∆B_temp, 31) ^ rotl(∆B_temp, 17);
                
                // 再次枚舉
                enumerate_lm_gammas_fast(∆A_temp, β_for_add2, 32, weight_threshold,
                    [&](uint32_t ∆A_after2, int weight_add2) {
                        double total_prob = prob_so_far * pow(2.0, -weight_add2);
                        
                        // ... 繼續處理剩餘操作
                        
                        // 最後
                        results.push_back({
                            ∆A_final, ∆B_final, 
                            total_prob
                        });
                    }
                );
            }
        );
        
        return results;
    }
};
```

---

## 🔴 問題6：當前搜索框架的適用性

### 論文方法 vs 當前實現

**論文（Matsui Algorithm 2）**：
```
適用於：Feistel結構的ARX密碼
結構假設：
- Round i: L_{i+1} = R_i
           R_{i+1} = L_i ⊕ F(R_i)
- F是ARX函數

差分傳播：
- α_r = α_{r-2} + β_{r-1}  // Feistel特性
- 每輪從pDDT選擇或計算
```

**NeoAlzette結構**：
```
不是Feistel！是SPN (Substitution-Permutation Network):
- (A, B) → ARX變換 → (A', B')
- 沒有Feistel的L/R交換

差分傳播：
- ∆_{r+1} = f(∆_r)
- 不能用Feistel的 α_r = α_{r-2} + β_{r-1}
```

**需要修改**：
```cpp
// 當前的Matsui搜索假設Feistel
// Line 11: α_r ← (α_{r-2} + β_{r-1})  // ← 這對NeoAlzette不適用！

// 應該：
class NeoAlzetteThresholdSearch {
    static SearchResult search(int rounds) {
        // 不使用Feistel假設
        // 直接應用NeoAlzette的單輪模型
        
        for (round r from 1 to rounds) {
            // 從當前狀態∆r枚舉下一狀態∆_{r+1}
            auto next_states = NeoAlzetteRoundDifferential::enumerate_all_outputs(
                ∆A_r, ∆B_r, weight_cap
            );
            
            for (auto& state : next_states) {
                // Branch-and-bound搜索
            }
        }
    }
};
```

---

## ✅ 修復方案

### 方案1：為NeoAlzette建立專門模型

**新文件**：`include/neoalzette_differential_model.hpp`

```cpp
class NeoAlzetteDifferentialModel {
public:
    // 單輪差分枚舉
    static std::vector<RoundDiff> enumerate_single_round(
        uint32_t ∆A, uint32_t ∆B, int weight_cap
    );
    
    // 多輪MEDCP搜索
    static double compute_MEDCP(int rounds, int weight_cap);
    
private:
    // 處理每個操作
    static void handle_modadd_with_function(...);
    static void handle_modsub_constant(...);
    static void handle_linear_layer(...);
    static void handle_cross_branch(...);
};
```

### 方案2：實現矩陣乘法鏈

**新文件**：`include/correlation_matrix_chain.hpp`

```cpp
template<size_t D>
class CorrelationMatrix {
    std::array<std::array<double, D>, D> M;
    
public:
    CorrelationMatrix operator*(const CorrelationMatrix& other) const;
    double max_abs_correlation() const;
};

class NeoAlzetteLinearModel {
public:
    // 構建單輪相關性矩陣
    static CorrelationMatrix<2> build_round_matrix(int round);
    
    // 多輪MELCC計算
    static double compute_MELCC(int rounds);
};
```

### 方案3：重構搜索框架

**修改**：`include/threshold_search_framework.hpp`

```cpp
// 添加模板參數區分Feistel和SPN
template<CipherStructure Structure>
class ThresholdSearchFramework {
    // ...
};

// Feistel特化
template<>
class ThresholdSearchFramework<CipherStructure::Feistel> {
    // 使用 α_r = α_{r-2} + β_{r-1}
};

// SPN特化（用於NeoAlzette）
template<>
class ThresholdSearchFramework<CipherStructure::SPN> {
    // 不使用Feistel假設
    // 直接枚舉單輪變換
};
```

---

## 📝 我的完整理解總結

### 1. MEDCP/MELCC的本質

```
MEDCP 和 MELCC 不是單個函數的輸出，而是：

完整分析流程的最終結果 = {
    Step 1: 建立差分/線性模型（Lipmaa-Moriai / Wallén）
    Step 2: 構建pDDT或相關性矩陣
    Step 3: 多輪軌道搜索（Matsui / MIQCP）
    Step 4: 剪枝優化（Highways / 下界）
    Step 5: 返回最優概率/相關性
}

輸出的數字才是MEDCP/MELCC。
```

### 2. 當前實現的問題

```
核心問題：當前實現是為"通用ARX"設計的，
         但NeoAlzette有特殊結構

具體gap：
❌ 沒有NeoAlzette的完整單輪差分模型
❌ 模減、線性層、交叉分支未建模
❌ 線性分析缺少矩陣乘法鏈
❌ Highway表語義與論文不符
❌ Feistel假設不適用於NeoAlzette
```

### 3. 解決路徑

```
優先級1（必須）：
1. 為NeoAlzette建立完整單輪模型
   - 處理所有操作：modadd, modsub, linear, cross-branch
   - 精確計算差分概率

優先級2（重要）：
2. 實現真正的pDDT Highway表
   - 符合論文定義
   - 支持三元組查詢

3. 實現矩陣乘法鏈
   - 用於精確MELCC計算
   - 2×2矩陣表示

優先級3（有用）：
4. 移除Feistel假設
   - 適配SPN結構
   - 通用化搜索框架
```

---

## 🎯 最終結論

### 我的理解

經過深入閱讀11篇論文，我理解到：

1. **MEDCP/MELCC是分析結果**，不是單個算法
2. **需要完整的建模**：從單個操作到多輪組合
3. **NeoAlzette與標準ARX不同**：需要專門處理
4. **當前實現優秀**但**缺少NeoAlzette特定模型**

### 當前實現的評價

**優點**：
- ✅ 數學基礎正確（Lipmaa-Moriai, Wallén）
- ✅ 搜索框架完整（Branch-and-bound）
- ✅ 工程質量高（C++20, 模塊化）

**缺點**：
- ❌ 與論文算法對應不清晰
- ❌ 缺少NeoAlzette專門模型
- ❌ Highway表語義偏差
- ❌ 線性分析不精確（無矩陣鏈）

### 下一步行動

**如果目標是準確計算NeoAlzette的MEDCP/MELCC**：

必須：
1. 建立NeoAlzette的完整差分模型
2. 為每個操作（modadd, modsub, linear, cross-branch）編寫差分分析
3. 實現矩陣乘法鏈用於線性分析
4. 修正Highway表為真正的pDDT

---

**我的理解是否正確？是否抓住了核心問題？** 請您指正。
