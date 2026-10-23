# RMTC 图灵对决赛参赛程序技术报告

## 1. 总体设计

程序为 ROS 2 Humble / C++17 节点 `rmtc_bot`（ament_cmake 构建，核心逻辑编译为不依赖 rclcpp 的
静态库 `rmtc_bot_core`），按"感知为唯一实时真值"设计：

- 位置只取感知 `self.position`，绝不使用 `last_action` 累加位移。
- 每个实体的 9x9 相对位置图独立解析，`2` 所在列/行减 4 得到相对位移，再与当前 `self.position` 相加恢复绝对坐标。
- 所有动作回填当前感知的 `tick` 与 `session_id`，不自行生成 `session_id`。
- `session_id` 变化时清空规划、定位、动作缓存、事件记忆与 AI 配额状态。
- 旧 Tick 感知直接丢弃；同 Tick 重发不重复推进规划器。
- 随机事件文本只做缓存与按内容去重，语义理解全部交给统一 LLM；本地不使用正则或关键词解析事件。
- LLM 只输出高层策略与事件结构化摘要，经本地结构校验后才允许影响规划，且永不直接下发动作。
- AI 调用异步执行，超时、429、502/504、输出非法时立即回退到本地规则策略，不阻塞动作窗口。

## 2. 目录与模块

```
rmtc_bot_ws/
├── src/rmtc_bot/
│   ├── CMakeLists.txt     ament_cmake 构建：rmtc_bot_core 静态库 + main_node 可执行 + 6 个 gtest
│   ├── package.xml        rclcpp / std_msgs / ament_index_cpp / jsoncpp / yaml-cpp / libcurl
│   ├── config/            decision_config.json / decision_enum.json / params.yaml
│   ├── launch/            stage0 / stage1 / stage2 launch
│   ├── scripts/           run_contestant.sh
│   ├── include/rmtc_bot/  公共头文件（types、perception、planning、action、ai、events、decision、utils）
│   ├── src/
│   │   ├── main.cpp       进程入口：curl 全局初始化、rclcpp init/spin/shutdown
│   │   ├── main_node.cpp  订阅、会话与 Tick 管理、决策路由、动作发布、AI 异步
│   │   ├── perception/    解析、状态存储、相对坐标、实体短期记忆
│   │   ├── action/        动作构造、单 Tick 发送、last_action 回执解释
│   │   ├── decision/      路由、第一阶段、第二阶段、降级、安全预检
│   │   ├── ai/            网关客户端、提示词、输出校验、配额、事件去重
│   │   ├── planning/      网格、BFS 寻路、目标选择、战斗规则
│   │   ├── events/        事件/任务输入、事件生效窗口
│   │   ├── registration/  注册发布与名录订阅
│   │   └── utils/         脱敏日志、时间窗口、配置校验
│   └── test/              test_perception_parse / test_relative_position / test_action_schema /
│                          test_event_text_no_regex / test_llm_fallback / test_planning
└── docs/                  本报告与决策枚举说明
```

## 3. 关键实现

### 3.1 感知与定位

`perception/parser.cpp` 对 `tick`、`session_id`、`self.position`、`visible_entities[].position`、
`missions`、地图尺寸做类型校验。单个实体解析失败只跳过该实体，不终止进程；整体解析失败时主节点
不推进状态、不发送动作，等待下一帧。

`perception/relative_map.cpp` 要求字符串恰好 9 行、每行 9 字符、总长 89（81 格 + 8 个换行），
`0` 不视为可通行标记，也不等同于地图内。

### 3.2 动作与安全

- `action_schema.cpp`：动作字段小写、方向仅 `up/down/left/right`、`wait/defend` 不带方向、
  单条 JSON 不超过 4096 字节。
- `action_sender.cpp`：每个 Tick 最多发布一次合法动作，重复 Tick 不重复发送。
- `safety_guard.cpp`：阵亡只 WAIT；保护期不攻击；第一阶段只 MOVE/WAIT；移动前检查越界、
  已知障碍、世界分裂临时墙、已占格；攻击前确认 1-2 格直线存在目标。
- `last_action.cpp`：仅解释回执，幻术方向直接采用 `effective_direction`，不二次反转。

### 3.3 决策

- `stage1_collect`：带路线预演的资源采集 + 最近未知区域探索，仅 MOVE/WAIT。
  选择下一颗资源时，对每颗已知资源试算“先到它、再贪心串完其余资源”的总路程，
  取总路程最短的一颗，避免只盯最近资源而把后续路线带偏；未知区域仍按最近前沿探索。
- `stage2_combat`：按 v1.1 规则“输出与生存并重”组织：
  1. 危险区撤离 → 2. 天下无双 5×5 攻击 → 3. 攻击（斩杀 / 后期血量尚可 / 前期只打贴脸敌人）→
  4. 低血生存（能换血先换血，不能换血先防御，无法防御再后撤）→ 5. 天使恩泽回血。
  - 死亡代价：v1.1 每次死亡扣 60 点有效伤害并等待 3 Tick 复活，因此每次攻击前先算
    本 Tick 同行/同列敌人的总伤害（普通 20、潘潘的神易伤 40、防御减半）；
    若站着必死而防御能活，即使能完成一次击杀也优先防御，不用复活等待换人头。
  - 资源候选：视野内资源即时增删；视野外保留记忆但设 60 Tick 年龄上限，
    并且只有“离开视野后最近 8 Tick 内有敌人在其相邻格”才判定为已被对手取走，
    避免原先 10 Tick 硬过滤误弃仍然有效的资源。
  - 前期（`tick < hunt_after_tick`）：资源采集优先，资源用与第一阶段相同的路线预演
    （`pick_resource(..., tour_lookahead=true)`）批量串点；无资源时直接去任务点；
    `reach_priority_tick` 后进入任务点冲刺，只顺路拾取 4 格内资源；
    堡垒有增益且无资源可抢时再驻留；不主动追击视野内敌人（敌人进入射程时仍会攻击）。
  - 后期（`tick >= hunt_after_tick`，或三项任务完成且资源价值已达 120 的计分上限）：先补天下无双
    增益与到达任务点，再主动追击视野内敌人与最近 30 Tick 见过的敌人，血量高于防御阈值即持续输出；
    只有 3 格内资源才顺路拾取。经济收益归零后立即开打，避免把 Tick 空耗在探索上。
  - 击杀优先：击杀奖励 60 相当于三次普通命中，追击排序优先“一次攻击即可击毁”的目标，
    并把潘潘的神易伤（目标移动后受伤 ×2）计入实际伤害，30 HP 敌人在易伤期可一击击杀。
  - 低血判定：先数本 Tick 能命中自己的敌人，若不防御必死而防御可活则防御（`stage2_defend_lethal`）；
    已在射程内且本 Tick 不会阵亡时继续攻击；被两路以上锁定时先防御减半；
    其余情况再后撤，撤离判定把最近 6 Tick 见过的敌人也算作威胁，避免在视距边界来回折返。
  - LLM 策略折算为有限偏置，超出 100 Tick 窗口自动失效：`fight` 提前 60 Tick 进入输出期、
    `collect` 推后 40 Tick 并把顺路拾取放宽到 6 格、`reach` 优先任务点、`defend/avoid`
    提高防御阈值且不追记忆目标、`heal` 提高回血阈值、`fortress` 优先驻留堡垒。
  - 所有移动都有兜底：目标不可达时改走敌人/目标旁的最近可通行格，再不行朝目标方向贪心走一步；
    资源、敌人、下一步全都没有时前往当前最远航点（路程单调缩短的跨图巡回，不做原地折返）；
    最后才用邻格移动或防御，避免空转。
  - 目标格被敌人占住时改走邻格；到达旧记忆位置仍未遇到目标时丢弃该敌人记忆；
    感知未提供任务列表或任务无坐标时使用默认到达点。
  - 障碍记忆：视野内再次确认不到的障碍立即失效（世界分裂十字墙消失后不再封路），
    视野外的障碍最多记忆 40 Tick（覆盖 v1.1 中世界分裂 30 Tick 的持续时间）。
  - 巡逻防绕圈：记录最近 64 Tick 的轨迹，巡逻时优先走没走过的前进格；
    只靠“禁止原路折返”仍会在四格之间循环，浪费输出 Tick。
- 任务点默认 `[12,12]`，可由感知 `missions[].position` 覆盖。
- `local_fallback`：任何 AI 失败时仍由本地规则继续行动，必要时 WAIT。

### 3.4 事件与 LLM

- `event_memory.cpp`：按公告内容去重，跨 Tick 重复公告不重复触发；场次切换清空。
- `prompt_builder.cpp`：赛前封存提示词，要求输出 `strategy`、`summary`、`events[]`
  （`type`、`active_from`、`active_until`、`area`、`point`、`recommendation`），明确禁止输出动作字段。
- `output_validator.cpp`：校验 JSON、枚举、坐标范围、时间区间，拒绝任何 `action/direction/moves` 字段。
- `quota_manager.cpp`：最多 2 个在途调用，记录本场成功调用标志；`quota_exceeded` 直接停止 AI，
  `rate_limit/concurrency_limit` 退避，其它错误回退本地策略。
- 事件生效窗口（v1.1）：世界分裂十字墙在激活前 2 Tick 就视为危险并要求离开整行整列，
  激活前一 Tick 起禁止进入；爆炸按 `active_from` 前 2 Tick 预警（80 HP、无视防御）；
  洁身自好期间不攻击防御中的敌人（否则自承反伤且无收益，无双攻击同样跳过）；
  潘潘的神：观察到的敌人在触发窗口内移动即视为易伤（我方伤害 ×2），
  我方在触发窗口内移动后按易伤计算受击（20→40）并提高防御阈值；
  出生保护期间免疫爆炸/辣椒等环境伤害，可不因危险格放弃输出，但自身不能 ATTACK。

### 3.5 日志与令牌

令牌只从环境变量 `RMTC_TEAM_TOKEN` 读取，日志过滤器会把令牌替换为 `***`，
不写入公开日志或仓库。每条动作记录 Tick、session、动作、方向、原因与拒绝情况。

### 3.6 线程模型

- 主线程（`rclcpp::spin`）完成感知解析、状态更新、决策、动作发布与回执处理，单线程串行保证同 Tick 顺序。
- AI 调用在独立 `std::thread` 中执行，`libcurl` 带超时；结果写入互斥保护的队列，由下一帧主线程取用。
- worker 以 `shared_ptr<std::atomic<bool>>` 标记完成，主线程 `reap_workers()` 回收并 `join`，析构时统一等待退出。
- 动作发送、事件缓存、配额状态均带互斥保护；AI 结果应用前再次校验 `session_id`/Tick 时效。

## 4. 运行方式

```bash
cd /home/wcy/xns/rmtc_bot_ws
source /opt/ros/humble/setup.bash
colcon build --packages-select rmtc_bot --cmake-args -DCMAKE_BUILD_TYPE=Release
source install/setup.bash

export ROS_DOMAIN_ID=27
export ROS_LOCALHOST_ONLY=0
export RMTC_TEAM_TOKEN="<本场席位令牌>"
export RMTC_AI_BASE_URL="http://<网关地址>:8000"

ros2 launch rmtc_bot stage2.launch.py team:=A team_name:=队伍名称
# 或
./run_contestant.sh A 队伍名称 <网关地址>
```

构建依赖系统包 `libjsoncpp-dev`、`libyaml-cpp-dev`、`libcurl4-openssl-dev`（Ubuntu 22.04 默认可用）。

首次在新机器上使用，先运行自动配置脚本（安装缺失依赖、构建并跑单测）：

```bash
./setup_env.sh              # 自动安装系统依赖并构建、测试
./setup_env.sh --no-apt     # 无法使用 sudo / 依赖已就绪时
./setup_env.sh --skip-test  # 只构建，不跑单测
```

现场切换策略只需修改 `src/rmtc_bot/config/decision_config.json` 的 `decision_type`。
`params.yaml` 为赛前固定参数，现场不得修改。

### 4.1 公开测试端（测试裁判）联调

按《选手测试裁判使用教程》，测试端 AI 完整地址为
`http://127.0.0.1:8080/v1/chat/completions`，测试令牌为 `practice-A/B/C/D`，
`ROS_DOMAIN_ID=27`，`RMW_IMPLEMENTATION=rmw_fastrtps_cpp`（`run_contestant.sh` 已默认设置）。

第一阶段（仅 A 席位，100 ms/Tick）：

```bash
export RMTC_TEAM_TOKEN=practice-A
./run_contestant.sh A '队伍名称' http://127.0.0.1:8080/v1/chat/completions 1
```

第二阶段（500 ms/Tick）：

```bash
export RMTC_TEAM_TOKEN=practice-A
./run_contestant.sh A '队伍名称' http://127.0.0.1:8080/v1/chat/completions 2
```

也可只写 `127.0.0.1:8080`（脚本只补 `http://`），或预先
`export RMTC_AI_BASE_URL=http://127.0.0.1:8080/v1/chat/completions`。
B/C/D 席位须同时替换席位参数、话题与 `practice-B/C/D` 令牌。
重开场次后程序会自动按新 `session_id` 清空缓存；暂停恢复重发同一 Tick 时只重发缓存动作。

## 5. 测试与验证

- 构建：`colcon build --packages-select rmtc_bot --cmake-args -DCMAKE_BUILD_TYPE=Release` 通过。
- 单元测试：`colcon test --packages-select rmtc_bot && colcon test-result --verbose`，
  7 个 gtest 目标共 147 项全部通过，覆盖相对坐标、感知解析、动作校验、规划与战斗规则、
  事件去重、LLM 输出校验、配额与本地降级、第二阶段前后期战术（资源竞争判定、低血防御、
  LLM 策略偏置、撤离粘性、临时障碍失效、巡逻不折返与防绕圈），以及 v1.1 新增规则：
  死亡惩罚下的换命判定、出生保护不能攻击且免疫环境伤害、潘潘的神双向易伤、洁身自好跳过防御目标、
  世界分裂提前离开十字线、经济收益归零后立刻转入输出期。
- 第二阶段练习赛（v1.1 裁判 `dist`，18×18、4 席、500 Tick、50 ms/Tick）：
  A 队 500 个动作中 0 次 WAIT、0 次无效动作、0 次阵亡（196 次攻击、305 次移动）；
  三项任务全部完成，资源 200/200；有效伤害 6240（对抗项上限 500 早已拿满），
  结算 60.0 分（资源 30 + 对抗 15 + 任务 15）。两次重开结果一致。
  注意：练习陪练车全程不主动攻击，因此该局主要验证输出与任务完成度；
  生存相关的换命判定、易伤与出生保护由单元测试覆盖。
- 练习环境的随机事件只以 `event_text` 下发，需经统一 LLM 解析后才进入本地事件表；
  模拟 AI 返回固定响应，不会真正理解事件，因此模拟局中世界分裂等事件无法提前规避，
  需用 `--ai-mode real` 联调验证。这与“事件必须交给 LLM 理解、禁止本地文本解析”的规则一致。
- 地图尺寸完全按感知 `map.width/height` 自适应，v1.1 第二阶段 24×24 与练习 18×18 均无需改代码。
- 第一阶段离线回放：20 张 20×20、26 个障碍、16 个资源的随机地图，100 Tick 内平均资源
  价值 197.5/200（优化前 191.0），17/20 张地图收满 200；公开练习地图稳定 200/200。
- 冒烟：启动 `main_node` 后向 `/team_A/perception` 发布单帧感知，
  `/team_A/action` 返回 `{"action":"move","direction":"left","session_id":"smoke-1","tick":1}`，
  `tick`/`session_id` 回填正确；`./run_contestant.sh A RMTC-Bot 127.0.0.1` 可正常启动
  （无令牌时如预期告警并关闭 AI）。
- 本机模拟 AI 网关：成功返回结构化事件后节点应用事件并继续行动，验证了异步调用与校验链路。

尚未在真实裁判环境验证，需按主办方联调窗口完成感知/动作/AI 三类回执确认。

## 6. 风险与处理

| 风险 | 处理 |
| --- | --- |
| 相对图被误当合并视野 | 每实体独立解析，仅以 `2` 定位 |
| 用 last_action 累计位置 | 位置只信 `self.position` |
| 正则/关键词解析事件 | 事件语义全部交给 LLM，本地仅缓存去重 |
| AI 阻塞动作窗口 | 子线程异步调用 + 超时 + 本地降级 |
| 阵亡误发动作 | 安全预检强制 WAIT |
| 现场改源码/参数 | 仅允许修改 `decision_type`，其余字段拒绝 |
| 第二阶段无 AI 成功调用 | 启动后首个 Tick 即发起调用并记录成功标志 |
| 世界分裂/爆炸致死 | 事件窗口内禁止进入十字线，危险区优先撤离 |
