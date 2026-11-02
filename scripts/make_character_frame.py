#!/usr/bin/env python3
"""
Persona 5 Rice - Character Cut-in Frame Generator
Composites any transparent PNG character portrait (bustup) into the authentic
Persona 5 dialogue / confidant cut-in comic frame style.
"""

import sys
import argparse
from pathlib import Path
from PIL import Image, ImageDraw, ImageOps
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

    # Precise inner polygon of the dialogue window (inside the inner border)
    poly_inner = [
        (1010, 48),   # Top vertex
        (1258, 365),  # Upper-right corner
        (1235, 640),  # Right corner
        (670, 940),   # Bottom vertex
        (290, 840),   # Bottom-left corner
        (185, 470),   # Lower-left corner
        (155, 350),   # Mid-left
        (126, 248),   # Notch base bottom (inner edge of white stripe)
        (120, 212),   # Notch tip inner
        (195, 190),   # Notch base top
    ]

    # Inner mask
    inner_mask = Image.new("L", (W, H), 0)
    draw = ImageDraw.Draw(inner_mask)
    draw.polygon(poly_inner, fill=255)

    # Pure white comic interior background
    bg = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    bg_draw = ImageDraw.Draw(bg)
    bg_draw.polygon(poly_inner, fill=(255, 255, 255, 255))

    # Scale character portrait
    s_w = int(char_img.width * scale)
    s_h = int(char_img.height * scale)
    char_scaled = char_img.resize((s_w, s_h), Image.LANCZOS)
    if tilt_deg != 0.0:
        char_scaled = char_scaled.rotate(tilt_deg, resample=Image.BICUBIC, expand=True)

    # Create charcoal drop-shadow silhouette (Persona 5 style)
    c_arr = np.array(char_scaled)
    shadow_arr = np.zeros_like(c_arr)
    shadow_arr[:, :, :3] = 48  # #303030
    shadow_arr[:, :, 3] = (c_arr[:, :, 3] * 0.96).astype(np.uint8)
    char_shadow = Image.fromarray(shadow_arr, "RGBA")

    # Center character nicely inside the frame
    pos_x = int(W * 0.335)
    pos_y = int(H * 0.108)
    shadow_pos_x = pos_x - 42
    shadow_pos_y = pos_y + 12

    # Assemble character layer
    char_layer = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    char_layer.paste(char_shadow, (shadow_pos_x, shadow_pos_y), char_shadow)
    char_layer.paste(char_scaled, (pos_x, pos_y), char_scaled)

    # Strictly clip character to inner frame polygon
    char_alpha = np.array(char_layer.split()[3])
    char_alpha = np.minimum(char_alpha, np.array(inner_mask))
    char_layer.putalpha(Image.fromarray(char_alpha, "L"))

    # Composite: white bg + clipped character
    interior = Image.alpha_composite(bg, char_layer)

    # Clean any headphone residue from template border area
    t_clean = template.copy()
    t_arr_clean = np.array(t_clean)
    for y in range(200, 450):
        mask = (t_arr_clean[y, :, 0] < 70) & (t_arr_clean[y, :, 1] < 70) & (t_arr_clean[y, :, 2] < 70) & (t_arr_clean[y, :, 3] > 100)
        for x in range(115, 200):
            if mask[x]:
                t_arr_clean[y, x, :3] = 255
    t_clean = Image.fromarray(t_arr_clean, "RGBA")

    # Extract clean frame border from template
    border_mask = np.where((t_alpha > 50) & (np.array(inner_mask) == 0), 255, 0).astype(np.uint8)
    border_layer = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    border_layer = Image.composite(t_clean, border_layer, Image.fromarray(border_mask, "L"))

    # Final composition: interior + frame border
    final_frame = Image.alpha_composite(interior, border_layer)

    # Clip outer alpha
    final_alpha = np.minimum(np.array(final_frame.split()[3]), t_alpha)
    final_frame.putalpha(Image.fromarray(final_alpha, "L"))

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

