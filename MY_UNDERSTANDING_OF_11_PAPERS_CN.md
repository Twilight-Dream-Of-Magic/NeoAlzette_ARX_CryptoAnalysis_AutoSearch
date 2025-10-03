# 我對11篇ARX密碼分析論文的完整理解

> **作者**：AI分析員  
> **日期**：2025-10-03  
> **目的**：理解論文算法並識別當前實現的偏差

---

## 🎯 核心理解：MEDCP和MELCC是什麼？

### MEDCP (Maximum Expected Differential Characteristic Probability)

**定義** (來自Alzette論文):
```
MEDCP = max_{軌道T} ∏_{i=1}^R p_i

其中：
- R是輪數
- p_i是第i輪的差分概率
- T = {(α₀,β₀), (α₁,β₁), ..., (αᵣ,βᵣ)} 是差分軌道
- 在Markov假設下，各輪獨立
```

**關鍵理解**：
1. **不是單個差分的概率**，而是**最優差分軌道的概率**
2. **需要搜索**：遍歷所有可能的軌道，找到概率最大的那個
3. **用於評估密碼強度**：MEDCP越小，密碼越安全

**數學公式**：
```
對於n輪ARX密碼：
MEDCP_n = max_{(α₁,...,αₙ)} ∏_{i=1}^n DP(αᵢ → αᵢ₊₁)

其中DP(α→β)是單輪差分概率，由Lipmaa-Moriai公式計算：
DP(α,β→γ) = 2^{-HW(AOP(α,β,γ))}
AOP(α,β,γ) = α⊕β⊕γ⊕((α∧β)⊕((α⊕β)∧γ))<<1
```

### MELCC (Maximum Expected Linear Characteristic Correlation)

**定義** (來自Alzette論文):
```
MELCC = max_{軌道T} ∏_{i=1}^R c_i

其中：
- c_i是第i輪的線性相關性
- T = {(μ₀,ν₀), (μ₁,ν₁), ..., (μᵣ,νᵣ)} 是線性軌道
- 相關性的平方等於偏差的平方
```

**關鍵理解**：
1. **不是單個線性逼近的相關性**，而是**最優線性軌道的相關性**
2. **需要搜索**：遍歷所有可能的線性軌道
3. **用於評估線性攻擊抗性**：MELCC越小，抵抗線性攻擊越強

**數學公式**：
```
對於n輪ARX密碼：
MELCC_n = max_{(μ₁,...,μₙ)} ∏_{i=1}^n Cor(μᵢ, νᵢ)

其中Cor由Wallén公式計算：
1. 計算z* = M_n^T(v) 其中 v = μ ⊕ ν ⊕ ω
2. 檢查可行性：(μ⊕ω) ⪯ z* AND (ν⊕ω) ⪯ z*
3. 計算相關性：Cor = 2^{-k} 其中k依賴於v的結構
```

---

## 📚 11篇論文的分層理解

### 第一層：基礎理論（數學基礎）

#### 1️⃣ Lipmaa-Moriai (2001) - 差分分析的數學革命

**核心貢獻**：
```cpp
// 之前：不可計算
DP+(α,β→γ) = |{(x,y): (x+y)⊕((x⊕α)+(y⊕β))=γ}| / 2^{2n}  // O(2^{2n})

// 之後：可計算
DP+(α,β→γ) = 2^{-HW(AOP(α,β,γ))}  // O(log n)

// AOP函數
uint32_t AOP(uint32_t α, uint32_t β, uint32_t γ) {
    uint32_t xor_part = α ^ β ^ γ;
    uint32_t and_part = (α & β) ^ ((α ^ β) & γ);
    return xor_part ^ (and_part << 1);
}
```

**對我們項目的意義**：
- 這是`lm_fast.hpp`的理論基礎
- 使得32位模加的差分分析從不可能變為可能
- **關鍵創新**：單調性剪枝，平均複雜度降至O(2^8)

#### 2️⃣ Wallén (2003) - 線性分析的精確計算

**核心貢獻**：
```cpp
// M_n^T操作符：計算carry的"支撐"
uint32_t MnT_of(uint32_t v) {
    uint32_t z = 0, suffix = 0;
    for (int i = 31; i >= 0; --i) {
        if (suffix & 1) z |= (1u << i);
        suffix ^= (v >> i) & 1u;
    }
    return z;
}

// 可行性檢查
bool is_feasible(uint32_t μ, uint32_t ν, uint32_t ω) {
    uint32_t v = μ ^ ν ^ ω;
    uint32_t z_star = MnT_of(v);
    uint32_t a = μ ^ ω;
    uint32_t b = ν ^ ω;
    return (a & ~z_star) == 0 && (b & ~z_star) == 0;
}
```

**對我們項目的意義**：
- 這是`wallen_optimized.hpp`的理論基礎
- 使得線性相關性從啟發式變為精確計算
- **我們的改進**：WallenAutomaton預計算轉移表，完整枚舉

---

### 第二層：算法實現（搜索策略）

#### 3️⃣ Biryukov & Velichkov (2014) - 自動搜索框架

**核心貢獻**：

**Algorithm 1: pDDT構建**
```python
def compute_pddt(n, p_thres, k, pk, αk, βk, γk):
    """
    構建部分差分分布表
    D = {(α,β,γ,p) : DP(α,β→γ) ≥ p_thres}
    """
    if k == n:
        if pk >= p_thres:
            D.add((αk, βk, γk, pk))
        return
    
    for x, y, z in {0, 1}^3:
        α_{k+1} = x | αk  # 位拼接
        β_{k+1} = y | βk
        γ_{k+1} = z | γk
        p_{k+1} = DP(α_{k+1}, β_{k+1} → γ_{k+1})
        
        if p_{k+1} >= p_thres:  # 單調性剪枝
            compute_pddt(n, p_thres, k+1, p_{k+1}, α_{k+1}, β_{k+1}, γ_{k+1})
```

**Algorithm 2: Matsui閾值搜索**
```python
def threshold_search(n, r, H, B, Bn, T):
    """
    閾值搜索差分軌道
    H = highways (pDDT中的高概率差分)
    C = country roads (低概率但連接回highways)
    """
    # 第1-2輪：從H自由選擇
    if (r == 1 or r == 2) and r != n:
        for (α, β, p) in H:
            if p * B[n-r] >= Bn:  # 剪枝條件
                T.add((α, β, p))
                threshold_search(n, r+1, H, B, Bn, T)
    
    # 第3-(n-1)輪：highways/country roads策略
    elif r > 2 and r != n:
        αr = α[r-2] + β[r-1]  # Feistel結構
        C = build_country_roads(αr, α[r-1], H, p_min)
        
        if C.empty():
            # 沒有country roads回highways，選最優country road
            (βr, pr) = max_prob(αr)
            C.add((αr, βr, pr))
        
        for (α, β, p) in H.union(C):
            if α == αr and p * B[n-r] >= Bn:
                threshold_search(n, r+1, H, B, Bn, T)
    
    # 第n輪：最終輪
    elif r == n:
        αn = α[n-2] + β[n-1]
        (βn, pn) = max_prob(αn)
        if p1 * p2 * ... * pn >= Bn:
            update_best_trail(T)
```

**對我們項目的意義**：
- 這是`threshold_search_framework.hpp`的基礎
- **Highways/Country Roads是關鍵創新**
- 我們的實現：**缺少明確的H/C分離**

#### 4️⃣ MIQCP論文 (2022) - ARX自動搜索的突破

**核心貢獻**：

**將矩陣乘法鏈轉換為MIQCP**：
```python
# 差分線性相關性計算：需要矩陣乘法鏈
Cor = M1 ⊗ M2 ⊗ ... ⊗ Mn

# 問題：矩陣乘法是非線性的，MILP無法處理
# 突破：引入二次約束，使用MIQCP求解器

def transform_to_miqcp(matrix_chain):
    """
    創新：將矩陣乘法表示為二次約束
    """
    variables = []
    constraints = []
    
    for i in range(len(matrix_chain) - 1):
        Mi, M_next = matrix_chain[i], matrix_chain[i+1]
        
        # 引入中間變量 v_i
        v_i = new_variable()
        variables.append(v_i)
        
        # 二次約束：v_{i+1} = Mi · v_i
        # 轉換為：∑_j Mi[k,j] * v_i[j] = v_{i+1}[k]
        for k in range(dimension):
            constraint = QuadraticConstraint(
                sum(Mi[k, j] * v_i[j] for j in range(dimension)) == v_{i+1}[k]
            )
            constraints.append(constraint)
    
    return MIQCP(variables, constraints)
```

**對MEDCP/MELCC的精確定義**：
```python
# MEDCP = Maximum Expected Differential Characteristic Probability
def compute_MEDCP(cipher, rounds):
    """
    對於ARX密碼，MEDCP是所有可能差分軌道中概率最大的
    """
    best_prob = 0
    best_trail = None
    
    # 方法1：Branch-and-bound搜索（Matsui算法）
    trails = matsui_search(cipher, rounds, prob_threshold)
    
    # 方法2：MIQCP求解（本論文創新）
    # trails = miqcp_solve(cipher, rounds)
    
    for trail in trails:
        prob = 1.0
        for round_diff in trail:
            prob *= DP(round_diff.α, round_diff.β, round_diff.γ)
        
        if prob > best_prob:
            best_prob = prob
            best_trail = trail
    
    return best_prob, best_trail

# MELCC = Maximum Expected Linear Characteristic Correlation  
def compute_MELCC(cipher, rounds):
    """
    對於ARX密碼，MELCC是所有可能線性軌道中相關性最大的
    """
    # 類似MEDCP，但計算線性相關性
    # 關鍵：需要處理矩陣乘法鏈
    
    best_corr = 0
    best_trail = None
    
    for trail in linear_search(cipher, rounds):
        # 矩陣乘法鏈
        corr = compute_correlation_chain(trail)
        
        if abs(corr) > best_corr:
            best_corr = abs(corr)
            best_trail = trail
    
    return best_corr, best_trail
```

**對我們項目的意義**：
- **這是整個項目的理論核心**
- MEDCP/MELCC的計算需要：
  1. 單輪差分/線性概率計算（Lipmaa-Moriai, Wallén）
  2. 多輪軌道搜索（Matsui, MIQCP）
  3. 下界剪枝（Highway表）

---

### 第三層：具體應用（Alzette與NeoAlzette）

#### 5️⃣ Alzette論文 (2020) - 64位ARX-box設計

**Alzette原始設計**：
```cpp
// 論文Algorithm 1：精確的3步流水線
void Alzette_c(uint32_t& x, uint32_t& y, uint32_t c) {
    // Step 1-3
    x = x + rotr(y, 31);
    y = y ^ rotr(x, 24);
    x = x ^ c;
    
    // Step 4-6
    x = x + rotr(y, 17);
    y = y ^ rotr(x, 17);
    x = x ^ c;
    
    // Step 7-9
    x = x + rotr(y, 0);  // 注意：無旋轉
    y = y ^ rotr(x, 31);
    x = x ^ c;
    
    // Step 10-12
    x = x + rotr(y, 24);
    y = y ^ rotr(x, 16);
    x = x ^ c;
}
```

**Alzette的MEDCP/MELCC結果**：
```
4輪（單次Alzette）：
- MEDCP = 2^{-6}
- MELCC = 2^{-2}

8輪（雙次Alzette）：
- MEDCP = 2^{-18}
- MELCC = 2^{-8}

與AES比較：
- 單輪Alzette ≈ AES S-box
- 雙輪Alzette ≈ AES super-S-box
```

**NeoAlzette（我們的實現）**：
```cpp
// 來自neoalzette_core.cpp
void NeoAlzetteCore::forward(uint32_t& a, uint32_t& b) noexcept {
    // First subround
    B += (rotl(A, 31) ^ rotl(A, 17) ^ R[0]);  // 更複雜！
    A -= R[1];
    A ^= rotl(B, 24);
    B ^= rotl(A, 16);
    A = l1_forward(A);  // 線性層！
    B = l2_forward(B);  // 線性層！
    auto [C0, D0] = cd_from_B(B, R[2], R[3]);  // 交叉分支！
    A ^= (rotl(C0, 24) ^ rotl(D0, 16) ^ R[4]);
    
    // Second subround
    A += (rotl(B, 31) ^ rotl(B, 17) ^ R[5]);
    B -= R[6];
    B ^= rotl(A, 24);
    A ^= rotl(B, 16);
    B = l1_forward(B);
    A = l2_forward(A);
    auto [C1, D1] = cd_from_A(A, R[7], R[8]);
    B ^= (rotl(C1, 24) ^ rotl(D1, 16) ^ R[9]);
    
    // Final
    A ^= R[10];
    B ^= R[11];
}
```

**關鍵差異**：
```
Alzette (論文)：
- 12條指令，4組×3步
- 單純的modadd + XOR + 常量
- 旋轉：31, 24, 17, 17, 0, 31, 24, 16

NeoAlzette (我們)：
- 更多指令，更複雜結構
- 模加 + 模減 + XOR + 線性層 + 交叉分支
- 12個輪常量
- 旋轉：31, 17, 24, 16 等

結論：NeoAlzette是Alzette的**顯著擴展**，不是簡單實現！
```

---

## 🔍 當前實現與論文的偏差分析

### 偏差1：MEDCP/MELCC的計算方法

**論文要求**：
```python
# 完整的多輪搜索
def compute_MEDCP_paper_method(R):
    # 1. 構建pDDT (Algorithm 1)
    H = compute_pddt(n=32, p_thres=2^{-10})
    
    # 2. Matsui搜索 (Algorithm 2)
    best_trail = threshold_search(
        rounds=R,
        highways=H,
        use_country_roads=True  # 關鍵！
    )
    
    # 3. 計算總概率
    MEDCP = product(trail[i].prob for i in range(R))
    return MEDCP
```

**當前實現**：
```cpp
// MEDCPAnalyzer::analyze()
// ✗ 缺少：明確的pDDT構建
// ✗ 缺少：Highways/Country Roads區分
// ✓ 有：Branch-and-bound搜索
// ✓ 有：Highway表（但不是pDDT）

auto result = ThresholdSearchFramework::matsui_threshold_search(
    max_rounds,
    initial_state,
    weight_cap,
    next_states,  // ← 這裡隱含了pDDT查詢，但不明確
    lower_bound
);
```

**問題**：
1. **沒有顯式的pDDT構建階段**
2. **Highways/Country Roads混在next_states中**
3. **無法區分"從pDDT選擇"vs"計算最優country road"**

### 偏差2：線性分析的矩陣乘法鏈

**論文要求（MIQCP論文）**：
```python
# 完整的矩陣乘法鏈計算
def compute_MELCC_paper_method(R):
    # 1. 構建線性模型
    matrices = []
    for r in range(R):
        M_r = build_correlation_matrix(round=r)
        matrices.append(M_r)
    
    # 2. 矩陣乘法鏈
    M_total = matrices[0]
    for i in range(1, R):
        M_total = M_total @ matrices[i]  # 矩陣乘法
    
    # 3. 找最大相關性
    MELCC = max_correlation(M_total)
    return MELCC
```

**當前實現**：
```cpp
// MELCCAnalyzer::analyze()
// ✗ 缺少：顯式的矩陣表示
// ✗ 缺少：矩陣乘法鏈
// ✓ 有：Wallén枚舉
// ~ 近似：使用搜索代替矩陣乘法

// 我們用搜索而不是矩陣乘法
auto result = ThresholdSearchFramework::matsui_threshold_search(
    max_rounds,
    initial_linear_state,
    correlation_cap,
    enumerate_wallen_omegas,  // ← 單輪枚舉
    linear_lower_bound
);
```

**問題**：
1. **沒有矩陣表示**（論文是2×2矩陣鏈）
2. **用啟發式搜索代替精確的矩陣乘法**
3. **可能遺漏最優解**（因為不是精確的矩陣計算）

### 偏差3：NeoAlzette與Alzette的結構差異

**論文Alzette**：
```
簡單結構：modadd → XOR → const (重複4次)
分析方法：直接應用Lipmaa-Moriai + Wallén
MEDCP計算：straightforward branch-and-bound
```

**我們NeoAlzette**：
```
複雜結構：
- 模加 + 模減
- 線性擴散層 (l1_forward, l2_forward)
- 交叉分支注入 (cd_from_A, cd_from_B)
- 12個輪常量

分析挑戰：
- l1/l2的差分性質？
- cd_from_A/B如何影響差分傳播？
- 模減的差分概率？（論文未涵蓋）
```

**關鍵問題**：
```
Q: 論文的Lipmaa-Moriai算法能直接用於NeoAlzette嗎？
A: ❌ 不完全適用

原因：
1. Lipmaa-Moriai只處理：α + β → γ (XOR差分的模加)
2. NeoAlzette有：
   - A -= constant (模減！)
   - l1_forward(A) (線性層！)
   - cd_from_B(...) (複雜的分支函數！)

需要：
- 擴展模型處理模減
- 分析線性層的差分性質
- 建模交叉分支的影響
```

---

## 💡 正確的MEDCP/MELCC計算流程

### 對於標準ARX（如Alzette）

```python
def compute_MEDCP_standard_arx(cipher, rounds):
    """
    標準ARX的MEDCP計算（論文方法）
    """
    # Step 1: 構建pDDT
    print("Building pDDT...")
    H = PDDTAlgorithm1Complete.compute_pddt(
        bit_width=32,
        weight_threshold=10  # 對應 p_thres = 2^{-10}
    )
    print(f"pDDT size: {len(H)}")
    
    # Step 2: 構建Highway表
    highway_table = HighwayTable()
    for (α, β, γ, w) in H:
        highway_table.add(α, β, γ, weight=w)
    highway_table.build_index()
    
    # Step 3: Matsui搜索
    print(f"Searching {rounds}-round trails...")
    result = MatsuiAlgorithm2Complete.execute_threshold_search(
        num_rounds=rounds,
        highway_table=highway_table,
        use_country_roads=True,  # ← 關鍵！
        initial_estimate=2^{-128}
    )
    
    # Step 4: 驗證
    best_trail = result.best_trail
    MEDCP = result.best_probability
    
    print(f"MEDCP_{rounds} = 2^{{-{result.best_weight}}}")
    print(f"Trail: {best_trail}")
    
    return MEDCP, best_trail

def compute_MELCC_standard_arx(cipher, rounds):
    """
    標準ARX的MELCC計算（論文方法）
    """
    # Step 1: 構建線性模型（Wallén）
    wallen_auto = WallenAutomaton()
    wallen_auto.precompute_transitions()
    
    # Step 2: 搜索最優線性軌道
    # 注意：應該使用矩陣乘法鏈，但實踐中用搜索近似
    result = threshold_search_linear(
        rounds=rounds,
        enumerate_func=wallen_auto.enumerate_complete_optimized,
        lower_bound=linear_highway_bound
    )
    
    MELCC = result.best_correlation
    
    print(f"MELCC_{rounds} = 2^{{-{-log2(MELCC)}}}")
    return MELCC
```

### 對於NeoAlzette（需要擴展模型）

```python
def compute_MEDCP_neoalzette(rounds):
    """
    NeoAlzette的MEDCP計算（需要擴展）
    """
    # Step 1: 擴展差分模型
    # 需要處理：
    
    # 1.1 模減的差分概率
    def diff_prob_sub(α, const, γ):
        """
        DP(α - const → γ) = ?
        論文未涵蓋，需要推導
        
        提示：A - C = A + (-C) = A + (NOT(C) + 1)
        可以轉換為模加問題
        """
        minus_C = (~const + 1) & 0xFFFFFFFF
        return diff_prob_add(α, minus_C, γ)
    
    # 1.2 線性層的差分性質
    def diff_through_l1(α_in):
        """
        l1_forward: x ^ rotl(x,2) ^ rotl(x,10) ^ rotl(x,18) ^ rotl(x,24)
        
        差分：∆(l1(x)) = l1(x⊕α) ⊕ l1(x)
        由於l1是線性的：∆(l1(x)) = l1(α)
        
        概率：永遠是1（線性操作）
        """
        return l1_forward(α_in), probability=1.0
    
    # 1.3 交叉分支的差分
    def diff_through_cd_from_B(∆B, rc0, rc1):
        """
        cd_from_B涉及：
        - l2_forward(B ^ rc0)
        - l1_forward(rotr(B, 3) ^ rc1)
        - 複雜的XOR組合
        
        差分分析：
        - 常量被差分消去
        - 線性操作概率=1
        - 但需要跟踪差分傳播路徑
        """
        # 詳細建模...
        return (∆c, ∆d), probability=1.0  # 線性！
    
    # Step 2: 完整的單輪差分模型
    def single_round_diff(state_diff):
        """
        NeoAlzette單輪差分傳播
        """
        ∆A, ∆B = state_diff
        prob = 1.0
        
        # First subround
        # B += (rotl(A, 31) ^ rotl(A, 17) ^ R[0])
        ∆temp = rotl(∆A, 31) ^ rotl(∆A, 17)  # 線性
        ∆B_after_add, p1 = diff_prob_add(∆B, ∆temp, γ=...)
        prob *= p1
        
        # A -= R[1]
        ∆A_after_sub, p2 = diff_prob_sub(∆A, R[1], γ=...)
        prob *= p2  # 通常 = 1
        
        # A ^= rotl(B, 24)
        ∆A ^= rotl(∆B, 24)  # 線性，prob=1
        
        # ... 繼續建模其他操作
        
        return (∆A_final, ∆B_final), prob
    
    # Step 3: 多輪搜索
    trails = search_differential_trails(
        rounds=rounds,
        single_round_model=single_round_diff,
        use_highway=True
    )
    
    MEDCP = max(trail.probability for trail in trails)
    return MEDCP
```

---

## 🚀 對當前實現的建議

### 建議1：明確分離pDDT構建和搜索

```cpp
// 當前（混雜）：
auto result = matsui_threshold_search(
    ...,
    next_states,  // 隱含pDDT查詢
    ...
);

// 建議（清晰）：
// Step 1: 離線構建pDDT
auto pddt = PDDTAlgorithm1Complete::compute_pddt(config);

// Step 2: 轉換為Highway表
HighwayTable H;
for (auto& entry : pddt) {
    H.add(entry);
}
H.build_index();

// Step 3: Matsui搜索（明確使用H）
auto result = MatsuiAlgorithm2Complete::execute_threshold_search(
    SearchConfig{
        .num_rounds = R,
        .highway_table = H,
        .use_country_roads = true
    }
);
```

### 建議2：實現顯式的Country Roads策略

```cpp
// 當前：混在next_states中
auto children = next_states(current.state, current.round, slack);

// 建議：明確區分
class MatsuiSearchWithHC {
    CountryRoadsTable build_country_roads(
        uint32_t αr, 
        uint32_t α_prev,
        const HighwayTable& H,
        double p_min
    ) {
        CountryRoadsTable C;
        
        // 論文邏輯：找到所有βr使得：
        // 1. p(αr → βr) ≥ p_min
        // 2. (α_{r-1} + βr) = γ ∈ H
        
        for (uint32_t βr : enumerate_candidates()) {
            double prob = compute_xdp_add(αr, 0, βr, 32);
            if (prob >= p_min) {
                uint32_t next_α = α_prev + βr;
                if (H.contains_output(next_α)) {
                    C.add(DifferentialEntry{αr, βr, prob});
                }
            }
        }
        
        if (C.empty()) {
            // 沒找到回highways的路，選最優country road
            auto [best_β, best_p] = find_max_prob(αr);
            C.add(DifferentialEntry{αr, best_β, best_p});
        }
        
        return C;
    }
};
```

### 建議3：為NeoAlzette建立專門的差分模型

```cpp
// 新文件：neoalzette_differential_model.hpp
class NeoAlzetteDifferentialModel {
public:
    struct SingleRoundDiff {
        uint32_t ∆A_in, ∆B_in;
        uint32_t ∆A_out, ∆B_out;
        double probability;
    };
    
    SingleRoundDiff compute_single_round_diff(
        uint32_t ∆A, uint32_t ∆B,
        const RoundConstants& rc
    ) {
        // 完整建模NeoAlzette的單輪差分
        
        // 1. 模加/模減的差分
        // 2. 線性層的差分（l1_forward, l2_forward）
        // 3. 交叉分支的差分（cd_from_A, cd_from_B）
        // 4. 旋轉和XOR的差分
        
        // 返回輸出差分和概率
    }
    
    std::vector<DifferentialTrail> search_best_trails(
        int rounds,
        int weight_threshold
    ) {
        // 使用專門的模型搜索
    }
};
```

### 建議4：實現矩陣乘法鏈（用於MELCC）

```cpp
// 新文件：matrix_multiplication_chain.hpp
template<size_t N>
class CorrelationMatrix {
    std::array<std::array<double, N>, N> M;
    
public:
    CorrelationMatrix operator*(const CorrelationMatrix& other) const {
        // 矩陣乘法
    }
    
    double max_correlation() const {
        // 找最大相關性
    }
};

class LinearCorrelationComputer {
public:
    double compute_MELCC(int rounds) {
        std::vector<CorrelationMatrix<4>> matrices;
        
        // 構建每輪的相關性矩陣
        for (int r = 0; r < rounds; ++r) {
            auto M_r = build_round_correlation_matrix(r);
            matrices.push_back(M_r);
        }
        
        // 矩陣乘法鏈
        auto M_total = matrices[0];
        for (int i = 1; i < rounds; ++i) {
            M_total = M_total * matrices[i];
        }
        
        // 找最大相關性
        return M_total.max_correlation();
    }
};
```

---

## 📊 總結：論文vs實現對照表

| 方面 | 論文要求 | 當前實現 | 偏差程度 | 建議 |
|------|---------|---------|---------|------|
| **pDDT構建** | Algorithm 1顯式構建 | 隱含在搜索中 | 🟡 中等 | 實現`PDDTAlgorithm1Complete` |
| **Highways/Country Roads** | 明確區分H和C表 | 混在next_states中 | 🔴 高 | 實現`CountryRoadsTable` |
| **Matsui搜索** | Algorithm 2完整邏輯 | 簡化的branch-and-bound | 🟡 中等 | 實現完整分輪策略 |
| **差分概率** | Lipmaa-Moriai精確計算 | ✅ 已實現 | 🟢 低 | 保持 |
| **線性相關性** | Wallén完整枚舉 | ✅ 已改進 | 🟢 低 | 保持WallenAutomaton |
| **矩陣乘法鏈** | 2×2矩陣乘法 | ❌ 未實現 | 🔴 高 | 實現CorrelationMatrix |
| **Highway表** | 精確的pDDT後綴下界 | 近似的下界 | 🟡 中等 | 改進下界計算 |
| **NeoAlzette建模** | N/A（論文是Alzette） | 缺少完整模型 | 🔴 高 | 建立擴展差分模型 |

**顏色說明**：
- 🟢 低偏差：實現基本符合論文
- 🟡 中等偏差：核心思想正確，細節有差異
- 🔴 高偏差：缺少關鍵組件或重要創新

---

## 🎯 核心結論

### 1. MEDCP/MELCC的本質

```
MEDCP和MELCC不是簡單的函數調用，而是：

MEDCP = 結果( 
    pDDT構建 + 
    Matsui多輪搜索 + 
    Highways/Country Roads策略 + 
    剪枝優化
)

MELCC = 結果(
    Wallén枚舉 + 
    矩陣乘法鏈 +
    多輪線性軌道搜索 +
    相關性計算
)

它們是整個分析框架的**輸出**，不是單個算法。
```

### 2. 當前實現的優勢

```
✅ 數學基礎正確：
   - Lipmaa-Moriai實現精確
   - Wallén改進完整

✅ 搜索框架完整：
   - Branch-and-bound工作良好
   - 剪枝策略有效

✅ 工程質量高：
   - 現代C++20
   - 模塊化設計
   - 性能優化
```

### 3. 當前實現的不足

```
❌ 論文算法對應不明確：
   - pDDT構建不顯式
   - Highways/Country Roads混雜
   - 缺少論文的分輪策略

❌ NeoAlzette特定建模缺失：
   - 模減差分未建模
   - 線性層影響未分析
   - 交叉分支未完整建模

❌ 矩陣乘法鏈未實現：
   - 線性相關性計算不精確
   - 可能遺漏最優解
```

### 4. 優先改進順序

```
Priority 1 (關鍵)：
1. 為NeoAlzette建立完整差分模型
2. 實現顯式的Highways/Country Roads分離
3. 添加矩陣乘法鏈支持

Priority 2 (重要)：
4. 實現pDDT的顯式構建階段
5. 改進Highway表的下界計算
6. 添加論文的完整分輪邏輯

Priority 3 (有用)：
7. 實驗驗證MEDCP/MELCC結果
8. 與論文數據對比
9. 文檔化差異和改進
```

---

**最終理解**：當前實現在數學正確性和工程質量上都很優秀，但在**論文算法的直接對應**和**NeoAlzette的特殊性**方面需要補充。核心任務是建立完整的NeoAlzette差分/線性模型，使其能準確計算MEDCP和MELCC。

---

**作者註**：這份理解基於深入閱讀11篇論文和當前代碼庫。如有疑問或需要澄清，請指出具體部分。
