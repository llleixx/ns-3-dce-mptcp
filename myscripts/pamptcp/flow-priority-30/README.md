# Flow Priority 30-Client Example

这个目录现在按“一个目录就是一个实验场景”组织，和 `myscripts/pamptcp/设计.md` 保持一致。

## 场景目录

- `avg-only/`
- `p1-peak-p3-sub-peak/`
- `p3-6peak-x6/`
- `p3-3peak-x6/`
- `p3-peak/`
- `p3-sub-peak/`
- `p23-peak/`

其中：

- `p1-peak-p3-sub-peak/`
  - 1 个 P1 设备使用 `20 Mbps` 峰值模板，其余 P1/P2 保持稳态，P3 中 10 个设备保持 `25 Mbps` 峰值、3 个设备保持稳态。
  - 这个场景比 `p3-sub-peak/` 更容易解释，因为它明确回答的是“当一个高优先级设备自己也出现突发时，scheduler 能否仍然保护高优先级尾时延，同时不牺牲低优 bulk 吞吐”。
- `p3-3peak-x6/`
  - 所有 steady/peak 模板速率都放大到原来的 `6x`，其中只有 3 台 P3 设备使用峰值模板，其余设备保持对应的 `6x` steady 模板。
  - 这个场景的目的不是增加更多峰值设备，而是把“少量低优峰值 + 全局更高基线负载”叠在一起，观察 scheduler 是否仍能稳住高优先级时延尾部。
- `p3-6peak-x6/`
  - 沿用 `6x` 模板，但把 `P3` 设备改成 7 台 steady、6 台 peak。
  - 这个场景用于观察：当高强度 `P3 peak` 数量从 3 台增加到 6 台时，scheduler 是否开始明显劣化。

每个子目录都是一个完整场景，至少包含：

- `templates.csv`
- `groups.csv` 或 `clients.csv`

## 模板字段

`templates.csv` 的字段尽量直接对应 `pamptcp.cc` 的应用参数：

- `template_id`
- `priority`
- `appSteadyRate`
- `appBurstRate`
- `appTrafficModel`
- `appSteadyTime`
- `appBurstTime`
- `appBurstProb`
- `appStateInterval`
- `appInitialSendDelay`
- `appPacketSize`

说明：

- 当前 `avg-only / p3-peak / p23-peak` 主要使用 `appSteadyRate` 和 `appBurstRate`。
- 之所以把 `appSteadyTime / appBurstTime` 等字段也保留在模板里，是为了后续直接扩展到更真实的 burst 场景，而不是再额外引入一层“实验模式映射”。
- 旧格式里的 `avg_rate / peak_rate / packet_size / initial_send_delay_s` 仍然兼容，但新的推荐写法是使用 `appSteadyRate / appBurstRate / appPacketSize / appInitialSendDelay`。

## Client 分配

`groups.csv` 使用：

- `template_id`
- `count`

这里允许同一个 `template_id` 在多行重复出现。这样不仅能表示数量，还能通过多行顺序控制不同优先级设备的排列顺序。

如果你想为每个 client 单独指定详细业务，可以改用 `clients.csv`：

- `client_id`
- `template_id`

或者直接把单个 client 的业务参数写在 `clients.csv` 里：

- `client_id`
- `priority`
- `appSteadyRate`
- `appBurstRate`
- 以及其他 `app*` 字段

此时 `template_id` 可以省略，适合每个设备都不同的情况。
