from __future__ import annotations

import json
import math
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


PROJECT_ROOT = Path(__file__).resolve().parents[1]
DATA_PATH = PROJECT_ROOT / "Artifacts" / "column-collapse-data.json"
GIF_PATH = PROJECT_ROOT / "Artifacts" / "column-collapse.gif"
SHEET_PATH = PROJECT_ROOT / "Artifacts" / "column-collapse-contact-sheet.png"

DOMAIN_CORNERS = (
    (-0.75, -0.375, 0.0),
    (0.75, -0.375, 0.0),
    (0.75, 0.375, 0.0),
    (-0.75, 0.375, 0.0),
)


def load_font(size: int) -> ImageFont.FreeTypeFont | ImageFont.ImageFont:
    for candidate in (
        Path(r"C:\Windows\Fonts\msyh.ttc"),
        Path(r"C:\Windows\Fonts\simhei.ttf"),
        Path(r"C:\Windows\Fonts\arial.ttf"),
    ):
        if candidate.exists():
            return ImageFont.truetype(str(candidate), size)
    return ImageFont.load_default()


def project(point: list[float] | tuple[float, float, float], width: int, height: int) -> tuple[float, float, float]:
    yaw = math.radians(25.0)
    cosine = math.cos(yaw)
    sine = math.sin(yaw)
    horizontal = cosine * point[0] - sine * point[1]
    depth = sine * point[0] + cosine * point[1]
    scale = min((width - 90.0) / 1.85, (height - 100.0) / 1.16)
    return (
        width * 0.53 + horizontal * scale,
        height * 0.90 - point[2] * scale + depth * scale * 0.31,
        depth,
    )


def mix_color(a: tuple[int, int, int], b: tuple[int, int, int], amount: float) -> tuple[int, int, int]:
    amount = max(0.0, min(1.0, amount))
    return tuple(round(left + (right - left) * amount) for left, right in zip(a, b))


def render_frame(points: list[list[float]], time_seconds: float, size: tuple[int, int] = (760, 520)) -> Image.Image:
    width, height = size
    image = Image.new("RGB", size, (246, 244, 239))
    draw = ImageDraw.Draw(image)
    title_font = load_font(max(17, width // 40))
    detail_font = load_font(max(13, width // 55))

    draw.text((24, 17), "UE5 GPU 干砂柱体崩塌", font=title_font, fill=(31, 35, 40))
    draw.text((width - 162, 22), f"t = {time_seconds:0.2f} s", font=detail_font, fill=(70, 76, 82))

    projected_corners = [project(corner, width, height) for corner in DOMAIN_CORNERS]
    floor_polygon = [(round(x), round(y)) for x, y, _ in projected_corners]
    draw.polygon(floor_polygon, fill=(235, 232, 224))
    draw.line(floor_polygon + [floor_polygon[0]], fill=(92, 98, 104), width=2)

    for corner in (DOMAIN_CORNERS[0], DOMAIN_CORNERS[3]):
        lower = project(corner, width, height)
        upper = project((corner[0], corner[1], 1.0), width, height)
        draw.line((lower[0], lower[1], upper[0], upper[1]), fill=(116, 121, 126), width=2)

    projected_points = sorted(
        ((project(point, width, height), point[2]) for point in points),
        key=lambda item: item[0][2],
        reverse=True,
    )
    radius = max(2, width // 260)
    for (screen_x, screen_y, depth), height_m in projected_points:
        depth_factor = (depth + 0.8) / 1.6
        height_factor = min(1.0, height_m / 0.7)
        color = mix_color((181, 103, 35), (239, 184, 88), 0.35 * depth_factor + 0.45 * height_factor)
        draw.ellipse(
            (
                round(screen_x - radius),
                round(screen_y - radius),
                round(screen_x + radius),
                round(screen_y + radius),
            ),
            fill=color,
        )

    draw.text(
        (24, height - 31),
        "30° 内摩擦角 · 0 Pa 黏聚力 · 5 cm 诊断网格",
        font=detail_font,
        fill=(70, 76, 82),
    )
    return image


def main() -> None:
    data = json.loads(DATA_PATH.read_text(encoding="utf-8"))
    frame_delta = float(data["frameDt"])
    frames = [
        render_frame(points, index * frame_delta)
        for index, points in enumerate(data["frames"])
    ]

    gif_frames = [frames[0]] * 8 + frames + [frames[-1]] * 12
    durations = [55] * len(gif_frames)
    gif_frames[0].save(
        GIF_PATH,
        save_all=True,
        append_images=gif_frames[1:],
        duration=durations,
        loop=0,
        optimize=False,
        disposal=2,
    )

    selected = (0, len(frames) // 3, 2 * len(frames) // 3, len(frames) - 1)
    panel_size = (520, 360)
    contact_sheet = Image.new("RGB", (panel_size[0] * 2, panel_size[1] * 2), (246, 244, 239))
    for panel_index, frame_index in enumerate(selected):
        panel = render_frame(data["frames"][frame_index], frame_index * frame_delta, panel_size)
        contact_sheet.paste(panel, ((panel_index % 2) * panel_size[0], (panel_index // 2) * panel_size[1]))
    contact_sheet.save(SHEET_PATH, optimize=True)

    print(f"Wrote {GIF_PATH} ({GIF_PATH.stat().st_size} bytes)")
    print(f"Wrote {SHEET_PATH} ({SHEET_PATH.stat().st_size} bytes)")


if __name__ == "__main__":
    main()
