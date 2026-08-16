extends SceneTree
## 无头冒烟测试（TS-§3 scenarios 行）：加载主场景，校验四条行为链路——
##   1) 初始巡逻（玩家远离，MoveIntent 权威）；
##   2) 玩家靠近 → 巡逻决策器让位 → 问候（SayIntent）；
##   3) 枪声 → 惊吓（EmoteIntent → startled）；
##   4) 警戒期内玩家远离 → 持续警戒（EmoteIntent → 警戒）。
## 运行：godot --headless --path game_adapter/godot_demo --script res://scripts/smoke_test.gd --fixed-fps 60
## 退出码：0 = 通过，1 = 链路未触发。

var main: Node
var frames := 0
var patrol_snapshots: Array[String] = []
var greet_snapshots: Array[String] = []
var startle_snapshots: Array[String] = []
var alert_snapshots: Array[String] = []

func _init() -> void:
	main = load("res://scenes/main.tscn").instantiate()
	root.add_child(main)

func _process(_delta: float) -> bool:
	frames += 1
	var label := String(main.get_node("DebugLabel").text)
	# 阶段 1（0~60 帧）：玩家在出生点（约 3.5 单位 > 感知半径 2.0）→ NPC 应巡逻。
	if frames >= 30 and frames <= 50:
		patrol_snapshots.append(label)
	# 阶段 2（60 帧起）：玩家贴近 NPC（世界 ≈ (0.1, 0)）→ 巡逻让位 → 问候。
	if frames == 60:
		main.get_node("Player").position = Vector2(490.0, 270.0)
	if frames >= 61 and frames <= 140:
		greet_snapshots.append(label)
	# 阶段 3（150 帧）：注入枪声 → 惊吓（单 tick 意图，逐帧快照捕获）。
	if frames == 150:
		main.inject_gunshot()
	if frames >= 151 and frames <= 170:
		startle_snapshots.append(label)
	# 阶段 4（180 帧起）：玩家远离，警戒期内应持续"警戒"表情。
	if frames == 180:
		main.get_node("Player").position = Vector2(830.0, 270.0)
	if frames >= 181 and frames <= 299:
		alert_snapshots.append(label)
	if frames == 300:
		var ok_patrol := _any_contains(patrol_snapshots, "MoveIntent")
		var ok_greet := _any_contains(greet_snapshots, "SayIntent")
		var ok_startle := _any_contains(startle_snapshots, "startled")
		var ok_alert := _any_contains(alert_snapshots, "警戒")
		if ok_patrol and ok_greet and ok_startle and ok_alert:
			print("[smoke] PASS patrol=%s greet=%s startle=%s alert=%s"
					% [ok_patrol, ok_greet, ok_startle, ok_alert])
			quit(0)
		else:
			printerr("[smoke] FAIL patrol=%s greet=%s startle=%s alert=%s"
					% [ok_patrol, ok_greet, ok_startle, ok_alert])
			printerr("[smoke] 当前面板: %s" % [label])
			quit(1)
	return false

func _any_contains(snapshots: Array[String], needle: String) -> bool:
	for snapshot in snapshots:
		if needle in snapshot:
			return true
	return false
