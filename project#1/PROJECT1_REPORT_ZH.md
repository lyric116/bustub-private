# BusTub Project 1 完成报告（中文）

日期：2026-03-06

## 1. 项目结论

本次已完成 Project 1 的核心实现与收敛修复，覆盖：

1. Task #1 `ArcReplacer`
2. Task #2 `DiskScheduler`
3. Task #3 `BufferPoolManager + PageGuard`
4. Task #4 Leaderboard 方向的小幅优化（扫描访问优化）

当前版本已通过你提供环境中的全部测试（包括你后续反馈的 `FlushPageTest` 问题修复后回归）。

## 2. 完成内容总览

## 2.1 Task #1: ArcReplacer

已实现接口：
- `ArcReplacer::ArcReplacer`
- `ArcReplacer::Evict`
- `ArcReplacer::RecordAccess`
- `ArcReplacer::SetEvictable`
- `ArcReplacer::Remove`
- `ArcReplacer::Size`

实现要点：
- 使用四个 ARC 列表：`mru_ / mfu_ / mru_ghost_ / mfu_ghost_`。
- 维护 alive/ghost 元数据映射和迭代器，保证 O(1) 链表移动与删除。
- `curr_size_` 严格表示“可淘汰帧数量”。
- `RecordAccess` 覆盖四类命中场景（alive hit / ghost hit / miss）。
- `Evict` 支持：
  - 依据 `mru_target_size_` 决策主淘汰侧。
  - 目标侧全 pinned 时自动尝试另一侧。
- `Remove` 不产生 ghost 记录，符合题意。

## 2.2 Task #2: DiskScheduler

已实现接口：
- `DiskScheduler::DiskScheduler`
- `DiskScheduler::Schedule`
- `DiskScheduler::StartWorkerThread`

实现要点：
- 构造函数创建后台线程。
- `Schedule` 把请求移动入 `request_queue_`。
- worker 线程循环消费请求，收到 `std::nullopt` 退出。
- 每个请求执行读/写后回调 `promise`；异常分支返回失败，避免 future 挂死。

## 2.3 Task #3: BufferPoolManager + PageGuard

### A. PageGuard

已实现：
- `ReadPageGuard` / `WritePageGuard` 构造、移动构造、移动赋值、`Drop`、`Flush`。

实现要点：
- RAII 语义：构造时拿页锁，析构自动 `Drop`。
- `Drop` 幂等，且顺序安全（先释放页锁，再修改 pin/replacer 元数据）。
- pin-count 与 evictable 状态联动：pin 从 1 -> 0 时设为可淘汰。
- `WritePageGuard::GetDataMut()` 每次访问都置脏，确保“同一 guard 多次修改 + 多次 flush”语义正确。

### B. BufferPoolManager

已实现：
- `NewPage`
- `DeletePage`
- `CheckedWritePage`
- `CheckedReadPage`
- `FlushPageUnsafe`
- `FlushPage`
- `FlushAllPagesUnsafe`
- `FlushAllPages`
- `GetPinCount`

实现要点：
- 新页分配使用原子 `next_page_id_`。
- `CheckedRead/Write` 实现三类路径：
  1. 页面已在内存（直接命中）
  2. 有空闲 frame
  3. 需要 replacer 淘汰 frame
- 维护页表映射、pin 计数、evictable 状态与磁盘 I/O 的一致性。
- 新增 frame 元数据 `page_id_`（`FrameHeader` 内）：
  - 淘汰时直接确定旧页，避免遍历 `page_table_` 反查。
  - 修复隐藏场景下可能出现的“刷盘页号错位”问题。
- flush 系列函数支持单页/全量刷盘，安全版带页锁。

## 2.4 Task #4: Leaderboard 方向优化

已做优化（不影响正确性测试）：
- 在 ARC 中引入 `AccessType::Scan` 感知策略，降低扫描访问对 MFU 的污染。
- 目标：减少大范围顺序扫描冲掉热点页的概率，提升混合负载 QPS 表现。

## 3. 关键问题与修复记录

## 3.1 格式与静态检查
- 修复 `check-format` 报错（缩进/单行格式）。
- 修复 `check-clang-tidy-p1` 报错（参数 const 性提示）。

## 3.2 隐藏用例 `FlushPageTest` 失败修复

现象：
- 期望 `str2/str3/str4`，实际反复读到 `str1`。

根因与修复：
1. frame 缺少稳定页号元数据，淘汰/刷盘场景反查风险较高：
- 修复：在 `FrameHeader` 中增加 `page_id_`，并在 `Reset/装载/淘汰` 路径维护。

2. 同一 `WritePageGuard` 多次写入时脏标可能被提前清空后未恢复：
- 修复：`WritePageGuard::GetDataMut()` 每次调用都置脏。

该问题修复后，`FlushPageTest` 场景已通过。

## 4. 代码改动文件清单

1. `src/include/buffer/arc_replacer.h`
2. `src/buffer/arc_replacer.cpp`
3. `src/include/storage/disk/disk_scheduler.h`（接口沿用，逻辑实现在 cpp）
4. `src/storage/disk/disk_scheduler.cpp`
5. `src/include/buffer/buffer_pool_manager.h`
6. `src/buffer/buffer_pool_manager.cpp`
7. `src/include/storage/page/page_guard.h`
8. `src/storage/page/page_guard.cpp`

## 5. 关键提交记录（按时间）

1. `ebbcf53` `p1: implement ARC replacer`
2. `90da1e7` `p1: implement disk scheduler worker and queueing`
3. `3aed4ab` `p1: implement page guards and buffer pool manager core`
4. `c69ffb1` `p1: add scan-aware ARC behavior for leaderboard`
5. `97952ce` `p1: satisfy clang-tidy for disk io helper signatures`
6. `b1e226c` `p1: fix clang-format violations`
7. `b4625a8` `p1: track frame page id to fix eviction and flush mapping`
8. `7a96628` `p1: mark page dirty on every WritePageGuard mutable access`

## 6. 本地验证清单

已执行并通过：
- `check-format`
- `check-clang-tidy-p1`
- `arc_replacer_test --gtest_also_run_disabled_tests`
- `disk_scheduler_test --gtest_also_run_disabled_tests`
- `page_guard_test --gtest_also_run_disabled_tests`
- `buffer_pool_manager_test --gtest_also_run_disabled_tests`
- `bustub-bpm-bench` 烟测（Task4 方向验证）

## 7. 总结

本次 P1 工作已从“可编译/基础通过”推进到“隐藏问题修复 + 规范化（format/tidy）+ Leaderboard 方向优化”。

最终版本具备：
- 正确性：通过项目测试。
- 并发安全：符合页级锁 + BPM 元数据锁的协作约束。
- 可维护性：关键路径（淘汰/刷盘/脏标）元数据更加明确。
- 可扩展性：已预留 AccessType 感知策略的优化入口。
