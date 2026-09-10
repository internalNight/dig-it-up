from __future__ import annotations

from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


ROOT = Path(__file__).resolve().parents[1]
FRAME_DIR = ROOT / "Artifacts" / "FirstPlayableSurfaceFramesRelease"
GIF_PATH = ROOT / "Artifacts" / "SandFirstPlayableSurface.gif"
SHEET_PATH = ROOT / "Artifacts" / "SandFirstPlayableSurfaceContactSheet.png"

LABELS = (
    "初始沙床",
    "铲斗下探",
    "切入并收斗",
    "装砂抬升",
    "保持载荷",
    "开始倾倒",
    "落砂与堆积",
    "倾倒后稳定",
)


def font(size: int) -> ImageFont.FreeTypeFont | ImageFont.ImageFont:
    for path in (Path(r"C:\Windows\Fonts\msyh.ttc"), Path(r"C:\Windows\Fonts\simhei.ttf")):
        if path.exists():
            return ImageFont.truetype(str(path), size)
    return ImageFont.load_default()


def annotate(source: Path, label: str) -> Image.Image:
    image = Image.open(source).convert("RGB")
    draw = ImageDraw.Draw(image, "RGBA")
    draw.rounded_rectangle((18, 16, 245, 58), radius=9, fill=(18, 25, 32, 185))
    draw.text((32, 24), label, font=font(23), fill=(255, 255, 255, 255))
    return image


def main() -> None:
    # Only consume the canonical numbered frames from the latest packaged
    # sequence. Older hand-picked milestone files intentionally remain beside
    # them for historical comparison.
    paths = sorted(FRAME_DIR.glob("frame_[0-9][0-9][0-9].png"))
    if len(paths) != len(LABELS):
        raise RuntimeError(f"expected {len(LABELS)} frames, found {len(paths)}")

    frames = [annotate(path, label) for path, label in zip(paths, LABELS)]
    gif_frames = [frames[0]] * 2 + frames[1:-1] + [frames[-1]] * 3
    gif_frames[0].save(
        GIF_PATH,
        save_all=True,
        append_images=gif_frames[1:],
        duration=650,
        loop=0,
        optimize=True,
        disposal=2,
    )

    # Use four representative phases in the fixed 2x2 sheet, without black gaps.
    representative = (0, 2, 4, 6)
    sheet = Image.new("RGB", (1920, 1080), (35, 39, 43))
    for panel_index, frame_index in enumerate(representative):
        sheet.paste(frames[frame_index], ((panel_index % 2) * 960, (panel_index // 2) * 540))
    sheet.save(SHEET_PATH, optimize=True)

    print(f"Wrote {GIF_PATH} ({GIF_PATH.stat().st_size} bytes)")
    print(f"Wrote {SHEET_PATH} ({SHEET_PATH.stat().st_size} bytes)")


if __name__ == "__main__":
    main()
