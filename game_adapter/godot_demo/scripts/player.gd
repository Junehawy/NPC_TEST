extends Node2D
## 玩家控制（GDScript ↔ C++ 扩展边界示例）：
## WASD 移动；空格触发枪声——跨语言调用 C++ 扩展节点 Main 的 inject_gunshot()。
## speed / clamp_margin 由 C++ 侧按 DemoConfig（extra.demo.player）注入，默认值兜底。

var speed: float = 400.0      # 像素/秒
var clamp_margin: float = 24.0 # 窗口边界钳制边距（像素）

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
	# 钳制在窗口内（窗口尺寸来自配置，运行时取视口大小，改动无需同步）
	var vp := get_viewport_rect().size
	position.x = clampf(position.x, clamp_margin, vp.x - clamp_margin)
	position.y = clampf(position.y, clamp_margin, vp.y - clamp_margin)

func _unhandled_input(event: InputEvent) -> void:
	if event is InputEventKey and event.pressed and event.physical_keycode == KEY_SPACE:
		# 经 group 定位演示节点（主场景直接运行与 autopilot 嵌套场景均适用）。
		get_tree().get_first_node_in_group("npc_demo").inject_gunshot()
