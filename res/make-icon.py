"""Regenerates res/app.ico from res/print.png (needs Pillow: pip install pillow). Run: python res/make-icon.py"""
from pathlib import Path
from PIL import Image

here = Path(__file__).resolve().parent
img = Image.open(here / "print.png").convert("RGBA")
img.save(here / "app.ico", sizes=[(16, 16), (20, 20), (24, 24), (32, 32), (48, 48), (64, 64), (256, 256)])
print("wrote", here / "app.ico")
