extends SceneTree
## 多 NPC 场景无头冒烟（R7-11 连锁反应，TS-§3 scenarios 行）：加载主场景（--config 指向
## sample_town.json），校验枪声触发的三 NPC 连锁时间线——
##   1) 初始：守卫巡逻（guard: MoveIntent）；
##   2) 枪声 → 守卫警戒 + 平民逃窜（逃往 -4.5,-1.8）+ 支援兵隐蔽；
##   3) 守卫呼叫支援台词 → 吼叫传播 → 支援兵响应（跑向集合点 1.0,0.6）；
##   4) 守卫搜寻（MoveIntent → 3.0）；
##   5) 搜寻结束靠近玩家 → 守卫问候（你好）。
## 运行：godot --headless --path game_adapter/godot_demo --script res://scripts/smoke_town.gd \
##       --fixed-fps 60 -- --config res://assets/npcs/sample_town.json
## 退出码：0 = 通过，1 = 链路未触发。

var main: Node
var frames := 0
var patrol_snapshots: Array[String] = []
var gunshot_snapshots: Array[String] = []
var shout_snapshots: Array[String] = []
var search_snapshots: Array[String] = []
var greet_snapshots: Array[String] = []
var reshot_snapshots: Array[String] = []

func _init() -> void:
	main = load("res://scenes/main.tscn").instantiate()
	root.add_child(main)

func _process(_delta: float) -> bool:
	frames += 1
	var label := String(main.get_node("DebugLabel").text)
	# 阶段 1（0.5~1.3s）：守卫巡逻
	if frames >= 30 and frames <= 80:
		patrol_snapshots.append(label)
	# 阶段 2（2.0s 枪声 → 2.7s 前）：守卫警戒 / 平民逃窜 / 支援兵隐蔽
	if frames == 120:
		main.inject_gunshot()
	if frames >= 121 and frames <= 160:
		gunshot_snapshots.append(label)
	# 阶段 3（守卫呼叫支援 ~3.2s → 吼叫 → 支援兵响应）
	if frames >= 190 and frames <= 240:
		shout_snapshots.append(label)
	# 阶段 4（守卫搜寻 ~4.7s → 3.5s 相位）
	if frames >= 280 and frames <= 400:
		search_snapshots.append(label)
	# 阶段 5（搜寻结束 ~8.2s → 回 idle → 玩家近距 → 问候）
	if frames >= 495 and frames <= 580:
		greet_snapshots.append(label)
	# 阶段 6（二次开枪回归，R7-12）：问候/响应状态必须可被枪声打断——
	# 守卫重入警戒、平民再次逃窜、支援兵再次隐蔽。
	if frames == 600:
		main.inject_gunshot()
	if frames >= 601 and frames <= 640:
		reshot_snapshots.append(label)
	if frames == 640:
		var ok_patrol := _any_contains(patrol_snapshots, "guard: MoveIntent")
		var ok_alert := _any_contains(gunshot_snapshots, "guard: EmoteIntent → startled")
		var ok_flee := _any_contains(gunshot_snapshots, "civilian: MoveIntent → (-4.500000")
		var ok_crouch := _any_contains(gunshot_snapshots, "medic: EmoteIntent")
		var ok_call := _any_contains(shout_snapshots, "guard: SayIntent → \"呼叫支援")
		var ok_respond := _any_contains(shout_snapshots, "medic: MoveIntent → (1.000000")
		var ok_search := _any_contains(search_snapshots, "guard: MoveIntent → (2.200000")
		var ok_greet := _any_contains(greet_snapshots, "guard: SayIntent → \"你好")
		var ok_reshot_alert := _any_contains(reshot_snapshots, "guard: EmoteIntent → startled")
		var ok_reshot_flee := _any_contains(reshot_snapshots, "civilian: MoveIntent → (-4.500000")
		var ok_reshot_crouch := _any_contains(reshot_snapshots, "medic: EmoteIntent")
		if ok_patrol and ok_alert and ok_flee and ok_crouch and ok_call and ok_respond \
				and ok_search and ok_greet and ok_reshot_alert and ok_reshot_flee \
				and ok_reshot_crouch:
			print("[smoke-town] PASS patrol=%s alert=%s flee=%s crouch=%s call=%s respond=%s search=%s greet=%s reshot_alert=%s reshot_flee=%s reshot_crouch=%s"
					% [ok_patrol, ok_alert, ok_flee, ok_crouch, ok_call, ok_respond, ok_search, ok_greet, ok_reshot_alert, ok_reshot_flee, ok_reshot_crouch])
			quit(0)
		else:
			printerr("[smoke-town] FAIL patrol=%s alert=%s flee=%s crouch=%s call=%s respond=%s search=%s greet=%s reshot_alert=%s reshot_flee=%s reshot_crouch=%s"
					% [ok_patrol, ok_alert, ok_flee, ok_crouch, ok_call, ok_respond, ok_search, ok_greet, ok_reshot_alert, ok_reshot_flee, ok_reshot_crouch])
			printerr("[smoke-town] 当前面板: %s" % [label])
			quit(1)
	return false

func _any_contains(snapshots: Array[String], needle: String) -> bool:
	for snapshot in snapshots:
		if needle in snapshot:
			return true
	return false
