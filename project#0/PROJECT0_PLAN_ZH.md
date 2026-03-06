# BusTub Project 0（Fall 2025）中文总结与执行计划

原始题目：<https://15445.courses.cs.cmu.edu/fall2025/project0/>

## 1. 项目目标
- 熟悉 C++17 开发环境、编译与测试流程。
- 理解并实现 **Count-Min Sketch (CMS)** 频率估计结构。
- 在并发场景下实现高性能插入（题目明确要求 lock-free insertion）。

## 2. 评分与范围（按题面）
- 主要评分项：
  - `count_min_sketch_test.cpp`（约 60 分）
  - SQLLogicTests（`p0.*.slt`，约 20 分）
  - 代码风格（约 20 分）
- Project 0 这部分代码核心在：
  - `src/include/primer/count_min_sketch.h`
  - `src/primer/count_min_sketch.cpp`

## 3. 你需要完成的功能
`CountMinSketch<KeyType>` 中以下接口（含 TODO）需要正确实现：
- 构造函数：检查 `width/depth > 0`，初始化 sketch 存储。
- move 构造/赋值：迁移状态并保证对象可继续使用。
- `Insert(item)`：对每一行对应桶做 `+1`，并发安全。
- `Count(item)`：返回各行计数中的最小值。
- `Merge(other)`：同维度 sketch 逐桶相加；维度不一致抛异常。
- `Clear()`：清空 sketch。
- `TopK(k, candidates)`：对候选集按估计频率降序输出前 `k`。

## 4. 实现要点
- CMS 是 `depth x width` 的计数矩阵，每行一个独立哈希函数。
- 并发要求重点在 `Insert`：
  - 不要用全局互斥锁串行化所有插入。
  - 推荐以原子计数器做无锁增量（`fetch_add`）。
- move 语义要注意哈希函数闭包对 `this` 的绑定，移动后应重建哈希函数。
- 按题面注释扩展：类内部维护 item set 与 top-k min-heap，`Clear` 需要一并清空；`TopK(k, ...)` 要按初始 `k` 做上限截断。

## 5. 分步执行计划
1. 阅读头文件与测试，列出所有语义要求（已完成）。
2. 在 `count_min_sketch.h/.cpp` 增加并发安全计数存储并实现全部 TODO。
3. 构建并运行 `count_min_sketch_test`，修复编译/逻辑问题直到全通过。
4. 运行 `p0.*.slt`，确认 Project 0 相关 SQL 测试通过。
5. 汇总修改点与可复现命令，便于你后续提交。

## 6. 本地测试与运行命令
如果尚未配置构建目录：

```bash
mkdir -p build
cd build
cmake ..
```

构建并运行 CMS 单测：

```bash
cd /home/lyricx/bustub-private/build
make -j$(nproc) count_min_sketch_test
./test/count_min_sketch_test
```

运行 Project 0 的 SQLLogicTests（任选其一）：

```bash
cd /home/lyricx/bustub-private/build
ctest --verbose -R "SQLLogicTest\.p0\."
```

或逐个运行：

```bash
cd /home/lyricx/bustub-private/build
make p0.01-lower-upper_test p0.02-function-error_test p0.03-string-scan_test
./bin/bustub-sqllogictest ../test/sql/p0.01-lower-upper.slt --verbose -d --in-memory
./bin/bustub-sqllogictest ../test/sql/p0.02-function-error.slt --verbose -d --in-memory
./bin/bustub-sqllogictest ../test/sql/p0.03-string-scan.slt --verbose -d --in-memory
```

## 7. 通过标准（实操验收）
- `count_min_sketch_test` 全通过。
- `SQLLogicTest.p0.*` 全通过。
- 无新增编译告警/风格问题（至少不引入明显 clang-tidy 风险写法）。

## 8. 当前仓库注意事项（实测）
- 在本仓库当前状态下，`SQLLogicTest.p0.*` 会触发 `TODO(P1): Add implementation.` 异常并提前退出。
- 这属于仓库中已有的 P1 未实现模块（非 `CountMinSketch` 代码路径）导致，不是本次 CMS 实现错误。
