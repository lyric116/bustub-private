# BusTub Project 1（Fall 2025）中文题解导引与实现流程

原始题面：<https://15445.courses.cs.cmu.edu/fall2025/project1/>

## 1. 这个 Project 需要做什么

Project 1 的核心是实现一个并发安全的 Buffer Pool 子系统，包含 4 个任务：

1. `Task #1 Arc Replacer`（25 分）
- 在内存页替换策略中实现 ARC（四个列表：`mru_ / mfu_ / mru_ghost_ / mfu_ghost_`）。

2. `Task #2 Disk Scheduler`（10 分）
- 实现一个后台 I/O 线程，异步处理读写请求，并通过 `promise/future` 回传完成信号。

3. `Task #3 Buffer Pool Manager`（60 分）
- 实现页面分配、装载、淘汰、刷盘、pin/unpin 管理。
- 实现 `ReadPageGuard` / `WritePageGuard` 的 RAII 生命周期、移动语义、落盘逻辑。

4. `Task #4 Leaderboard`（5 分）
- 可选性能优化项（`GetDataMut` 基准与扫描基准），不影响主线正确性实现。

题面明确强调：
- 需要线程安全。
- 需要避免死锁。
- 不要持有“大锁”过久，尤其不要在等待页锁时一直持有 `bpm_latch`。

## 2. 本仓库中你要改的文件（P1_FILES）

`CMakeLists.txt` 中 `submit-p1` 指定了提交文件：

- `src/include/buffer/arc_replacer.h`
- `src/buffer/arc_replacer.cpp`
- `src/include/storage/disk/disk_scheduler.h`
- `src/storage/disk/disk_scheduler.cpp`
- `src/include/buffer/buffer_pool_manager.h`
- `src/buffer/buffer_pool_manager.cpp`
- `src/include/storage/page/page_guard.h`
- `src/storage/page/page_guard.cpp`

## 3. 本地 TODO 需要实现的接口总结

### 3.1 ARC Replacer
文件：`src/buffer/arc_replacer.cpp`

- `ArcReplacer::ArcReplacer(size_t num_frames)`
- `ArcReplacer::Evict()`
- `ArcReplacer::RecordAccess(frame_id_t, page_id_t, AccessType)`
- `ArcReplacer::SetEvictable(frame_id_t, bool)`
- `ArcReplacer::Remove(frame_id_t)`
- `ArcReplacer::Size()`

关键语义（来自题面+注释）：
- `Size()` 是“可淘汰帧数”，不是总帧数。
- `RecordAccess()` 处理 ARC 的四种命中/未命中情况（除 REPLACE 外）。
- `Evict()` 做 REPLACE：按 `mru_target_size_` 决策从 `mru_` 或 `mfu_` 淘汰，并迁移到对应 ghost。
- 若目标侧候选都被 pin（不可淘汰），要尝试另一侧。
- `Remove()` 与 `Evict()` 不同：`Remove()` 不应产生 ghost 记录。

### 3.2 Disk Scheduler
文件：`src/storage/disk/disk_scheduler.cpp`

- `DiskScheduler::DiskScheduler(DiskManager *)`
- `DiskScheduler::Schedule(std::vector<DiskRequest> &)`
- `DiskScheduler::StartWorkerThread()`

关键语义：
- 构造时启动后台线程。
- `Schedule()` 将请求压入 `request_queue_`。
- 后台线程循环消费队列，直到收到 `std::nullopt` 退出。
- 每个请求执行后要 `callback_.set_value(true)`（异常时要确保 future 不会永久阻塞）。

### 3.3 Buffer Pool Manager
文件：`src/buffer/buffer_pool_manager.cpp`

- `NewPage()`
- `DeletePage(page_id_t)`
- `CheckedWritePage(page_id_t, AccessType)`
- `CheckedReadPage(page_id_t, AccessType)`
- `FlushPageUnsafe(page_id_t)`
- `FlushPage(page_id_t)`
- `FlushAllPagesUnsafe()`
- `FlushAllPages()`
- `GetPinCount(page_id_t)`

关键语义：
- `CheckedReadPage/CheckedWritePage` 失败时返回 `std::nullopt`（不是 abort）。
- 只有封装器 `ReadPage/WritePage` 在拿不到页时会 `abort`。
- `GetPinCount` 用于测试，必须线程安全且准确。
- `DeletePage`：若页在内存中且 pin_count > 0，返回 `false`；否则从内存与磁盘删除。

### 3.4 Page Guard（RAII）
文件：`src/storage/page/page_guard.cpp`

`ReadPageGuard`：
- 构造函数
- move 构造
- move 赋值
- `Flush()`
- `Drop()`

`WritePageGuard`：
- 构造函数
- move 构造
- move 赋值
- `Flush()`
- `Drop()`

关键语义：
- 析构调用 `Drop()`。
- `Drop()` 要幂等（重复调用无副作用）。
- move 后源对象必须失效，避免 double drop。
- `WritePageGuard` 需要保证脏页标记逻辑正确。

## 4. 推荐实现顺序（按风险递增）

1. 实现 `ArcReplacer` 并单测验证淘汰顺序。
2. 实现 `DiskScheduler`（先保证请求能跑通并返回 future）。
3. 实现 `ReadPageGuard/WritePageGuard`（尤其 `Drop` / move 语义）。
4. 实现 `BufferPoolManager` 的 `CheckedReadPage/CheckedWritePage` 主路径。
5. 实现 `NewPage/DeletePage/Flush*`。
6. 最后收敛 `GetPinCount` 与并发边界，跑全 P1 测试。

这样做的原因：
- BPM 依赖 ARC（淘汰）和 PageGuard（pin/unpin 生命周期）。
- 若先写 BPM，很容易在依赖未稳定时反复改锁顺序。

## 5. 详细实现方案

## 5.1 ArcReplacer 设计建议

建议把四个链表 + 两类映射做成 O(1) 更新：
- alive map：`frame_id -> 元数据(所在列表、evictable、page_id、迭代器)`
- ghost map：`page_id -> 元数据(所在 ghost 列表、迭代器)`

建议统一约定：
- 链表头为“最新”，链表尾为“最旧”，淘汰从尾部找。
- `curr_size_` 只统计 evictable 的 alive 记录。

`RecordAccess` 四类情况：
1. 命中 `mru_/mfu_`：移到 `mfu_` 头部。
2. 命中 `mru_ghost_`：增大 `mru_target_size_`，从 ghost 移除，插入 `mfu_` 头部。
3. 命中 `mfu_ghost_`：减小 `mru_target_size_`，从 ghost 移除，插入 `mfu_` 头部。
4. 全 miss：按 ARC case 4 规则裁剪 ghost 容量，再插入 `mru_` 头部。

`Evict`：
- 根据 `|mru_|` 与 `mru_target_size_` 决定优先淘汰侧。
- 若该侧全是不可淘汰（被 pin），尝试另一侧。
- 淘汰成功后：alive 删除、对应 ghost 插入、`curr_size_--`、返回 frame_id。

`SetEvictable`：
- 仅 alive 记录允许修改。
- 状态改变时同步更新 `curr_size_`。
- 无效 frame_id（越界）按题意抛异常或终止。

`Remove`：
- 只能移除 evictable 记录，否则抛异常/终止。
- 成功后 `curr_size_--`。
- 不创建 ghost 条目。

## 5.2 DiskScheduler 设计建议

`Schedule(std::vector<DiskRequest> &requests)`：
- 遍历请求并 `request_queue_.Put(std::move(req))`。

`StartWorkerThread()`：
- 无限循环 `Get()`。
- 收到 `nullopt` 退出。
- `is_write_` 决定调用 `disk_manager_->WritePage` 或 `ReadPage`。
- 调用 `callback_.set_value(true)`。
- 若出现异常，建议 `set_exception` 或至少 `set_value(false)`，避免调用方 `future.get()` 卡死。

## 5.3 PageGuard 设计建议

构造：
- 保存传入共享指针并设置 `is_valid_ = true`。
- `ReadPageGuard` 拿共享锁（`frame_->rwlatch_.lock_shared()`）。
- `WritePageGuard` 拿独占锁（`frame_->rwlatch_.lock()`），并将 `is_dirty_` 置为 true（写路径保守置脏）。

`Drop()`（重点）：
- 若无效直接返回（幂等）。
- 先释放页锁，再处理 pin/replacer 更新，避免锁环。
- `pin_count_` 原子减 1；若从 1 变 0，再拿 `bpm_latch_` 调 `replacer_->SetEvictable(frame_id, true)`。
- 清空内部指针并置 `is_valid_ = false`。

`Flush()`：
- 用 `DiskScheduler` 发一个写请求并等待 future。
- 成功后清 `is_dirty_`。

move 构造/赋值：
- 转移所有权。
- 先 `Drop()` 当前对象再接管新资源（赋值路径）。
- 源对象清空并设 invalid。

## 5.4 BufferPoolManager 设计建议

建议先写一个内部 helper：
- “拿可用 frame”：优先 `free_frames_`，否则 `replacer_->Evict()`。

`CheckedReadPage/CheckedWritePage` 主流程（两者结构一致，只是最后返回不同 guard）：
1. 拿 `bpm_latch_`。
2. 若页在 `page_table_`：
- `pin_count_++`
- `replacer_->RecordAccess(...)`
- `replacer_->SetEvictable(frame_id, false)`
- 复制 `frame` 共享指针，释放 `bpm_latch_`。
- 构造并返回对应 guard（由 guard 获取页锁）。
3. 若不在内存：
- 找可用 frame；找不到返回 `nullopt`。
- 若为淘汰 frame：
  - 找到旧 page_id 映射并从 `page_table_` 删除。
  - 若脏则刷盘。
- 从磁盘把目标页读入 frame（读请求 + future wait）。
- 更新页表映射、pin_count、dirty、replacer 状态（设为不可淘汰并记录访问）。
- 释放 `bpm_latch_`，返回 guard。

`NewPage()`：
- 原子递增 `next_page_id_` 返回新 page_id。

`DeletePage(page_id)`：
- 若页不在内存：直接 `DeallocatePage(page_id)`，返回 true。
- 若在内存且 `pin_count > 0`：返回 false。
- 若在内存且可删：从 replacer/page_table 移除，frame reset 放回 free list，再 `DeallocatePage`。

`FlushPageUnsafe/FlushPage`：
- `Unsafe` 不拿页锁。
- 安全版先拿页锁再刷。
- 刷盘后更新 dirty 标记。

`FlushAllPagesUnsafe/FlushAllPages`：
- 遍历页表逐页刷盘（安全版逐页加锁）。

`GetPinCount(page_id)`：
- 仅持有 `bpm_latch_` 查页表。
- 命中后原子 `load()` 该 frame 的 `pin_count_`。

## 6. 并发与死锁规避（必须遵守）

建议固定锁顺序：
- 元数据路径：`bpm_latch_`。
- 页面数据路径：`frame->rwlatch_`。

强约束：
- 不要在“等待 `frame->rwlatch_`”时持有 `bpm_latch_`。
- `Drop()` 不要在持有页锁期间长时间占用 `bpm_latch_`。

对应 `DeadlockTest` 的典型坑：
- 线程 A 持有页 0 写锁并尝试拿 `bpm_latch_`。
- 线程 B 持有 `bpm_latch_` 等页 0 写锁。
- 形成环路，测试会卡死。

## 7. 建议测试流程

先编译相关测试：

```bash
cd /home/lyricx/cmu_15445/bustub-private/build
cmake ..
make -j$(nproc) arc_replacer_test disk_scheduler_test page_guard_test buffer_pool_manager_test
```

运行禁用测试（P1 必须用这个参数）：

```bash
./test/arc_replacer_test --gtest_also_run_disabled_tests
./test/disk_scheduler_test --gtest_also_run_disabled_tests
./test/page_guard_test --gtest_also_run_disabled_tests
./test/buffer_pool_manager_test --gtest_also_run_disabled_tests
```

风格与提交包检查：

```bash
cd /home/lyricx/cmu_15445/bustub-private/build
make check-clang-tidy-p1
make submit-p1
```

## 8. 实施时的高频错误清单

1. `curr_size_` 与 evictable 状态不同步，导致 ARC 大小错误。
2. `Remove()` 错误地产生 ghost 记录。
3. `Drop()` 没有幂等，析构二次释放导致崩溃。
4. move 语义没把源对象置 invalid，导致双重 unpin。
5. 读/写 guard 获取页锁时仍持有 `bpm_latch_`，触发死锁。
6. `future.get()` 永久等待（后台线程没 `set_value` 或异常分支漏处理）。
7. `GetPinCount` 读错对象或缺少 bpm 级保护，导致竞态。

## 9. 结论（后续编码执行指南）

按本文件的推荐顺序实现：
- 先 ARC，后磁盘调度，再 Guard，最后 BPM 主逻辑与 flush/delete 收尾。
- 全程先保正确性和线程安全，再做性能优化（Leaderboard 放最后）。

这份文档作为后续编码的主 checklist，目标是先通过本地 P1 disabled tests，再准备提交 `project1-submission.zip`。
