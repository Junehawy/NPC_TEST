extends Node
## 视觉验收自动演示：固定时间线驱动玩家（追上 NPC → 停下问候 → 开枪），
## 供外部截屏/录屏验证 巡逻/问候/惊吓 三时刻效果（配合 scenes/main_autopilot.tscn）。
## 运行：godot --path game_adapter/godot_demo scenes/main_autopilot.tscn
## 时间线：0~2.5s 巡逻 → 2.5s 起玩家追击 NPC 至 150px 内停下 → 问候 → 6.0s 枪声 → 惊吓/警戒。

var main: Node
var t := 0.0
var reached := false
var fired := false

func _ready() -> void:
	main = load("res://scenes/main.tscn").instantiate()
	add_child(main)

func _process(delta: float) -> void:
	t += delta
	var player := main.get_node("Player") as Node2D
	var npc := main.get_node("Npc") as Node2D
	if t > 2.5 and not reached:
		# 追击 NPC：距离收敛到 150px（1.5 单位 < 感知半径 2.0）即停下。
		if player.position.x - npc.position.x > 150.0:
			player.position.x -= 400.0 * delta
		else:
			reached = true
	if t >= 6.0 and not fired:
		main.inject_gunshot()
		fired = true
