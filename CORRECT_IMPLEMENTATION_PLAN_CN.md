# 正確實現計劃：Bit-Vector精確方法

## 📋 現狀

當前`compute_diff_weight_addconst()`被我錯誤地簡化為：
```cpp
return compute_diff_weight_add(delta_x, 0, delta_y);  // ❌ 錯誤！
```

**這會導致50%的差分判斷錯誤！**

## ✅ 需要實現的正確方法

### Theorem 2 (Machado, 2015)

論文第493-562行的精確公式：

```
Pr[u →^a v] = ϕ₀ × ϕ₁ × ... × ϕₙ₋₁

其中：
Sᵢ = (u[i-1], v[i-1], u[i]⊕v[i])  // 3位狀態

ϕᵢ = {
    1,              Sᵢ = 000
    0,              Sᵢ = 001 (不可行)
    1/2,            Sᵢ ∈ {010, 011, 100, 101}
    (計算公式),      Sᵢ = 110
    (計算公式),      Sᵢ = 111
}

δᵢ = {
    (a[i-1] + δᵢ₋₁)/2,  Sᵢ = 000
    0,                  Sᵢ = 001
    a[i-1],             Sᵢ ∈ {010, 100, 110}
    δᵢ₋₁,               Sᵢ ∈ {011, 101}
    1/2,                Sᵢ = 111
}
```

### 實現策略

#### 方法1：直接實現Theorem 2（精確，但需要浮點）

```cpp
static double compute_diff_prob_addconst_exact(
    uint32_t u,      // 輸入差分
    uint32_t v,      // 輸出差分
    uint32_t a       // 常量（必須使用！）
) {
    double delta = 0.0;
    double prob = 1.0;
    
    for (int i = 0; i < 32; ++i) {
        int u_prev = (i > 0) ? ((u >> (i-1)) & 1) : 0;
        int v_prev = (i > 0) ? ((v >> (i-1)) & 1) : 0;
        int u_i = (u >> i) & 1;
        int v_i = (v >> i) & 1;
        int a_i = (a >> i) & 1;
        
        // S_i = (u[i-1], v[i-1], u[i]⊕v[i])
        int state = (u_prev << 2) | (v_prev << 1) | (u_i ^ v_i);
        
        double phi_i = 1.0;
        double delta_next = 0.0;
        
        switch (state) {
            case 0b000:  // 000
                phi_i = 1.0;
                delta_next = (a_i + delta) / 2.0;
                break;
            case 0b001:  // 001
                return 0.0;  // 不可行
            case 0b010:  // 010
            case 0b011:  // 011
            case 0b100:  // 100
            case 0b101:  // 101
                phi_i = 0.5;
                if (state == 0b011 || state == 0b101) {
                    delta_next = delta;
                } else {
                    delta_next = a_i;
                }
                break;
            case 0b110:  // 110
                phi_i = 1.0 - (a_i + delta - 2.0 * a_i * delta);
                delta_next = a_i;
                break;
            case 0b111:  // 111
                phi_i = a_i + delta - 2.0 * a_i * delta;
                delta_next = 0.5;
                break;
        }
        
        prob *= phi_i;
        if (prob == 0.0) return 0.0;
        delta = delta_next;
    }
    
    return prob;
}

static int compute_diff_weight_addconst(
    uint32_t delta_x,
    uint32_t constant,
    uint32_t delta_y
) noexcept {
    double prob = compute_diff_prob_addconst_exact(delta_x, delta_y, constant);
    if (prob == 0.0) return -1;
    return static_cast<int>(std::ceil(-std::log2(prob)));
}
```

#### 方法2：Algorithm 1的Bit-Vector實現（論文推薦，但複雜）

論文Algorithm 1使用位向量操作，O(log²n)複雜度，避免浮點。

**優點**：
- 適合SMT求解器
- 位向量操作

**缺點**：
- 非常複雜
- 需要實現LZ, Rev, HW, ParallelLog等輔助函數

**建議**：先實現方法1（精確且可驗證），如果性能不夠再考慮方法2。

---

## 🧪 測試驗證

需要驗證實現的正確性：

```cpp
// 測試用例1：論文Example 1
uint32_t u = 0b1010001110;  // 10位
uint32_t v = 0b1010001010;
uint32_t a = 0b1000101110;

double prob = compute_diff_prob_addconst_exact(u, v, a);
// 期望：prob = 5/16 ≈ 0.3125
// 期望：weight ≈ 1.678

// 測試用例2：與LM方法對比
uint32_t delta_x = 0x12345678;
uint32_t constant = 0xABCDEF00;
uint32_t delta_y = 0x87654321;

int w_correct = compute_diff_weight_addconst(delta_x, constant, delta_y);
int w_wrong = compute_diff_weight_add(delta_x, 0, delta_y);

// 應該看到明顯差異！
```

---

## ⚠️ 重要注意事項

1. **絕對不能**使用`compute_diff_weight_add(Δx, 0, Δy)`
2. **必須**使用常量的實際值`a`
3. **必須**逐位追踪進位狀態`δᵢ`
4. **必須**處理所有9種狀態組合

---

## 📊 性能對比

| 方法 | 複雜度 | 精確度 | 適用場景 |
|------|--------|--------|---------|
| LM簡化 | O(1) | 50%錯誤 | ❌ 不適用NeoAlzette |
| Theorem 2 | O(n) | 100%精確 | ✅ 適用 |
| Algorithm 1 | O(log²n) | 100%精確 | ✅ 適用（SMT） |

對於32位，O(n)=O(32)完全可接受。

---

*需要立即重新實現！*
