extends SceneTree
## 压力冒烟（TS-§3 scenarios）：4 个场景各 20s 卡死检测——
## 1) 正常生活流（巡逻/逛街/往返）；2) 单次枪声连锁；3) 连续放置木箱挡路；
## 4) 玩家靠近 NPC 触发问候 + 移动。任何 NPC 连续 4 次采样（≈12s）位置与意图
## 均不变 → 判定卡死，退出码 1。
## 运行：godot --headless --path game_adapter/godot_demo --script res://scripts/smoke_stress.gd \
##       --fixed-fps 60 -- --config res://assets/npcs/sample_town.json
var main: Node
var frames := 0
var samples := 0
var prev: Dictionary = {}
var stuck: Array = []
var scene := 1

func _init() -> void:
	main = load("res://scenes/main.tscn").instantiate()
	root.add_child(main)

func _process(_delta: float) -> bool:
	frames += 1
	# 场景切换（每 1200 帧 ≈ 20s）
	if frames == 1200 and scene == 1:
		scene = 2
		main.inject_gunshot()  # 场景 2：单次枪声
	if frames == 2400 and scene == 2:
		scene = 3
		# 场景 3：玩家在街区空地连放 3 个木箱（不封主干道），验证 NPC 绕行
		var p = main.get_node("Player")
		p.position = Vector2(640.0 + (-2.4) * 100.0, 360.0 + 0.3 * 100.0)
		for i in range(3):
			main.place_obstacle(1.0, 0.0)
		p.position = Vector2(640.0 + 3.6 * 100.0, 360.0)
	if frames == 3600 and scene == 3:
		scene = 4
		# 场景 4：玩家靠近平民触发问候
		var cpos: Vector2 = main.get_node("civilian").position
		main.get_node("Player").position = cpos + Vector2(80.0, 0.0)
	# 场景 4 玩家绕圈
	if scene == 4 and frames % 30 == 0 and frames > 3600:
		var p = main.get_node("Player")
		var cpos: Vector2 = main.get_node("civilian").position
		var ang: float = frames * 0.1
		p.position = cpos + Vector2(cos(ang) * 60.0, sin(ang) * 60.0)
	if frames % 180 != 0:
		return false
	var label := String(main.get_node("DebugLabel").text)
	var lines := {}
	for line in label.split("\n"):
		for prefix in ["守卫:", "支援兵:", "平民:"]:
			if line.begins_with(prefix):
				lines[prefix] = line
	var names := {"守卫:": "guard", "支援兵:": "medic", "平民:": "civilian"}
	for name in names:
		var pos: Vector2 = main.get_node(names[name]).position
		var intent: String = lines.get(name, "???")
		var key := "%s@%s" % [pos.x, pos.y]
		if prev.has(name) and prev[name].has("key") and prev[name]["key"] == key and prev[name].has("intent") and prev[name]["intent"] == intent:
			var n: int = prev[name].get("cnt", 0) + 1
			prev[name]["cnt"] = n
			if n >= 4 and name not in stuck:
				stuck.append(name)
				print("[smoke-stress] 场景%d: %s 卡住 @ %.0f,%.0f intent=%s" % [scene, name, pos.x, pos.y, intent])
		else:
			prev[name] = {"key": key, "intent": intent, "cnt": 0}
	samples += 1
	if samples >= 28:  # 场景 4 到 80s 结束
		print("[smoke-stress] 完成 stuck=%s" % [stuck])
		quit(0 if stuck.is_empty() else 1)
	return false
