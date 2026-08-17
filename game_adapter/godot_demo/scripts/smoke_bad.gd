extends SceneTree
## 负向冒烟（fail-fast 路径，TS-§5）：非法配置（extra.demo 含未知键）必须拒绝装配——
## 演示节点 ready_ 置 false、不驱动 tick，调试面板保持空白。
## 运行：godot --headless --path game_adapter/godot_demo --script res://scripts/smoke_bad.gd \
##       --fixed-fps 60 -- --config res://assets/npcs/sample_guard_bad.json
## 退出码：0 = 通过（已拒绝装配），1 = 非法配置被放行。

var main: Node
var frames := 0

func _init() -> void:
	main = load("res://scenes/main.tscn").instantiate()
	root.add_child(main)

func _process(_delta: float) -> bool:
	frames += 1
	if frames == 30:
		# fail-fast 语义：装配失败 → 场景树未构建 → DebugLabel 不存在或保持空白。
		var label_node: Node = main.get_node_or_null("DebugLabel")
		var label := String(label_node.text) if label_node != null else ""
		if label == "":
			print("[smoke-bad] PASS 非法配置已拒绝装配（fail-fast）")
			quit(0)
		else:
			printerr("[smoke-bad] FAIL 非法配置被放行，面板非空: %s" % [label])
			quit(1)
	return false
