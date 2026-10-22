# RMTC 参赛程序 C++ 重实现规格（spec）

## 1. 背景与目标

用户要求本工程改用 **C++** 实现（“如果使用 python 请改过来，如果必须要用 python 实现则保留不变”）。

结论：ROS 2 Humble 节点不强制使用 Python，感知/决策/HTTP/AI 逻辑均可用 C++17 等价实现，因此执行改造。

目标：

1. 将 `src/rmtc_bot` 从 `ament_python` 包改为 **`ament_cmake` C++17 包**，包名仍为 `rmtc_bot`。
2. 以现有 Python 实现为**行为基准**，逐模块等价移植：协议、决策语义、安全约束、降级策略保持一致。
3. 保持所有**外部契约不变**：话题名、消息类型、QoS、动作 JSON、配置文件键、`run_contestant.sh`、launch 文件、可执行名 `main_node`。
4. 用 C++ gtest 替换原 Python 单元测试，覆盖原 44 个 pytest 用例对应的行为。

## 2. 非目标

- 不修改比赛协议、决策规则、提示词语义。
- 不新增决策类型，不改变 `decision_enum.json`。
- 不引入除 `rclcpp`、`std_msgs`、`ament_index_cpp`、`jsoncpp`、`yaml-cpp`、`libcurl`、`gtest` 之外的依赖。
- 不保留 Python 运行时代码（避免双实现漂移）。

## 3. 外部契约（不可变更）

### 3.1 话题与 QoS

| 话题 | 方向 | 类型 | QoS |
|---|---|---|---|
| `/team_{X}/perception` | 订阅 | `std_msgs/String` | RELIABLE / VOLATILE / depth 1 |
| `/team_{X}/action` | 发布 | `std_msgs/String` | RELIABLE / VOLATILE / depth 1 |
| `/team_{X}/registration` | 发布 | `std_msgs/String` | RELIABLE / TRANSIENT_LOCAL / depth 1 |
| `/referee/mission` | 订阅 | `std_msgs/String` | RELIABLE / TRANSIENT_LOCAL / depth 1 |
| `/referee/team_registry` | 订阅 | `std_msgs/String` | RELIABLE / TRANSIENT_LOCAL / depth 1 |
| `/referee/event_text` | 订阅 | `std_msgs/String` | RELIABLE / TRANSIENT_LOCAL / depth 128 |

### 3.2 动作 JSON

```json
{"tick":123,"session_id":"s1","action":"move","direction":"right"}
```

- `action ∈ {move, attack, defend, wait}`，全小写。
- `direction ∈ {up, down, left, right}`，仅 move/attack 携带。
- 单条 UTF-8 序列化结果 ≤ 4096 字节。
- 未知字段、缺字段、类型错误均拒绝。

### 3.3 参数与配置

- ROS 参数：`team`(string)、`team_name`(string)、`config_dir`(string)、`stage`(int)。
- 环境变量：`RMTC_TEAM`、`RMTC_TEAM_NAME`、`RMTC_TEAM_TOKEN`、`RMTC_AI_BASE_URL`。
- `config/params.yaml`、`config/decision_config.json`、`config/decision_enum.json` 键与现有文件完全一致。
- `decision_config.json` 只允许 `decision_type` 一个字段；值必须属于 `decision_enum.json`。
- `decision_type` 运行期热加载（基于文件 mtime）。

### 3.4 可执行与打包

- 可执行名必须仍为 **`main_node`**（`ros2 run rmtc_bot main_node`）。
- `run_contestant.sh` 与 `launch/stage{0,1,2}.launch.py` 不做语义修改。
- 配置安装到 `share/rmtc_bot/config`，launch 安装到 `share/rmtc_bot/launch`，脚本安装到 `share/rmtc_bot/scripts`。

## 4. 目标目录结构（精确）

```
src/rmtc_bot/
├── CMakeLists.txt                      # 新增
├── package.xml                         # 改写为 ament_cmake
├── config/{params.yaml,decision_config.json,decision_enum.json}   # 不变
├── launch/stage{0,1,2}.launch.py       # 不变
├── scripts/run_contestant.sh           # 不变
├── include/rmtc_bot/
│   ├── types.hpp
│   ├── utils/{logger.hpp,config_loader.hpp,time_window.hpp}
│   ├── perception/{relative_map.hpp,parser.hpp,state_store.hpp,entity_tracker.hpp}
│   ├── planning/{grid_map.hpp,path_planner.hpp,target_selector.hpp,combat_rules.hpp}
│   ├── action/{action_schema.hpp,action_sender.hpp,last_action.hpp}
│   ├── ai/{event_memory.hpp,llm_client.hpp,prompt_builder.hpp,output_validator.hpp,quota_manager.hpp}
│   ├── events/{event_input.hpp,mission_input.hpp,event_state.hpp}
│   ├── decision/{context.hpp,safety_guard.hpp,stage1_collect.hpp,stage2_combat.hpp,local_fallback.hpp,decision_router.hpp}
│   ├── registration/team_registry.hpp
│   └── main_node.hpp
├── src/
│   ├── types.cpp                       # 仅必要的哈希/工具（可 header-only）
│   ├── utils/{logger.cpp,config_loader.cpp,time_window.cpp}
│   ├── perception/{relative_map.cpp,parser.cpp,state_store.cpp,entity_tracker.cpp}
│   ├── planning/{grid_map.cpp,path_planner.cpp,target_selector.cpp,combat_rules.cpp}
│   ├── action/{action_schema.cpp,action_sender.cpp,last_action.cpp}
│   ├── ai/{event_memory.cpp,llm_client.cpp,prompt_builder.cpp,output_validator.cpp,quota_manager.cpp}
│   ├── events/{event_input.cpp,mission_input.cpp,event_state.cpp}
│   ├── decision/{safety_guard.cpp,stage1_collect.cpp,stage2_combat.cpp,local_fallback.cpp,decision_router.cpp}
│   ├── registration/team_registry.cpp
│   ├── main_node.cpp
│   └── main.cpp
└── test/
    ├── test_relative_position.cpp
    ├── test_perception_parse.cpp
    ├── test_action_schema.cpp
    ├── test_planning.cpp
    ├── test_event_text_no_regex.cpp
    └── test_llm_fallback.cpp
```

删除清单（见 §10）。

## 5. 构建系统

### 5.1 CMakeLists.txt

- `cmake_minimum_required(VERSION 3.8)`，`project(rmtc_bot CXX)`，`C++17`，`-Wall -Wextra`。
- `find_package(rclcpp REQUIRED)`、`std_msgs`、`ament_index_cpp`、`jsoncpp REQUIRED`、`yaml-cpp REQUIRED`、`CURL REQUIRED`。
- 核心静态库 `rmtc_bot_core`：包含除 `main_node.cpp`、`team_registry.cpp`、`main.cpp` 外的全部源文件，链接 `jsoncpp yaml-cpp CURL::libcurl`。该库不依赖 rclcpp，便于纯逻辑单测。
- 可执行 `main_node`：`main_node.cpp` + `team_registry.cpp` + `main.cpp`，链接 `rmtc_bot_core rclcpp::rclcpp std_msgs::std_msgs ament_index_cpp::ament_index_cpp`。
- 安装：
  - `install(TARGETS main_node DESTINATION lib/${PROJECT_NAME})`
  - `install(DIRECTORY config launch DESTINATION share/${PROJECT_NAME})`
  - `install(PROGRAMS scripts/run_contestant.sh DESTINATION share/${PROJECT_NAME}/scripts)`
- 测试：`find_package(ament_cmake_gtest REQUIRED)`；6 个 gtest 目标，链接 `rmtc_bot_core`；`ament_add_gtest` 注册；`ament_package()`。

### 5.2 package.xml

- `<buildtool_depend>ament_cmake</buildtool_depend>`
- `<depend>rclcpp</depend> <depend>std_msgs</depend> <depend>ament_index_cpp</depend>`
- `<depend>jsoncpp</depend> <depend>yaml-cpp</depend> <depend>libcurl</depend>`
- `<test_depend>ament_cmake_gtest</test_depend>`
- `<export><build_type>ament_cmake</build_type></export>`

## 6. 模块映射与关键接口

命名空间统一 `rmtc_bot`；坐标 `using Cell = std::pair<int,int>`，配套 `CellHash`。

| Python | C++ | 关键接口 / 语义 |
|---|---|---|
| `perception/relative_map.py` | `perception/relative_map.{hpp,cpp}` | `std::optional<Cell> parse_relative_position(const std::string&, std::string& err)`；9 行×9 列、89 字符、无尾换行；无 `2` → `(0,0)`；多 `2`/非法字符 → 失败；`Cell to_absolute(Cell self, const std::string&)` |
| `perception/parser.py` | `perception/parser.{hpp,cpp}` | `ParseResult parse_perception(const std::string&)`；校验 tick≥0、session_id 非空、vision_radius 0..4、map.width/height≥1、position 长度 2；实体非法则跳过；失败不抛异常，返回 `ok=false + error` |
| `perception/state_store.py` | `perception/state_store.{hpp,cpp}` | `begin_session/reset/update/has_seen_tick/can_move/can_attack/can_defend/must_wait`；保存 `seen_ticks` |
| `perception/entity_tracker.py` | `perception/entity_tracker.{hpp,cpp}` | `update/visible_enemies/active_resources/enemy_cells`；视野覆盖但未出现 → 删除资源 |
| `planning/grid_map.py` | `planning/grid_map.{hpp,cpp}` | `set_map/update_from_perception/mark_temp_blocked/add_temp_wall_cross/prune/is_blocked/in_bounds`；`temp_blocked` TTL |
| `planning/path_planner.py` | `planning/path_planner.{hpp,cpp}` | `DIRECTIONS` 顺序 up/down/left/right；`move_cell`；`bfs_distances`；`bfs_first_step`（多车争格由 `extra_blocked` 表达，`discard(goal)`）；扩张上限 20000 |
| `planning/combat_rules.py` | `planning/combat_rules.{hpp,cpp}` | `line_target`（1..max_range 最近存活，保护目标仍挡后方）；`best_attack`（跳过 protected；洁身自好时跳过 defending）；`enemies_within`；`normal_damage` |
| `planning/target_selector.py` | `planning/target_selector.{hpp,cpp}` | `pick_resource`（距离优先、同距价值高优先；prefer 无匹配时回退 any）；`pick_frontier`（最近未知格） |
| `action/action_schema.py` | `action/action_schema.{hpp,cpp}` | `struct Action{int tick; std::string session_id, action, direction;}`；`validate/build/serialize`（jsoncpp 紧凑输出） |
| `action/action_sender.py` | `action/action_sender.{hpp,cpp}` | `send()` 同 Tick 只发一次；`resend_cached(tick,session)` 不推进规划；`reset()` |
| `action/last_action.py` | `action/last_action.{hpp,cpp}` | `interpret_last_action` → `AckReport`；只解释回执，不做定位 |
| `ai/event_memory.py` | `ai/event_memory.{hpp,cpp}` | 互斥锁保护；按内容去重；`pending_snapshot/ack_pending/recent_text(max_chars)`；容量 128 |
| `ai/llm_client.py` | `ai/llm_client.{hpp,cpp}` | libcurl：`GET {base}/healthz`；`POST {base}/v1/chat/completions`；`Authorization: Bearer`；`Content-Length` 由 curl 设置；超时；错误码映射 `missing_token/invalid_token/payload_too_large/quota_exceeded/rate_limit/concurrency_limit/timeout/network_error/invalid_response/http_<n>` |
| `ai/prompt_builder.py` | `ai/prompt_builder.{hpp,cpp}` | `SYSTEM_PROMPT` 原文照搬；`build_user_prompt`；`build_messages` 返回 jsoncpp `Json::Value` 数组 |
| `ai/output_validator.py` | `ai/output_validator.{hpp,cpp}` | `validate_llm_output` → `LLMDecision`；禁止键 `action/actions/direction/moves/commands/tick_action`；事件枚举/策略枚举/推荐枚举；area 排序并裁剪到地图内；point 越界→null；`active_until<=tick` 的事件丢弃；event 上限 16 |
| `ai/quota_manager.py` | `ai/quota_manager.{hpp,cpp}` | 互斥锁；`should_call/begin_call/end_call/reset_session/snapshot`；`quota_exceeded` 置 exhausted；`rate_limit/concurrency_limit` 退避 `tick+4`；`qualified` 本场成功标记 |
| `events/event_input.py` | `events/event_input.{hpp,cpp}` | `from_ros_text/from_perception` 仅缓存去重，**不做语义解析** |
| `events/mission_input.py` | `events/mission_input.{hpp,cpp}` | `update_from_text/update_from_perception/incomplete_types/reach_target/needs`；无结构化任务时视为标准三项 |
| `events/event_state.py` | `events/event_state.{hpp,cpp}` | `TimedEvent`；`apply(LLMDecision,tick,GridMap*)`；`illusion_active/clean_reflect_active/vulnerability_active/is_cell_blocked/is_cell_risky/healing_point/fortress_center/strategy_hint`；world_split 提前 1 Tick 布墙 |
| `decision/context.py` | `decision/context.hpp` | `struct Intent{action,direction,reason}`；`DecisionContext` 持有指针 + `planning()` |
| `decision/stage1_collect.py` | `decision/stage1_collect.{hpp,cpp}` | 仅 MOVE/WAIT；资源贪心 → 探索 → 地图中心 → WAIT |
| `decision/stage2_combat.py` | `decision/stage2_combat.{hpp,cpp}` | 顺序：危险撤离 → 攻击 → 低血防御 → 回血点 → 堡垒 → 优先到达 → 资源 → 到达 → 探索 → WAIT；易伤时 defend 阈值 +15 |
| `decision/local_fallback.py` | `decision/local_fallback.{hpp,cpp}` | 调 `stage2_combat`，异常 → WAIT |
| `decision/safety_guard.py` | `decision/safety_guard.{hpp,cpp}` | 阵亡→WAIT；stage1 仅 move/wait；保护不能攻击；攻击必须有方向和直线目标；移动校验越界/障碍/事件封锁/敌占；防御阶段校验 |
| `decision/decision_router.py` | `decision/decision_router.{hpp,cpp}` | stage==1 强制采集；其余按 `decision_type` 路由，未知类型回退 local_fallback |
| `registration/team_registry.py` | `registration/team_registry.{hpp,cpp}` | 依赖 rclcpp；队名清洗与 UTF-8 1..96 字节；发布注册；解析 registry JSON |
| `utils/config_loader.py` | `utils/config_loader.{hpp,cpp}` | `Params{stage,team,team_name,AiParams,PlanningParams}`；`load_params`（yaml-cpp，缺失用默认值）；`load_decision_config`（jsoncpp，仅允许一个字段，校验枚举） |
| `utils/logger.py` | `utils/logger.{hpp,cpp}` | `Logger` 单例/实例；`info/warn/error/debug`；`redact` 替换 `RMTC_TEAM_TOKEN` 与注册密钥为 `***`；互斥锁 |
| `utils/time_window.py` | `utils/time_window.{hpp,cpp}` | `is_fresh/is_current_tick` |
| `main_node.py` | `main_node.{hpp,cpp}` + `main.cpp` | 订阅/发布、会话切换清空、旧 Tick 丢弃、同 Tick 重发缓存、回执处理、AI 异步、安全预检、幻术反转、动作序列化发送 |

### 6.1 主节点线程模型

- 感知/任务/事件回调均在 `rclcpp::spin` 线程执行。
- AI 调用在 `std::thread` 中执行；结果写入 `std::mutex` 保护的 `std::deque<AiResult>`，下一次感知回调 `drain_ai_results` 消费。
- 线程对象存入 `std::vector<std::thread>`；析构/`shutdown` 时置 `shutting_down_` 并 `join`，避免悬垂引用。
- 所有跨线程共享状态（EventMemory、QuotaManager、结果队列、关闭标志）均有互斥保护。
- 任何 AI 异常/超时不阻塞动作窗口：本 Tick 仍用本地策略出动作。

### 6.2 必须保持的行为不变量（验收点）

1. 感知是唯一实时真值，位置只取 `self.position`，绝不累加 `last_action`。
2. 仅处理最新 Tick；早于当前 Tick 丢弃；同 Tick 重发只重发缓存动作，不推进规划器。
3. `session_id` 变化清空状态/规划/定位/动作缓存/事件记忆/配额。
4. 事件文本只经 `EventMemory` 缓存并交给 LLM；**全仓库禁止 regex/关键词/固定句式解析事件**。
5. LLM 输出必须结构化校验，禁止直接下发动作；不允许的字段/枚举 → 降级本地策略。
6. AI 失败（超时/429/502/504/额度）→ 本地回退，不延长动作窗口。
7. 阵亡只发 WAIT；保护期不得 ATTACK；stage1 只允许 MOVE/WAIT。
8. `decision_type` 只从 `decision_config.json` 读取，值必须在枚举内；现场只改该字段即可生效。
9. 动作字段小写、方向合法、≤4096 字节。
10. `E_BLOCKED/E_OOB` 回执后对目标格 `mark_temp_blocked(ttl=5)`。
11. 幻术生效时，安全预检通过后再反转 MOVE 指令方向。
12. 日志脱敏，任何路径都不打印 `RMTC_TEAM_TOKEN`。

## 7. 配置文件解析细则

- `load_params`：`params.yaml` 不存在或解析失败 → 返回 `DEFAULT_PARAMS`；顶层必须是映射。
- `load_decision_enum`：支持 `{"decision_types": {...}}`、数组、映射；空 → 错误。
- `load_decision_config`：JSON 顶层对象；除 `decision_type` 外任何字段 → 错误；值不在枚举 → 错误。
- 失败时主节点记录错误并**保持上一个有效 decision_type**（首次失败用 `local_fallback`）。

## 8. 配置目录解析

优先级：
1. ROS 参数 `config_dir` 非空且存在 → 使用。
2. `ament_index_cpp::get_package_share_directory("rmtc_bot") + "/config"`。
3. 相对当前工作目录 `config/`。

## 9. 测试规格（gtest）

| 文件 | 覆盖用例 |
|---|---|
| `test_relative_position.cpp` | 中心/四向偏移、重叠无 2、行数错误、行长错误、多 2、非法字符、绝对坐标叠加 |
| `test_perception_parse.cpp` | 完整感知、缺字段、tick 负数、session 非字符串、vision_radius 越界、坏实体跳过、last_action 解析、missions/score 默认值、非法 JSON |
| `test_action_schema.cpp` | 四种动作、方向缺失/多余、大写拒绝、未知字段、tick 类型、session 空、序列化字节上限、同 Tick 只发一次、缓存重发、reset |
| `test_planning.cpp` | BFS 距离/首步、不可达、extra_blocked 与 goal 例外、障碍/临时墙、temp_blocked TTL、资源选择与回退、frontier、攻击直线/最近/保护阻挡/洁身自好 |
| `test_event_text_no_regex.cpp` | 去重、pending/ack、recent_text 截断、场次清空、文本原样保存；并断言源码不含 `std::regex`/`regex` |
| `test_llm_fallback.cpp` | 合法 LLM JSON、代码围栏剥离、禁止 action 字段、非法枚举、area 裁剪、point 越界、过期事件丢弃、配额 exhausted/退避/qualified、local_fallback 返回合法 Intent |

目标：≥ 44 个 gtest 用例，全部通过。

## 10. 删除清单

- `src/rmtc_bot/setup.py`
- `src/rmtc_bot/setup.cfg`
- `src/rmtc_bot/resource/`（含 `rmtc_bot` 标记文件）
- `src/rmtc_bot/rmtc_bot/`（全部 Python 源码与 `__init__.py`）
- `rmtc_bot_ws/test/*.py`（6 个 pytest 文件）

保留：`config/`、`launch/`、`scripts/`、`docs/`、`run_contestant.sh` 软链接。

## 11. 验证步骤

1. 静态检查：`grep -rn "regex\|re\.\|search(" src/rmtc_bot/src src/rmtc_bot/include` 无事件解析用途的正则。
2. 构建：`colcon build --packages-select rmtc_bot --cmake-args -DCMAKE_BUILD_TYPE=Release` → 成功，无 warning 阻断。
3. 单测：`colcon test --packages-select rmtc_bot && colcon test-result --verbose` → 0 失败，用例数 ≥ 44。
4. 可执行存在：`install/rmtc_bot/lib/rmtc_bot/main_node`。
5. 运行冒烟（不新建临时脚本）：
   - `RMTC_TEAM=A ROS_DOMAIN_ID=27 ros2 run rmtc_bot main_node --ros-args -p team:=A -p stage:=2`
   - 另开终端 `ros2 topic pub --once /team_A/perception std_msgs/String '{data: "<完整感知JSON>"}'`
   - 断言 `/team_A/action` 收到合法 JSON，`tick/session_id` 回填正确。
   - 若沙箱限制 ROS CLI 写入 `__pycache__`，则在沙箱外执行该步并在报告中注明。
6. 启动脚本：`./run_contestant.sh A RMTC-Bot 127.0.0.1` 能解析并启动 `main_node`（无令牌时输出告警且 AI 关闭）。
7. 文档同步：`docs/technical_report.md` 更新为 C++ 架构、构建/测试命令、用例数。

## 12. 风险与决策

| 风险 | 决策 |
|---|---|
| jsoncpp 无 `std::optional` 语义 | 所有可能失败的构造函数返回 `bool` + `std::string& err`；不使用异常做控制流 |
| 线程析构悬垂 | 保存 `std::thread` 并 join；共享队列加锁 |
| curl 全局初始化 | `main.cpp` 调 `curl_global_init/cleanup`；单测不触发网络 |
| 安装后 config_dir 为空 | `ament_index_cpp` 解析 share/config，失败回退 `./config` |
| 无 Python 运行时 | 删除 `setup.py` 与 Python 包；保留 `main_node` 可执行名不变 |
| 沙箱限制 ROS CLI | 冒烟步骤可在沙箱外执行，属环境限制不算代码失败 |

## 13. 审批

本规格覆盖构建系统、源码结构、模块接口、行为不变量、删除清单、测试与验证。请确认后开始实施；实施中如需偏离，将先回到本文件更新并重新确认。
