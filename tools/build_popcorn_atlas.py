"""Pack the site's popcorn GIF into a small texture for Switch SDL2.

SDL2_image in devkitPro decodes a GIF's first frame only. The checked-in atlas
keeps the site's artwork and animation without decoding 57 full-size frames
inside the Switch's limited playback heap.
"""

from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "assets" / "pipoca-loading.gif"
OUTPUT = ROOT / "data" / "pipoca_atlas.bin"
FRAME_SIZE = 160
COLUMNS = 8
INDICES = list(range(0, 57, 2))

with Image.open(SOURCE) as gif:
    if gif.n_frames != 57 or gif.size != (512, 512):
        raise SystemExit("Unexpected popcorn GIF; review frame selection")
    rows = (len(INDICES) + COLUMNS - 1) // COLUMNS
    atlas = Image.new("RGBA", (COLUMNS * FRAME_SIZE, rows * FRAME_SIZE))
    for target, source in enumerate(INDICES):
        gif.seek(source)
        frame = gif.convert("RGBA").resize(
            (FRAME_SIZE, FRAME_SIZE), Image.Resampling.LANCZOS
        )
        atlas.alpha_composite(
            frame, ((target % COLUMNS) * FRAME_SIZE, (target // COLUMNS) * FRAME_SIZE)
        )
    atlas.save(OUTPUT, format="PNG", optimize=True)

print(f"{len(INDICES)} frames -> {OUTPUT} ({OUTPUT.stat().st_size} bytes)")
