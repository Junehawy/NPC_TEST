extends SceneTree
## 阶段 2 FSM 模式无头冒烟（TS-§3 scenarios 行）：加载主场景（--config 指向
## sample_guard_fsm.json），校验框架 FSM + 感知模块驱动的时间线——
##   1) 初始 idle（无意图）；2) 枪声 → 警戒（startled）；3) 1.2s 后呼叫支援（台词）；
##   4) 1.5s 后搜寻（MoveIntent）；5) 搜寻结束且感知窗过期后 → 靠近玩家触发问候（你好）。
## 运行：godot --headless --path game_adapter/godot_demo --script res://scripts/smoke_fsm.gd \
##       --fixed-fps 60 -- --config res://assets/npcs/sample_guard_fsm.json
## 退出码：0 = 通过，1 = 链路未触发。

var main: Node
var frames := 0
var idle_snapshots: Array[String] = []
var alert_snapshots: Array[String] = []
var call_snapshots: Array[String] = []
var search_snapshots: Array[String] = []
var greet_snapshots: Array[String] = []

func _init() -> void:
	main = load("res://scenes/main.tscn").instantiate()
	root.add_child(main)

func _process(_delta: float) -> bool:
	frames += 1
	var label := String(main.get_node("DebugLabel").text)
	# 阶段 1：初始 idle（无权威意图）
	if frames >= 10 and frames <= 40:
		idle_snapshots.append(label)
	# 阶段 2：枪声 → 感知旗标 → FSM 警戒
	if frames == 60:
		main.inject_gunshot()
	if frames >= 61 and frames <= 90:
		alert_snapshots.append(label)
	# 阶段 3：警戒 1.2s（72 帧）后呼叫支援
	if frames >= 130 and frames <= 160:
		call_snapshots.append(label)
	# 阶段 4：呼叫 1.5s（90 帧）后搜寻
	if frames >= 220 and frames <= 260:
		search_snapshots.append(label)
	# 阶段 5：搜寻 2.0s + 感知窗过期后 → 玩家近距 → 问候
	if frames >= 344 and frames <= 420:
		greet_snapshots.append(label)
	if frames == 420:
		var ok_idle := _any_contains(idle_snapshots, "无意图")
		var ok_alert := _any_contains(alert_snapshots, "startled")
		var ok_call := _any_contains(call_snapshots, "呼叫支援")
		var ok_search := _any_contains(search_snapshots, "MoveIntent")
		var ok_greet := _any_contains(greet_snapshots, "你好")
		if ok_idle and ok_alert and ok_call and ok_search and ok_greet:
			print("[smoke-fsm] PASS idle=%s alert=%s call=%s search=%s greet=%s"
					% [ok_idle, ok_alert, ok_call, ok_search, ok_greet])
			quit(0)
		else:
			printerr("[smoke-fsm] FAIL idle=%s alert=%s call=%s search=%s greet=%s"
					% [ok_idle, ok_alert, ok_call, ok_search, ok_greet])
			printerr("[smoke-fsm] 当前面板: %s" % [label])
			quit(1)
	return false

func _any_contains(snapshots: Array[String], needle: String) -> bool:
	for snapshot in snapshots:
		if needle in snapshot:
			return true
	return false
