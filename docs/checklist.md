# 验证清单（checklist）

按 spec §11 与 tasks.md 完成标准逐项核对。

- [x] C1 构建系统为 ament_cmake，`main_node` 可执行名保持不变
- [x] C2 `rmtc_bot_core` 编译通过且不依赖 rclcpp
- [x] C3 感知解析/相对坐标/状态存储行为等价
- [x] C4 规划层 BFS/攻击/资源选择行为等价
- [x] C5 动作校验、同 Tick 单发、缓存重发行为等价
- [x] C6 事件仅缓存去重，无 regex/关键词语义解析
- [x] C7 LLM 输出结构校验与本地降级行为等价
- [x] C8 安全守卫覆盖阵亡/保护/阶段/越界/敌占
- [x] C9 主节点会话切换、旧 Tick 丢弃、幻术反转、回执回避完整
- [x] C10 6 个 gtest 文件全部通过，用例数 ≥ 44
- [x] C11 colcon build + colcon test 通过
- [x] C12 冒烟验证（或记录沙箱限制）
- [x] C13 docs/technical_report.md 已更新为 C++
