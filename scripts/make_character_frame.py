#!/usr/bin/env python3
"""
Persona 5 Rice - Character Cut-in Frame Generator
Composites any transparent PNG character portrait (bustup) into the authentic
Persona 5 dialogue / confidant cut-in comic frame style.

Preserves 100% of the authentic Persona 5 frame:
- Outer black polygon frame with left speech notch
- Comic white border stripe
- Inner black ink border lines & speed hatching lines
- Accurate corner angles and dynamic manga cuts
"""

import sys
import argparse
from pathlib import Path
from PIL import Image
import numpy as np

def generate_p5_character_frame(char_path: Path, template_frame_path: Path, out_path: Path, scale=1.62, tilt_deg=-2.5):
    if not char_path.exists():
        print(f"Error: Character file not found: {char_path}")
        sys.exit(1)
    if not template_frame_path.exists():
        print(f"Error: Template frame not found: {template_frame_path}")
        sys.exit(1)

    template = Image.open(template_frame_path).convert("RGBA")
    char_img = Image.open(char_path).convert("RGBA")
    W, H = template.size

    t_arr = np.array(template)
    t_alpha = t_arr[:, :, 3]

    # 1. Prepare clean template by clearing ONLY the interior character area of the template to pure white.
    # All authentic Persona 5 frame borders, corners, notches, white stripes, and speed lines are preserved 100%.
    clean_arr = t_arr.copy()

    for x in range(260, 1010):
        y_top = int(213 - 0.2133 * (x - 250)) + 2
        if x < 600:
            y_bot = int(809 + 0.435 * (x - 250)) + 4
        else:
            y_bot = int(959 - 0.500 * (x - 600)) + 4
        clean_arr[y_top:y_bot, x] = [254, 254, 254, 255]

    # Clear any stray colored pixels inside the interior canvas box
    sat = np.max(clean_arr[:, :, :3].astype(int), axis=-1) - np.min(clean_arr[:, :, :3].astype(int), axis=-1)
    stray = (clean_arr[:, :, 3] > 100) & (sat > 15) & \
            (np.arange(W)[None, :] > 255) & (np.arange(W)[None, :] < 1015) & \
            (np.arange(H)[:, None] > 40) & (np.arange(H)[:, None] < 950)
    clean_arr[stray] = [254, 254, 254, 255]

    clean_frame = Image.fromarray(clean_arr, "RGBA")

    # 2. Scale character portrait
    s_w = int(char_img.width * scale)
    s_h = int(char_img.height * scale)
    char_scaled = char_img.resize((s_w, s_h), Image.LANCZOS)
    if tilt_deg != 0.0:
        char_scaled = char_scaled.rotate(tilt_deg, resample=Image.BICUBIC, expand=True)

    # 3. Create authentic Persona 5 charcoal silhouette drop shadow
    c_arr = np.array(char_scaled)
    shadow_arr = np.zeros_like(c_arr)
    shadow_arr[:, :, :3] = 48  # #303030 charcoal
    shadow_arr[:, :, 3] = (c_arr[:, :, 3] * 0.96).astype(np.uint8)
    char_shadow = Image.fromarray(shadow_arr, "RGBA")

    # Center character nicely inside the frame
    pos_x = int(W * 0.335)
    pos_y = int(H * 0.108)
    shadow_pos_x = pos_x - 42
    shadow_pos_y = pos_y + 12

    # 4. Paste character + shadow onto interior of clean_frame
    char_layer = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    char_layer.paste(char_shadow, (shadow_pos_x, shadow_pos_y), char_shadow)
    char_layer.paste(char_scaled, (pos_x, pos_y), char_scaled)

    composite = Image.alpha_composite(clean_frame, char_layer)
    comp_arr = np.array(composite)

    # 5. Composite authentic foreground frame borders on top so character is clipped authentically:
    # - Left border with speech bubble notch & inner black line (x < 260) 100% bit-exact
    comp_arr[:, :260] = t_arr[:, :260]
    # - All frame lines, speed lines, comic hatchings, and outer black borders from template
    is_black_or_line = (clean_arr[:, :, 3] > 50) & (
        (clean_arr[:, :, 0] < 235) | (clean_arr[:, :, 1] < 235) | (clean_arr[:, :, 2] < 235)
    )
    comp_arr[is_black_or_line] = clean_arr[is_black_or_line]
    # - Outer white stripe on right border
    is_right_white_stripe = (clean_arr[:, :, 3] > 50) & (np.arange(W)[None, :] > 1245)
    comp_arr[is_right_white_stripe] = clean_arr[is_right_white_stripe]

    # Clip outer transparency to match the exact template outline
    comp_arr[:, :, 3] = np.minimum(comp_arr[:, :, 3], t_alpha)

    final_frame = Image.fromarray(comp_arr, "RGBA")
    out_path.parent.mkdir(parents=True, exist_ok=True)
    final_frame.save(out_path)
    print(f"Successfully generated Persona 5 frame at: {out_path}")

def main():
    parser = argparse.ArgumentParser(description="Persona 5 Character Cut-in Frame Generator")
    parser.add_argument("input", type=Path, help="Input character PNG (transparent portrait)")
    parser.add_argument("-o", "--output", type=Path, default=None, help="Output PNG path")
    parser.add_argument("--template", type=Path, default=None, help="Path to futaba_frame.png template")
    parser.add_argument("--scale", type=float, default=1.62, help="Scale multiplier for character (default: 1.62)")
    parser.add_argument("--tilt", type=float, default=-2.5, help="Tilt angle in degrees (default: -2.5)")
    args = parser.parse_args()

    cfg_home = Path.home() / ".config"
    default_template = cfg_home / "noctalia/p5-rice/personaMusic/assets/futaba_frame.png"
    template_path = args.template or default_template
    out_path = args.output or (args.input.parent / f"{args.input.stem}_frame.png")

    generate_p5_character_frame(args.input, template_path, out_path, args.scale, args.tilt)

if __name__ == "__main__":
    main()
