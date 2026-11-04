# Umbriel Configuration — Persona 5 Notifications & Futaba Music Widget

File [`config.toml`](file:///home/okarin/Projects/Personal/Persona%205/Persona-rice-main/umbriel/config.toml) chỉ cấu hình đúng 2 phần cần thiết cho **Thông báo Persona 5 (`p5notify`)** và **Widget Âm nhạc Futaba (`personaMusic`)**, **không định nghĩa bảng `[keybinds]`** để giữ nguyên 100% các phím tắt mặc định/cá nhân của Umbriel:

1. **Autostart (`[general]`)**: Tự khởi động `noctalia` và `p5-rice-daemon`.
2. **Layer Rule (`[[layer_rule]]`)**: Tắt blur của compositor cho layer `noctalia-p5notify` và `noctalia-desktop-widget-personaMusic` để giữ nguyên góc nghiêng comic trong suốt của Cairo.

## Tích hợp vào Umbriel hiện có của bạn

Bạn chỉ cần thêm `p5-rice-daemon` vào `autostart` và thêm `[[layer_rule]]` vào `~/.config/umbriel/config.toml`:

```toml
[[layer_rule]]
match.namespace = "^noctalia-(p5notify|desktop-widget-personaMusic)$"
blur = false
```
