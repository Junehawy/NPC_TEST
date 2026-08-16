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

func _unhandled_input(event: InputEvent) -> void:
	if event is InputEventKey and event.pressed and event.physical_keycode == KEY_SPACE:
		get_node("/root/Main").inject_gunshot()
