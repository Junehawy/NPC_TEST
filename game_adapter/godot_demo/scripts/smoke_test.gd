extends SceneTree
## 无头冒烟测试（TS-§3 scenarios 行）：加载主场景，校验两条行为链路——
##   1) 枪声刺激 → 黑板 alarm（决策器 pending）→ NPC 惊吓（EmoteIntent → startled）；
##   2) 警戒期内玩家靠近 → 感知注入 → 问候（SayIntent）持续胜出。
## 运行：godot --headless --path game_adapter/godot_demo --script res://scripts/smoke_test.gd --fixed-fps 60
## 退出码：0 = 通过，1 = 链路未触发。

var main: Node
var frames := 0
var label_snapshots: Array[String] = []

func _init() -> void:
	main = load("res://scenes/main.tscn").instantiate()
	root.add_child(main)

func _process(_delta: float) -> bool:
	frames += 1
	if frames == 60:
		# 玩家贴近 NPC（世界坐标 ≈ (0.1, 0)），并注入枪声。
		main.get_node("Player").position = Vector2(650.0, 360.0)
		main.inject_gunshot()
	# 枪声后窗口内逐帧快照调试面板（惊吓意图只存在单个 tick）。
	if frames >= 60 and frames <= 80:
		label_snapshots.append(String(main.get_node("DebugLabel").text))
	if frames == 200:
		# 警戒 3 秒（180 帧）内检查：此刻问候仍在胜出，惊吓已在快照窗口出现。
		var seen_startle := false
		for snapshot in label_snapshots:
			if "startled" in snapshot:
				seen_startle = true
		var seen_greet: bool = "SayIntent" in String(main.get_node("DebugLabel").text)
		if seen_startle and seen_greet:
			print("[smoke] PASS startle=%s greet=%s" % [seen_startle, seen_greet])
			quit(0)
		else:
			printerr("[smoke] FAIL startle=%s greet=%s" % [seen_startle, seen_greet])
			printerr("[smoke] 当前面板: %s" % [String(main.get_node("DebugLabel").text)])
			quit(1)
	return false
