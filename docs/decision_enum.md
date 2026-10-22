# 决策类型封存说明

本文件为赛前登记表，比赛现场仅允许修改 `config/decision_config.json` 中的 `decision_type`，
取值必须来自 `config/decision_enum.json`。禁止现场修改源码、算法、Prompt、决策参数或逐 Tick 人工控制。

| decision_type | 作用 | 读取方式 | 阶段限制 |
| --- | --- | --- | --- |
| `stage1_collect` | 第一阶段采集：仅 MOVE/WAIT，优先已知资源，其次探索未知区域 | `decision_config.json` | 第一阶段 |
| `stage2_combat` | 第二阶段综合：资源采集、任务点到达、直线攻击、低血防御、事件规避 | `decision_config.json` | 第二/三阶段 |
| `local_fallback` | 纯本地规则策略，不依赖任何 AI 结果；异常时 WAIT | `decision_config.json` | 第二/三阶段 |

## 生效规则

1. 主节点启动时读取 `decision_type`，并在每次新感知到达时检查文件修改时间，修改后下一 Tick 生效。
2. 文件顶层只允许 `decision_type` 一个字段；出现其它字段直接拒绝并保持原策略。
3. 未登记的取值会被拒绝，节点继续使用上一个合法策略。
4. 第一阶段（stage=1）无论配置为何值，都被强制路由到 `stage1_collect`。
5. 阵亡状态下所有策略统一降级为 WAIT，避免 `E_STATE` 与 `invalid_actions`。
6. 每场最多两次人工切换，须在暂停期间或加赛前向裁判提出并登记。

## 修改示例

```json
{"decision_type": "stage2_combat"}
```

修改后无需重启节点，下一次感知到达时自动生效。
