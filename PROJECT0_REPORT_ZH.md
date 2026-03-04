# Project #0 完成情况说明（当前进度）

日期：2026-03-04

## 1. 本次完成内容
本次已完成 `CountMinSketch` 的核心功能实现，涉及以下文件：
- `src/include/primer/count_min_sketch.h`
- `src/primer/count_min_sketch.cpp`

并补充了项目总结与执行计划文档：
- `PROJECT0_PLAN_ZH.md`
- `PROJECT0_REPORT_ZH.md`（本文件）

## 2. 已实现功能
`CountMinSketch<KeyType>` 已实现如下能力：

1. 构造函数参数校验
- `width == 0` 或 `depth == 0` 时抛出 `std::invalid_argument`。
- 初始化 `depth` 个带种子的哈希函数。
- 初始化 sketch 计数矩阵。

2. 并发安全插入 `Insert`
- 使用 `std::atomic<uint32_t>` 作为计数存储。
- 对每一行对应桶执行原子 `fetch_add(1)`。
- 支持并发线程无全局串行锁写入（lock-free counter update）。

3. 计数查询 `Count`
- 对所有行计算桶位置并读取计数。
- 返回各行计数的最小值（CMS 标准估计方式）。

4. 合并 `Merge`
- 维度不一致时抛异常。
- 维度一致时逐桶累加。

5. 清空 `Clear`
- 将 sketch 全部计数清零。
- 按接口注释清空内部 top-k 相关状态（item set / top-k 容器 / 初始 k 状态）。

6. TopK 查询 `TopK`
- 支持候选集计数统计并按频次降序返回。
- 支持 “`k` capped by initial k” 语义：首次调用记录 `initial_k`，后续调用对 `k` 取 `min(k, initial_k)`。
- 维护内部 top-k 相关结构状态，和注释语义保持一致。

7. move 语义
- 实现 move 构造和 move 赋值。
- 迁移内部状态后，重建哈希函数闭包，保证 `this` 绑定正确。

## 3. 关键实现点
1. 计数矩阵采用一维扁平化布局：`index = row * width + col`。
2. 并发路径用原子操作保证正确性和性能。
3. `TopK` 结果使用稳定降序排序，确保频次相同情况下顺序行为稳定。
4. `Merge`/`Clear`/move 语义都同步维护内部 top-k 状态一致性。

## 4. 已完成测试
已在本地执行：

```bash
ASAN_OPTIONS=detect_leaks=0 ./build/test/count_min_sketch_test
```

结果：
- 13 / 13 全通过。
- 包含并发性能测试 `ContentionRatioTest`，本次结果 speedup ≈ `1.779`（阈值 > 1.2）。

## 5. 备注
- 本次提交同步的是当前仓库进度（以 `CountMinSketch` 实现为主）。
- 该文档用于记录当前阶段实现状态，便于后续继续迭代与回归验证。
