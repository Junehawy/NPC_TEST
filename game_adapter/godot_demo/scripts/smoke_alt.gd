extends SceneTree
## 备选配置无头冒烟（TS-§3 scenarios 行，配置驱动）：
## 用 --config 加载 sample_guard_alt.json（快巡逻/短警戒/自定义问候语/800×450 窗口），
## 按配置动态计算像素坐标，校验四条行为链路——
##   1) 初始巡逻（MoveIntent）；2) 靠近 → 问候（SayIntent 含配置文案"哨兵"）；
##   3) 枪声 → 惊吓（startled）；4) 警戒期内远离 → 持续警戒。
## 运行：godot --headless --path game_adapter/godot_demo --script res://scripts/smoke_alt.gd \
##       --fixed-fps 60 -- --config res://assets/npcs/sample_guard_alt.json
## 退出码：0 = 通过，1 = 链路未触发。

const CONFIG_PATH := "res://assets/npcs/sample_guard_alt.json"

var main: Node
var frames := 0
var near_pos := Vector2.ZERO
var far_pos := Vector2.ZERO
var patrol_snapshots: Array[String] = []
var greet_snapshots: Array[String] = []
var startle_snapshots: Array[String] = []
var alert_snapshots: Array[String] = []

func _init() -> void:
	var text := FileAccess.get_file_as_string(CONFIG_PATH)
	var cfg: Dictionary = JSON.parse_string(text)
	var scene: Dictionary = cfg["extra"]["demo"]["scene"]
	var w: float = scene["window_width"]
	var h: float = scene["window_height"]
	var scale: float = scene["scale"]
	var spawn: Array = scene["player_spawn"]
	# 像素换算与演示节点一致：世界原点 = 窗口中心，1 单位 = scale 像素。
	near_pos = Vector2(w / 2.0 + 0.1 * scale, h / 2.0)
	far_pos = Vector2(w / 2.0 + spawn[0] * scale, h / 2.0 + spawn[1] * scale)
	main = load("res://scenes/main.tscn").instantiate()
	root.add_child(main)

func _process(_delta: float) -> bool:
	frames += 1
	var label := String(main.get_node("DebugLabel").text)
	# 阶段 1（快巡逻：walk 1.0s）：玩家在出生点（3.5 单位 > 感知半径 2.0）。
	if frames >= 10 and frames <= 40:
		patrol_snapshots.append(label)
	# 阶段 2：玩家贴近 NPC → 巡逻让位 → 问候（断言配置文案生效）。
	if frames == 60:
		main.get_node("Player").position = near_pos
	if frames >= 61 and frames <= 140:
		greet_snapshots.append(label)
	# 阶段 3：枪声 → 惊吓（单 tick 意图，逐帧快照捕获）。
	if frames == 150:
		main.inject_gunshot()
	if frames >= 151 and frames <= 170:
		startle_snapshots.append(label)
	# 阶段 4：玩家远离，警戒 1.5s（90 帧，至 240）内持续"警戒"表情。
	if frames == 180:
		main.get_node("Player").position = far_pos
	if frames >= 181 and frames <= 239:
		alert_snapshots.append(label)
	if frames == 240:
		var ok_patrol := _any_contains(patrol_snapshots, "MoveIntent")
		var ok_greet := _any_contains(greet_snapshots, "哨兵")
		var ok_startle := _any_contains(startle_snapshots, "startled")
		var ok_alert := _any_contains(alert_snapshots, "警戒")
		if ok_patrol and ok_greet and ok_startle and ok_alert:
			print("[smoke-alt] PASS patrol=%s greet=%s startle=%s alert=%s"
					% [ok_patrol, ok_greet, ok_startle, ok_alert])
			quit(0)
		else:
			printerr("[smoke-alt] FAIL patrol=%s greet=%s startle=%s alert=%s"
					% [ok_patrol, ok_greet, ok_startle, ok_alert])
			printerr("[smoke-alt] 当前面板: %s" % [label])
			quit(1)
	return false

func _any_contains(snapshots: Array[String], needle: String) -> bool:
	for snapshot in snapshots:
		if needle in snapshot:
			return true
	return false
