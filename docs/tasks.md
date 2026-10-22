# C++ 重实现任务清单（tasks）

依据 [spec.md](spec.md)。按顺序执行，每项完成后立即更新 todo 状态。

## T1 构建系统切换

- [x] 新增 `src/rmtc_bot/CMakeLists.txt`（ament_cmake、C++17、`rmtc_bot_core` 静态库、`main_node` 可执行、install 规则、6 个 gtest）。
- [x] 改写 `src/rmtc_bot/package.xml` 为 `ament_cmake` 依赖集。
- [x] 删除 `setup.py`、`setup.cfg`、`resource/`。
- [x] 完成标准：`colcon build` 能识别包为 cmake 类型（此时源码未全，允许编译失败）。

## T2 基础类型与工具

- `types.hpp/.cpp`：`Cell`、`CellHash`、`LastAction`、`SelfState`、`VisibleEntity`、`Perception`、`Score`、`Mission`、`direction` 工具。
- `utils/logger`：分级日志、密钥注册、`redact`、互斥。
- `utils/time_window`：`is_fresh`、`is_current_tick`。
- `utils/config_loader`：`Params/AiParams/PlanningParams`、`load_params`（yaml-cpp）、`load_decision_enum`、`load_decision_config`。
- [x] 完成标准：单文件可编译进 `rmtc_bot_core`。

## T3 感知层

- `perception/relative_map`：9×9 解析、绝对坐标。
- `perception/parser`：schema 校验、坏实体跳过、`ParseResult`。
- `perception/state_store`：场次/tick/地图/比分、可行动查询。
- `perception/entity_tracker`：敌人/资源记忆、视野消失删除。
- [x] 完成标准：对应 gtest 用例通过。

## T4 规划层

- `planning/grid_map`：障碍、已知视野、临时墙、临时封锁、prune。
- `planning/path_planner`：BFS 距离与首步。
- `planning/combat_rules`：直线目标、最佳攻击、范围威胁、伤害。
- `planning/target_selector`：资源选择、frontier。
- [x] 完成标准：`test_planning.cpp` 全绿。

## T5 动作层

- `action/action_schema`：校验/构造/序列化（jsoncpp）。
- `action/action_sender`：同 Tick 单发、缓存重发、reset。
- `action/last_action`：回执解释。
- [x] 完成标准：`test_action_schema.cpp` 全绿。

## T6 事件与任务

- `ai/event_memory`：去重、pending、recent_text、reset。
- `events/event_input`：ROS 文本/感知文本入库（无语义解析）。
- `events/mission_input`：结构化任务、完成集合、reach 目标。
- `events/event_state`：事件窗口、world_split 布墙、风险点、回血点、堡垒中心、策略提示。
- [x] 完成标准：`test_event_text_no_regex.cpp` 全绿。

## T7 AI 层

- `ai/prompt_builder`：SYSTEM_PROMPT 原文、user prompt、messages。
- `ai/output_validator`：结构校验、禁止字段、枚举、坐标裁剪、过期事件丢弃。
- `ai/quota_manager`：调用门控、在途上限、额度耗尽、退避、合格标记。
- `ai/llm_client`：libcurl `/healthz`、`/v1/chat/completions`、错误码映射。
- [x] 完成标准：`test_llm_fallback.cpp` 全绿。

## T8 决策层

- `decision/context.hpp`：`Intent`、`DecisionContext`。
- `decision/stage1_collect`：资源→探索→中心→WAIT。
- `decision/stage2_combat`：危险撤离→攻击→防御→回血→堡垒→到达→资源→探索→WAIT。
- `decision/local_fallback`：复用 stage2，异常 WAIT。
- `decision/safety_guard`：阵亡/阶段/保护/方向/目标/越界/障碍/敌占。
- `decision/decision_router`：stage1 强制、按类型路由、未知回退。
- [x] 完成标准：编译通过，单元级调用返回合法 Intent。

## T9 主节点与注册

- `registration/team_registry`：队名校验、注册发布、registry 订阅。
- `main_node`：QoS、订阅/发布、会话切换、旧 Tick 丢弃、同 Tick 重发、回执处理、AI 异步队列、幻术反转、动作发送、热加载配置。
- `main.cpp`：`rclcpp::init/spin/shutdown`、curl 全局初始化、退出汇总。
- [x] 完成标准：`colcon build` 成功，`main_node` 可执行生成。

## T10 测试迁移

- [x] 新建 6 个 gtest 文件（见 spec §9），迁移原 pytest 断言。
- [x] 删除 `rmtc_bot_ws/test/*.py` 与 `src/rmtc_bot/rmtc_bot/` Python 源码。
- [x] 完成标准：用例数 ≥ 44，全部通过（实际 99 项）。

## T11 构建、测试与冒烟

- [x] `colcon build --packages-select rmtc_bot --cmake-args -DCMAKE_BUILD_TYPE=Release`。
- [x] `colcon test --packages-select rmtc_bot && colcon test-result --verbose`。
- [x] `ros2 topic pub` 单帧感知 → 校验 `/team_A/action` 回包。
- [x] `./run_contestant.sh A RMTC-Bot 127.0.0.1` 启动链路检查（无 token 告警属预期）。
- [x] 完成标准：构建/测试/冒烟全部通过，或冒烟受沙箱限制时明确记录。

## T12 文档同步

- [x] 更新 `docs/technical_report.md`：C++ 架构、目录、构建/测试命令、用例数、线程模型。
- [x] 确认 `docs/decision_enum.md` 与实现一致（无需改动则保持）。
- [x] 完成标准：文档命令可直接复现。

## 审批状态

- [x] 用户已确认 spec.md 与 tasks.md
