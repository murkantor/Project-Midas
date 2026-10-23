import os
import math
from PIL import Image, ImageDraw, ImageFont, ImageFilter

def create_gunbarrel_vignette(width, height):
    base = Image.new('RGBA', (width, height), (12, 13, 16, 255))
    cx, cy = width / 2.0, height / 2.0
    max_r = math.sqrt(cx*cx + cy*cy)
    
    # Smooth radial gradient
    grad = Image.new('RGBA', (width, height), (0, 0, 0, 0))
    d_grad = ImageDraw.Draw(grad)
    steps = 60
    for i in range(steps):
        r = max_r * (1.0 - i / float(steps))
        alpha = int(240 * (i / float(steps))**1.4)
        c = (22 + int(12*(1-i/steps)), 24 + int(12*(1-i/steps)), 30 + int(14*(1-i/steps)), 255 - alpha)
        d_grad.ellipse([cx - r, cy - r, cx + r, cy + r], fill=c)
        
    base = Image.alpha_composite(base, grad)
    
    # Subtle gunbarrel rifling lines
    rifling = Image.new('RGBA', (width, height), (0, 0, 0, 0))
    d_rif = ImageDraw.Draw(rifling)
    num_spirals = 8
    for s in range(num_spirals):
        angle_base = s * (2 * math.pi / num_spirals)
        points = []
        for step in range(80):
            t = step / 80.0
            r = 100 + t * (max_r - 80)
            ang = angle_base + t * 1.3
            x = cx + r * math.cos(ang)
            y = cy + r * math.sin(ang)
            points.append((x, y))
        d_rif.line(points, fill=(218, 165, 32, 24), width=3)
    
    rifling = rifling.filter(ImageFilter.GaussianBlur(1.5))
    base = Image.alpha_composite(base, rifling)
    return base

def generate_landscape_splash(icon_img, logo_crop, out_path):
    w, h = 1920, 1080
    bg = create_gunbarrel_vignette(w, h)
    
    # GoldenEye / Midas Emblem
    # Scale emblem to 280x280
    emblem_size = 280
    emblem = icon_img.resize((emblem_size, emblem_size), Image.Resampling.LANCZOS)
    
    # Project Midas logo banner: scale to ~760px wide
    logo_w = 760
    logo_h = int(logo_crop.size[1] * (logo_w / float(logo_crop.size[0])))
    logo_scaled = logo_crop.resize((logo_w, logo_h), Image.Resampling.LANCZOS)
    
    # Vertical stacking in center
    total_content_h = emblem_size + 24 + logo_h + 36 + 24
    start_y = int((h - total_content_h) / 2.0) - 20
    
    # Paste emblem
    emblem_x = int((w - emblem_size) / 2.0)
    emblem_y = start_y
    bg.alpha_composite(emblem, (emblem_x, emblem_y))
    
    # Paste logo banner
    logo_x = int((w - logo_w) / 2.0)
    logo_y = emblem_y + emblem_size + 24
    bg.alpha_composite(logo_scaled, (logo_x, logo_y))
    
    # Subtle subtitle text
    draw = ImageDraw.Draw(bg)
    sub_text = "GOLDENEYE 007 - CLASSIFIED"
    # Fallback default font with letter spacing
    char_spacing = 6
    text_y = logo_y + logo_h + 30
    
    # Draw simple clean text centered
    approx_w = len(sub_text) * 10 + (len(sub_text) - 1) * char_spacing
    cur_x = int((w - approx_w) / 2.0)
    for ch in sub_text:
        draw.text((cur_x, text_y), ch, fill=(218, 165, 32, 190))
        cur_x += 10 + char_spacing
        
    bg.save(out_path, format='PNG', optimize=True)
    print(f"Saved landscape splash: {out_path} ({w}x{h})")

def generate_portrait_splash(icon_img, logo_crop, out_path):
    w, h = 1080, 1920
    bg = create_gunbarrel_vignette(w, h)
    
    # GoldenEye / Midas Emblem: scale to 380x380
    emblem_size = 380
    emblem = icon_img.resize((emblem_size, emblem_size), Image.Resampling.LANCZOS)
    
    # Project Midas logo banner: scale to ~780px wide
    logo_w = 780
    logo_h = int(logo_crop.size[1] * (logo_w / float(logo_crop.size[0])))
    logo_scaled = logo_crop.resize((logo_w, logo_h), Image.Resampling.LANCZOS)
    
    total_content_h = emblem_size + 40 + logo_h + 40 + 24
    start_y = int((h - total_content_h) / 2.0) - 40
    
    emblem_x = int((w - emblem_size) / 2.0)
    emblem_y = start_y
    bg.alpha_composite(emblem, (emblem_x, emblem_y))
    
    logo_x = int((w - logo_w) / 2.0)
    logo_y = emblem_y + emblem_size + 40
    bg.alpha_composite(logo_scaled, (logo_x, logo_y))
    
    draw = ImageDraw.Draw(bg)
    sub_text = "GOLDENEYE 007 - CLASSIFIED"
    char_spacing = 8
    text_y = logo_y + logo_h + 36
    approx_w = len(sub_text) * 12 + (len(sub_text) - 1) * char_spacing
    cur_x = int((w - approx_w) / 2.0)
    for ch in sub_text:
        draw.text((cur_x, text_y), ch, fill=(218, 165, 32, 190))
        cur_x += 12 + char_spacing
        
    bg.save(out_path, format='PNG', optimize=True)
    print(f"Saved portrait splash: {out_path} ({w}x{h})")

def generate_icons(icon_img, res_dir):
    # Density configs:
    # density: (legacy_icon_size, adaptive_fg_size)
    densities = {
        'mdpi': (48, 108),
        'hdpi': (72, 162),
        'xhdpi': (96, 216),
        'xxhdpi': (144, 324),
        'xxxhdpi': (192, 432),
    }
    
    for density, (legacy_size, fg_size) in densities.items():
        folder = os.path.join(res_dir, f"mipmap-{density}")
        os.makedirs(folder, exist_ok=True)
        
        # 1. Legacy icon (square with subtle rounded corners and gunmetal background)
        legacy = Image.new('RGBA', (legacy_size, legacy_size), (0, 0, 0, 0))
        # Draw background rounded rect
        bg_draw = ImageDraw.Draw(legacy)
        r = int(legacy_size * 0.18)
        bg_draw.rounded_rectangle([0, 0, legacy_size - 1, legacy_size - 1], radius=r, fill=(18, 19, 22, 255))
        # Resize emblem to fit inside with 4% padding
        pad = int(legacy_size * 0.06)
        inner_size = legacy_size - 2 * pad
        emblem_scaled = icon_img.resize((inner_size, inner_size), Image.Resampling.LANCZOS)
        legacy.alpha_composite(emblem_scaled, (pad, pad))
        legacy.save(os.path.join(folder, "ic_launcher.png"), format='PNG', optimize=True)
        
        # 2. Round icon (circular mask)
        round_icon = Image.new('RGBA', (legacy_size, legacy_size), (0, 0, 0, 0))
        r_draw = ImageDraw.Draw(round_icon)
        r_draw.ellipse([0, 0, legacy_size - 1, legacy_size - 1], fill=(18, 19, 22, 255))
        round_icon.alpha_composite(emblem_scaled, (pad, pad))
        # Mask edges cleanly
        mask = Image.new('L', (legacy_size, legacy_size), 0)
        m_draw = ImageDraw.Draw(mask)
        m_draw.ellipse([0, 0, legacy_size - 1, legacy_size - 1], fill=255)
        round_icon.putalpha(mask)
        round_icon.save(os.path.join(folder, "ic_launcher_round.png"), format='PNG', optimize=True)
        
        # 3. Adaptive foreground: canvas is fg_size, emblem occupies center 66% safe zone
        fg = Image.new('RGBA', (fg_size, fg_size), (0, 0, 0, 0))
        safe_size = int(fg_size * 0.68)
        emblem_fg = icon_img.resize((safe_size, safe_size), Image.Resampling.LANCZOS)
        fg_pos = int((fg_size - safe_size) / 2.0)
        fg.alpha_composite(emblem_fg, (fg_pos, fg_pos))
        fg.save(os.path.join(folder, "ic_launcher_foreground.png"), format='PNG', optimize=True)
        print(f"Generated icons for mipmap-{density}")

def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    res_dir = os.path.join(root, "android", "app", "src", "main", "res")
    icon_path = os.path.join(root, "port", "include", "icon.png")
    logo_path = os.path.join(root, "port", "include", "presskit", "PM thin 1 - full.png")
    
    icon_img = Image.open(icon_path).convert('RGBA')
    logo_img = Image.open(logo_path).convert('RGBA')
    logo_crop = logo_img.crop(logo_img.getbbox())
    
    # 1. Icons
    generate_icons(icon_img, res_dir)
    
    # 2. Splash screens
    land_dir = os.path.join(res_dir, "drawable-land")
    port_dir = os.path.join(res_dir, "drawable-port")
    os.makedirs(land_dir, exist_ok=True)
    os.makedirs(port_dir, exist_ok=True)
    
    generate_landscape_splash(icon_img, logo_crop, os.path.join(land_dir, "splash_screen.png"))
    generate_portrait_splash(icon_img, logo_crop, os.path.join(port_dir, "splash_screen.png"))

if __name__ == '__main__':
    main()
