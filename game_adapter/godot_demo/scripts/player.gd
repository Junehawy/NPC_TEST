extends Node2D
## 玩家控制（GDScript ↔ C++ 扩展边界示例）：
## WASD 移动；空格触发枪声——跨语言调用 C++ 扩展节点 Main 的 inject_gunshot()。

const SPEED := 400.0 # 像素/秒（世界单位 × 100）

func _process(delta: float) -> void:
	var dir := Vector2.ZERO
	if Input.is_physical_key_pressed(KEY_W):
		dir.y -= 1.0
	if Input.is_physical_key_pressed(KEY_S):
		dir.y += 1.0
	if Input.is_physical_key_pressed(KEY_A):
		dir.x -= 1.0
	if Input.is_physical_key_pressed(KEY_D):
		dir.x += 1.0
	if dir != Vector2.ZERO:
		position += dir.normalized() * SPEED * delta
	# 钳制在窗口内（960×540，留 24px 边距；窗口尺寸改动需同步）
	position.x = clampf(position.x, 24.0, 936.0)
	position.y = clampf(position.y, 24.0, 516.0)

func _unhandled_input(event: InputEvent) -> void:
	if event is InputEventKey and event.pressed and event.physical_keycode == KEY_SPACE:
		# 经 group 定位演示节点（主场景直接运行与 autopilot 嵌套场景均适用）。
		get_tree().get_first_node_in_group("npc_demo").inject_gunshot()
