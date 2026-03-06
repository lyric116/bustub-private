add_test([=[DiskSchedulerTest.ScheduleWriteReadPageTest]=]  /home/lyricx/cmu_15445/bustub-private/build_local/test/disk_scheduler_test [==[--gtest_filter=DiskSchedulerTest.DISABLED_ScheduleWriteReadPageTest]==] --gtest_also_run_disabled_tests [==[--gtest_output=xml:/home/lyricx/cmu_15445/bustub-private/build_local/test/disk_scheduler_test.xml]==] [==[--gtest_catch_exceptions=0]==])
set_tests_properties([=[DiskSchedulerTest.ScheduleWriteReadPageTest]=]  PROPERTIES DISABLED TRUE)
set_tests_properties([=[DiskSchedulerTest.ScheduleWriteReadPageTest]=]  PROPERTIES WORKING_DIRECTORY /home/lyricx/cmu_15445/bustub-private/build_local/test SKIP_REGULAR_EXPRESSION [==[\[  SKIPPED \]]==] TIMEOUT 120)
set(  disk_scheduler_test_TESTS DiskSchedulerTest.ScheduleWriteReadPageTest)
