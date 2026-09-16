from pathlib import Path
from PIL import Image, ImageDraw

image = Image.new("RGBA", (512, 512))
draw = ImageDraw.Draw(image)
draw.rounded_rectangle((0, 0, 511, 511), radius=96, fill="white")
draw.rounded_rectangle((56, 116, 456, 396), radius=56, fill="#e32636")
draw.polygon(((210, 176), (210, 336), (342, 256)), fill="white")
image.resize((128, 128), Image.Resampling.LANCZOS).save(Path(__file__).resolve().parents[1] / "assets/app-icon.png")
