# Umbriel Configuration for Persona 5 Notifications & Music Widget

File [`config.toml`](file:///home/okarin/Projects/Personal/Persona%205/Persona-rice-main/umbriel/config.toml) chỉ giữ lại đúng các cấu hình phục vụ cho **Persona 5 Notification Banners (`p5notify`)** và **Futaba Music Widget (`personaMusic`)** trên Umbriel:

1. **`[general]`**: Tự động khởi chạy `noctalia` và daemon C++ `p5-rice-daemon`.
2. **`[[layer_rule]]`**: Rule cho 2 layer surface `noctalia-p5notify` và `noctalia-desktop-widget-personaMusic`.
3. **`[keybinds]`**: Chỉ giữ các phím tắt điều khiển thông báo P5 và trình phát nhạc:
   - `Mod+Shift+M`: Chuyển đổi profile (`p5-profile-switcher --cycle`)
   - `Mod+Shift+N`: Xóa nhanh các banner thông báo P5 đang hiển thị (`p5-rice-ctl dismiss-all`)
   - `XF86AudioPlay` / `XF86AudioNext` / `XF86AudioPrev`: Điều khiển phát nhạc MPRIS hiển thị trên widget Futaba.
