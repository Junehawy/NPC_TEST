extends Node2D
## 玩家控制（GDScript ↔ C++ 扩展边界示例）：
## WASD 移动；空格触发枪声——跨语言调用 C++ 扩展节点 Main 的 inject_gunshot()。
## speed / clamp_margin / clamp_size 由 C++ 侧按 DemoConfig（extra.demo.player /
## scene 窗口尺寸）注入，默认值兜底。clamp_size 用配置值而非视口：
## 无头模式首帧视口为未初始化的 64×64，瞬时读视口会把玩家错误钳到角落。

var speed: float = 400.0       # 像素/秒
var clamp_margin: float = 24.0 # 窗口边界钳制边距（像素）
var clamp_size: Vector2 = Vector2(960.0, 540.0) # 钳制区域（窗口尺寸，配置注入）

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
		position += dir.normalized() * speed * delta
	position.x = clampf(position.x, clamp_margin, clamp_size.x - clamp_margin)
	position.y = clampf(position.y, clamp_margin, clamp_size.y - clamp_margin)

func _unhandled_input(event: InputEvent) -> void:
	if event is InputEventKey and event.pressed and event.physical_keycode == KEY_SPACE:
		# 经 group 定位演示节点（主场景直接运行与 autopilot 嵌套场景均适用）。
		get_tree().get_first_node_in_group("npc_demo").inject_gunshot()
