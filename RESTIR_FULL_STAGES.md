# ReSTIR 全面阶段规划（DI + GI）

本文档综合 Path Tracing 现状、ReSTIR DI / ReSTIR GI 的渐进实现策略，以及直接光/间接光分离的 buffer 设计，给出**最终全面的阶段规划**。实现顺序：**先完成直接光与间接光的 buffer 分离 → 先做 ReSTIR DI → 再做 ReSTIR GI**；直接光与间接光在整条管线中始终作为**独立 image** 存在。

---

## 一、术语与概念

| 术语 | 含义 |
|------|------|
| **RIS** | Resampled Importance Sampling（重采样重要性采样）：用目标权重从多个候选样本中重采样出一个，并构造无偏估计。 |
| **WRS** | Weighted Reservoir Sampling：用 reservoir 维护「当前选中样本、\(W = \sum w\)、\(M\)」，按权重更新选中样本的采样方法，是 ReSTIR 的核心。 |
| **ReSTIR DI** | 对**直接光照**（一次对光源的采样 + 可见性）做 reservoir 时域/空域复用。 |
| **ReSTIR GI** | 对**间接光照**（多 bounce 或至少一次反弹的路径贡献）做 reservoir 时域/空域复用。 |
| **Target weight** | RIS/WRS 中的目标权重，通常与理想 PDF 成比例；合并时用「当前像素的 target」再评估历史/邻居样本的权重。 |

---

## 二、Buffer 与管线约定

在整条管线中，**直接光与间接光始终作为独立 image**，最后再合成。

| Buffer 名称 | 用途 | 写入者 | 格式建议 |
|-------------|------|--------|----------|
| **direct_radiance_image** | 仅直接光 radiance（NEE 太阳等） | Path trace / ReSTIR DI pass | rgba32f |
| **indirect_radiance_image** | 仅间接光 radiance（bounce 贡献） | Path trace / ReSTIR GI pass | rgba32f |
| **output_image** | 最终显示 | Composite pass：direct + indirect → tone map → 写入 | rgba8 |

- ReSTIR DI 只读写 **direct_radiance_image** 及 DI 的 reservoir。
- ReSTIR GI 只读写 **indirect_radiance_image** 及 GI 的 reservoir。
- 合成阶段：`radiance = direct_radiance + indirect_radiance`，再 tone mapping / gamma 写入 **output_image**。

调试时可单独显示 `direct_radiance_image` 或 `indirect_radiance_image`。

---

## 三、Path Tracing 现状（简要）

- **入口**：每像素 1 条 primary ray → G-buffer → `pathTraceFromHit()`，每帧 1 条路径，`pc.iteration` 做渐进。
- **直接光**：NEE 对太阳圆盘 + 阴影射线，MIS（balance heuristic）。
- **间接光**：BSDF 余弦半球采样，Lambert，Russian Roulette，`MAX_DEPTH = 3`。
- **可复用**：G-buffer 与 `first_hit`（position, normal, albedo）可直接作为 ReSTIR 的 shading point。

---

## 四、总阶段一览

整体分为 **3 大块**，顺序固定：

1. **阶段 0–1**：基线 + Buffer 分离（直接/间接独立 image，合成到 output）。
2. **阶段 DI-0 ~ DI-5**：ReSTIR DI（只改直接光，写 direct_radiance_image）。
3. **阶段 GI-0 ~ GI-5**：ReSTIR GI（只改间接光，写 indirect_radiance_image）。
4. **阶段 F**：全管线整合与调参。

下表为总览，下文逐阶段展开。

| 阶段 | 名称 | 直接光 | 间接光 | 效果衡量 |
|------|------|--------|--------|----------|
| 0 | 基线 | NEE，写合在一起 | 同上 | 参考与方差基线 |
| 1 | Buffer 分离 | 写 direct_radiance_image | 写 indirect_radiance_image，合成→output | 直接/间接正确性，合=原图 |
| DI-0 | DI 基线 | 同阶段 1 | 同阶段 1 | 与阶段 1 一致 |
| DI-1 | DI 单帧 RIS | M 次光源采样 + WRS，无持久化 | 不变 | 无偏，方差对比 |
| DI-2 | DI Reservoir | 持久化 DI reservoir，本帧用 | 不变 | 同 DI-1，结构就绪 |
| DI-3 | DI 时域 | 重投影 + 合并上一帧 DI reservoir | 不变 | 静止收敛、运动鬼影 |
| DI-4 | DI 空域 | 邻域 DI reservoir merge | 不变 | 更平滑 |
| DI-5 | DI 调参 | 参数与可见性调优 | 不变 | 质量/噪声 |
| GI-0 | GI 基线 | 用 DI 输出 | 同阶段 1 间接 | 与 DI-5 一致 |
| GI-1 | GI 单帧 RIS | 不变 | M 条间接路径 + WRS | 无偏，方差对比 |
| GI-2 | GI Reservoir | 不变 | 持久化 GI reservoir | 同 GI-1 |
| GI-3 | GI 时域 | 不变 | 重投影 + 合并上一帧 GI reservoir | 静止收敛、鬼影 |
| GI-4 | GI 空域 | 不变 | 邻域 GI reservoir merge | 更平滑 |
| GI-5 | GI 调参 | 不变 | 参数与可见性调优 | 质量/PSNR |
| F | 全管线 | DI + GI 联合调参 | 同上 | 最终质量 |

---

## 五、阶段 0：基线

- **内容**：保持现有 path tracing 不变，输出为「直接+间接」合在一张图（当前 `output_image`）。
- **效果衡量**：作为参考画面与方差基线（固定视角多帧 mean/variance 或主观噪声）。

---

## 六、阶段 1：Buffer 分离（直接光 / 间接光独立 image）

**目标**：在不大改算法的前提下，把直接光和间接光拆成两路，分别写入 `direct_radiance_image` 和 `indirect_radiance_image`，再合成到 `output_image`。

**实现要点**：

- 在 path 循环（如 `pathTraceFromHit`）内按贡献拆分：
  - **直接**：当前 hit 对太阳的 NEE 贡献，以及若 BSDF 采样恰好击中太阳时的直接贡献（不乘之前 bounce 的 throughput）。
  - **间接**：其余所有贡献（第一次 bounce 之后的 throughput × 后续 NEE/BSDF 等）。
- 输出：
  - 每像素累加 `direct_radiance` → 写入 **direct_radiance_image**。
  - 每像素累加 `indirect_radiance` → 写入 **indirect_radiance_image**。
  - 合成：`radiance = direct_radiance + indirect_radiance`，再 tone map / gamma 写入 **output_image**（可单独 composite pass 或在同一 pass 末尾做）。
- C++ 侧：创建并绑定 `direct_radiance_image`、`indirect_radiance_image`（与现有 G-buffer、output 同分辨率，格式见上表）。

**效果衡量**：

- 单独显示 direct / indirect，确认直接光无颜色渗色、间接光有合理反弹（如 Cornell 盒）。
- `output_image` 与阶段 0 在数值或视觉上一致。

---

## 七、ReSTIR DI 阶段（DI-0 ~ DI-5）

只改**直接光**的生成方式，始终写 **direct_radiance_image**；间接光保持阶段 1 的 path 结果，写 **indirect_radiance_image**。

### DI-0：DI 基线

- 与阶段 1 完全一致：直接光仍为当前 NEE（每像素 1 次对太阳采样），写 direct_radiance_image。
- 作为 ReSTIR DI 的对比基线。

### DI-1：DI 单帧 RIS（无复用）

- **候选**：在当前像素 primary hit 处，对太阳采样 **M 次**（如 M=4），每次一条 shadow ray，得到 M 个 \((L_i, w_i)\)，其中 \(w_i\) 为 target weight（如 brdf×cos/pdf 或 1/pdf）。
- **WRS**：用 M 个候选做一次 weighted reservoir sampling，选出一个样本，得到无偏估计（如 \(L_{\mathrm{sel}} \cdot W / (M \cdot w_{\mathrm{sel}})\)）。
- 不保存 reservoir 到下一帧。
- **输出**：上述估计写入 direct_radiance_image。

**效果衡量**：期望与 DI-0 一致（无偏），可对比方差。

### DI-2：DI Reservoir 持久化（单帧使用）

- **Reservoir 内容**（每像素）：选中的光源样本信息（足够再算一次贡献，如方向、选中的 \(L\)）、\(W\)、\(M\)、\(w_{\mathrm{sel}}\)。
- 每帧：生成 M 个候选 → WRS 更新 reservoir → 用 reservoir 得到无偏估计写 direct_radiance_image。
- 仍不读上一帧 reservoir，仅为后续时域/空域准备数据布局（如 DI reservoir 的 buffer/image）。

**效果衡量**：画面与 DI-1 一致。

### DI-3：DI 时域复用

- **重投影**：用当前 G-buffer world position（或加简单位移）在上一帧 screen 上找到 reprojected 像素，读取其 DI reservoir。
- **有效性**：若 reprojected 的 world position / 法线与当前像素一致（阈值内），保留；否则丢弃上一帧 reservoir（或 M=0）。
- **合并**：将上一帧选中的样本视为额外候选，用**当前帧的 target** 再算 weight，与当前 M 个候选一起做 WRS，更新 reservoir。
- 双缓冲：每帧读 prev reservoir、写 curr reservoir，下一帧交换。

**效果衡量**：相机静止时直接光更快收敛；运动时观察鬼影，调可见性阈值。

### DI-4：DI 空域复用

- 当前像素读取若干邻域（如 3×3/5×5）的 DI reservoir；对每个邻居的选中样本用**当前像素的 target** 再算 weight，与当前 reservoir 做 WRS merge。
- **输出**：合并后的无偏估计写 direct_radiance_image。

**效果衡量**：同帧下比 DI-3 更平滑，可调半径与采样策略。

### DI-5：DI 调参

- 固定 M、邻域大小、可见性条件、历史 blend 等，做质量与噪声的平衡。
- 效果衡量：与 NEE 基线（DI-0）比噪声/PSNR。

---

## 八、ReSTIR GI 阶段（GI-0 ~ GI-5）

直接光已由 ReSTIR DI 输出，写 **direct_radiance_image**；以下只改**间接光**，写 **indirect_radiance_image**。

### GI-0：GI 基线

- 直接光使用 DI-5 的输出（或 DI-4）；间接光仍为阶段 1 的 path 结果（每像素 1 条间接路径），写 indirect_radiance_image。
- 作为 ReSTIR GI 的对比基线。

### GI-1：GI 单帧 RIS（无复用）

- **候选**：在 primary hit 处对「间接光」采样 **M 次**（如 M=4）：每次从该点发射一条间接路径（如 1 bounce：cosine 采样方向 → 命中点 → 该点 NEE + 天空），得到 \(L_i\) 与 target weight \(w_i\)。
- **WRS**：用 M 个候选做一次 WRS，得到无偏估计（如 \(L_{\mathrm{sel}} \cdot W / (M \cdot w_{\mathrm{sel}})\)），写入 indirect_radiance_image。
- 不保存 reservoir。

**效果衡量**：期望与 GI-0 一致（无偏），对比间接光方差。

### GI-2：GI Reservoir 持久化（单帧使用）

- **Reservoir 内容**：选中样本的状态（足以再算贡献：如出射方向、下一跳位置/法线/albedo，或直接存 \(L_{\mathrm{sel}}\)）、\(W\)、\(M\)、\(w_{\mathrm{sel}}\)。
- 每帧：M 个候选 → WRS 更新 reservoir → 用 reservoir 得到无偏估计写 indirect_radiance_image。
- 不读上一帧，仅为时域/空域准备 GI reservoir 的 buffer/image。

**效果衡量**：画面与 GI-1 一致。

### GI-3：GI 时域复用

- 用当前 G-buffer world position 重投影到上一帧，读取对应像素的 GI reservoir；做有效性判断（position/法线阈值）。
- 将上一帧选中样本用**当前帧 target** 再评估 weight，与当前 M 个候选一起 WRS 合并；双缓冲 GI reservoir。
- **输出**：合并后的无偏估计写 indirect_radiance_image。

**效果衡量**：静止时间接光更快收敛；运动时观察鬼影。

### GI-4：GI 空域复用

- 当前像素读取邻域 GI reservoir，对邻居选中样本用**当前像素 target** 再算 weight，做 WRS merge，写 indirect_radiance_image。

**效果衡量**：更平滑、收敛更快，可调半径与拒绝条件。

### GI-5：GI 调参

- 固定 M、邻域、可见性、多 bounce 策略（若扩展）等，做质量与性能平衡。
- 效果衡量：与离线多 spp 或 GI-0 比 PSNR/主观质量。

---

## 九、阶段 F：全管线整合与调参

- **内容**：直接光 = ReSTIR DI 最终版（写 direct_radiance_image）；间接光 = ReSTIR GI 最终版（写 indirect_radiance_image）；合成 = direct + indirect → tone map → output_image。
- **可选**：多 bounce 候选、target 设计、分辨率/降噪等。
- **效果衡量**：与参考图或各阶段并排对比，固定时间预算下的 PSNR 与主观质量。

---

## 十、实现与分支建议

1. **宏/配置切换**：用 `#define RESTIR_PHASE 0|1`、`RESTIR_DI_STAGE 0..5`、`RESTIR_GI_STAGE 0..5`（或等效配置）在 shader 与 C++ 中切换阶段，便于对比与回退。
2. **资源**：阶段 1 起需 direct_radiance_image、indirect_radiance_image；DI-2 起需 DI reservoir buffer(s)；GI-2 起需 GI reservoir buffer(s)；时域阶段需双缓冲与（可选）motion/position 重投影。
3. **调试**：始终支持单独显示 direct_radiance_image、indirect_radiance_image，便于验证各阶段效果。

---

## 十一、阶段依赖简图

```
阶段 0（基线）
    ↓
阶段 1（Buffer 分离：direct / indirect 独立 image，合成→output）
    ↓
DI-0 → DI-1 → DI-2 → DI-3 → DI-4 → DI-5（ReSTIR DI，只写 direct_radiance_image）
    ↓
GI-0 → GI-1 → GI-2 → GI-3 → GI-4 → GI-5（ReSTIR GI，只写 indirect_radiance_image）
    ↓
阶段 F（全管线整合与调参）
```

---

## 十二、小结

- **直接光与间接光在整条管线中均作为独立 image**（direct_radiance_image、indirect_radiance_image），最后再合成到 output_image。
- **先做 ReSTIR DI（DI-0~DI-5），再做 ReSTIR GI（GI-0~GI-5）**，每块内从单帧 RIS → reservoir 持久化 → 时域 → 空域 → 调参，阶段简单、可单独衡量效果。
- 本文档为最终全面阶段规划，可与现有 `path_tracing.comp` 及 C++ 绑定配合，按上表顺序逐步实现。
