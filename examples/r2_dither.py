"""R2 quasirandom dither: pattern generation + animated dissolve on a sprite."""
""" https://extremelearning.com.au/unreasonable-effectiveness-of-quasirandom-sequences/ """

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import matplotlib.colors as mcolors
import numpy as np
import pygame
from pathlib import Path

GRID = 16
TOTAL = GRID * GRID
SCALE = 8

PHI2 = 1.32471795724474602596
ALPHA1 = 1.0 / PHI2
ALPHA2 = 1.0 / (PHI2 * PHI2)
SEED = 0.5

SKY_BLUE = (135, 206, 235)

# Super Mario Bros Super Mushroom, 16x16, sampled from mushroom.pgm.
# 0=transparent  1=black(outline)  2=dark red  3=bright red  4=off-white  5=tan
PALETTE = np.array([
    [  0,   0,   0,   0],
    [  0,   0,   0, 255],
    [211,  26,  26, 255],
    [255,  26,  26, 255],
    [248, 248, 248, 255],
    [255, 208, 127, 255],
], dtype=np.uint8)

# fmt: off
SPRITE_INDICES = np.array([
    [0,0,0,0,0,1,1,1,1,1,1,0,0,0,0,0],
    [0,0,0,1,1,4,4,2,2,2,2,1,1,0,0,0],
    [0,0,1,4,4,4,4,3,3,3,3,4,4,1,0,0],
    [0,1,4,4,4,4,3,3,3,3,3,3,4,4,1,0],
    [0,1,4,4,4,3,3,4,4,4,4,3,3,4,1,0],
    [1,2,3,3,3,3,4,4,4,4,4,4,3,3,2,1],
    [1,2,4,4,3,3,4,4,4,4,4,4,3,3,2,1],
    [1,4,4,4,4,3,4,4,4,4,4,4,3,3,4,1],
    [1,4,4,4,4,3,3,4,4,4,4,3,3,4,4,1],
    [1,2,4,4,2,2,2,2,2,2,2,2,2,4,4,1],
    [1,2,2,2,1,1,1,1,1,1,1,1,2,2,4,1],
    [0,1,1,1,5,5,1,5,5,1,5,5,1,1,1,0],
    [0,0,1,5,5,5,1,5,5,1,5,5,5,1,0,0],
    [0,0,1,5,5,5,5,5,5,5,5,5,5,1,0,0],
    [0,0,0,1,5,5,5,5,5,5,5,5,1,0,0,0],
    [0,0,0,0,1,1,1,1,1,1,1,1,0,0,0,0],
])
# fmt: on


def build_dither_array() -> np.ndarray:
    grid = np.full((GRID, GRID), -1, dtype=int)
    filled = 0
    n = 0
    while filled < TOTAL:
        n += 1
        x = (SEED + ALPHA1 * n) % 1.0
        y = (SEED + ALPHA2 * n) % 1.0
        col = int(x * GRID) % GRID
        row = int(y * GRID) % GRID
        if grid[row, col] != -1:
            continue
        grid[row, col] = filled
        filled += 1
    return grid


def invert_dither_array(dither: np.ndarray) -> np.ndarray:
    """Swap roles of position and value: inverted[value] = linear_position."""
    inverted = np.empty_like(dither)
    for row in range(GRID):
        for col in range(GRID):
            v = dither[row, col]
            inverted[v // GRID, v % GRID] = row * GRID + col
    return inverted


def build_position_lut(dither: np.ndarray) -> list[tuple[int, int]]:
    """Map dither value -> (row, col). Used to find which pixel to flip."""
    lut = [(0, 0)] * TOTAL
    for row in range(GRID):
        for col in range(GRID):
            lut[dither[row, col]] = (row, col)
    return lut


def build_sprite() -> np.ndarray:
    return PALETTE[SPRITE_INDICES]


def apply_dithered_transparency(
    sprite: np.ndarray, dither: np.ndarray, transparency: float,
) -> np.ndarray:
    result = sprite.copy()
    threshold = int(TOTAL * transparency)
    result[dither < threshold, 3] = 0
    return result


def render_heatmap(grid: np.ndarray, title: str, ax: plt.Axes) -> None:
    cmap = plt.cm.viridis
    norm = mcolors.Normalize(vmin=0, vmax=TOTAL - 1)
    ax.imshow(grid, cmap=cmap, norm=norm, origin="upper")
    for row in range(GRID):
        for col in range(GRID):
            val = grid[row, col]
            rgba = cmap(norm(val))
            luminance = 0.299 * rgba[0] + 0.587 * rgba[1] + 0.114 * rgba[2]
            text_color = "white" if luminance < 0.5 else "black"
            ax.text(
                col, row, f"{val:3d}",
                ha="center", va="center",
                fontsize=7, fontfamily="monospace", color=text_color,
            )
    ax.set_xticks(np.arange(-0.5, GRID, 1), minor=True)
    ax.set_yticks(np.arange(-0.5, GRID, 1), minor=True)
    ax.grid(which="minor", color="gray", linewidth=0.5)
    ax.tick_params(
        which="both", bottom=False, left=False,
        labelbottom=False, labelleft=False,
    )
    ax.set_title(title, fontsize=12)


def plot_dither(grid: np.ndarray) -> None:
    fig, ax = plt.subplots(figsize=(10, 10))
    render_heatmap(grid, "R2 Quasirandom Dither Pattern (16\u00d716)", ax)
    plt.tight_layout()

    out = Path(__file__).parent / "r2_dither.png"
    fig.savefig(out, dpi=150)
    print(f"Saved {out}")
    plt.close(fig)


def fig_to_pygame_surface(fig: plt.Figure) -> pygame.Surface:
    fig.canvas.draw()
    w, h = fig.canvas.get_width_height()
    buf = np.frombuffer(fig.canvas.buffer_rgba(), dtype=np.uint8).reshape(h, w, 4)
    return pygame.surfarray.make_surface(buf[:, :, :3].transpose(1, 0, 2))


def show_dither_debug(dither: np.ndarray) -> None:
    inverted = invert_dither_array(dither)

    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(16, 8))
    render_heatmap(dither, "R2 Dither (position \u2192 fill order)", ax1)
    render_heatmap(inverted, "Inverted R2 (fill order \u2192 position index)", ax2)
    plt.tight_layout()

    surf = fig_to_pygame_surface(fig)
    plt.close(fig)

    screen = pygame.display.set_mode(surf.get_size())
    pygame.display.set_caption("R2 Dither Debug: Original vs Inverted")
    screen.blit(surf, (0, 0))
    pygame.display.flip()

    waiting = True
    while waiting:
        for event in pygame.event.get():
            if event.type in (pygame.QUIT, pygame.KEYDOWN):
                waiting = False


def show_bitplane_debug(dither: np.ndarray) -> None:
    fig, axes = plt.subplots(2, 4, figsize=(16, 8))
    for bit in range(8):
        ax = axes[bit // 4, bit % 4]
        plane = (dither >> bit) & 1
        ax.imshow(plane, cmap="gray", vmin=0, vmax=1, origin="upper")
        ax.set_xticks(np.arange(-0.5, GRID, 1), minor=True)
        ax.set_yticks(np.arange(-0.5, GRID, 1), minor=True)
        ax.grid(which="minor", color="gray", linewidth=0.5)
        ax.tick_params(
            which="both", bottom=False, left=False,
            labelbottom=False, labelleft=False,
        )
        ax.set_title(f"Bit {bit}  (weight {1 << bit})", fontsize=11)
    plt.suptitle("R2 Dither Bit Planes", fontsize=14)
    plt.tight_layout()

    surf = fig_to_pygame_surface(fig)
    plt.close(fig)

    screen = pygame.display.set_mode(surf.get_size())
    pygame.display.set_caption("R2 Dither Bit Planes")
    screen.blit(surf, (0, 0))
    pygame.display.flip()

    waiting = True
    while waiting:
        for event in pygame.event.get():
            if event.type in (pygame.QUIT, pygame.KEYDOWN):
                waiting = False


def compose(
    screen: pygame.Surface,
    sprite: np.ndarray,
    bg: tuple[int, int, int],
    ox: int = 0,
    oy: int = 0,
) -> None:
    """Composite sprite over a solid background. Alpha=0 pixels show bg."""
    for row in range(GRID):
        for col in range(GRID):
            r, g, b, a = sprite[row, col]
            color = (int(r), int(g), int(b)) if a > 0 else bg
            pygame.draw.rect(
                screen, color,
                (ox + col * SCALE, oy + row * SCALE, SCALE, SCALE),
            )


def show_debug_sprites(sprite: np.ndarray, dithered: np.ndarray) -> None:
    gap = SCALE
    w = GRID * SCALE * 2 + gap * 3
    h = GRID * SCALE + gap * 2 + 30
    screen = pygame.display.set_mode((w, h))
    pygame.display.set_caption("Debug: Original vs Dithered")

    font = pygame.font.SysFont("consolas", 16)

    screen.fill(SKY_BLUE)
    compose(screen, sprite, SKY_BLUE, gap, gap + 24)
    compose(screen, dithered, SKY_BLUE, gap * 2 + GRID * SCALE, gap + 24)

    lbl1 = font.render("Original", True, (0, 0, 0))
    lbl2 = font.render("75% opaque (dithered)", True, (0, 0, 0))
    screen.blit(lbl1, (gap, 6))
    screen.blit(lbl2, (gap * 2 + GRID * SCALE, 6))

    pygame.display.flip()

    waiting = True
    while waiting:
        for event in pygame.event.get():
            if event.type in (pygame.QUIT, pygame.KEYDOWN):
                waiting = False


DX_STEP = 5
DY_STEP = 13


def animate(
    sprite: np.ndarray, dither: np.ndarray, transparency: float,
) -> None:
    pos_lut = build_position_lut(dither)
    threshold = int(TOTAL * transparency)

    current = apply_dithered_transparency(sprite, dither, transparency)

    tail = 0
    tail_dx, tail_dy = 0, 0
    head = threshold
    head_dx, head_dy = 0, 0

    size = GRID * SCALE
    screen = pygame.display.set_mode((size, size))
    pygame.display.set_caption("R2 Dither Dissolve")
    clock = pygame.time.Clock()
    frame = 0

    speed = 1

    running = True
    while running:
        for event in pygame.event.get():
            if event.type == pygame.QUIT:
                running = False
            elif event.type == pygame.KEYDOWN:
                if event.key == pygame.K_ESCAPE:
                    running = False
                elif event.key == pygame.K_UP:
                    speed = min(speed + 1, TOTAL)
                elif event.key == pygame.K_DOWN:
                    speed = max(speed - 1, 0)

        keys = pygame.key.get_pressed()

        if keys[pygame.K_RIGHT] and threshold < TOTAL:
            row, col = pos_lut[head]
            ar, ac = (row + head_dy) % GRID, (col + head_dx) % GRID
            current[ar, ac, 3] = 0
            head = (head + 1) % TOTAL
            if head == 0:
                head_dx = (head_dx + DX_STEP) % GRID
                head_dy = (head_dy + DY_STEP) % GRID
            threshold += 1

        if keys[pygame.K_LEFT] and threshold > 0:
            row, col = pos_lut[tail]
            ar, ac = (row + tail_dy) % GRID, (col + tail_dx) % GRID
            current[ar, ac] = sprite[ar, ac]
            tail = (tail + 1) % TOTAL
            if tail == 0:
                tail_dx = (tail_dx + DX_STEP) % GRID
                tail_dy = (tail_dy + DY_STEP) % GRID
            threshold -= 1

        for _ in range(speed):
            row, col = pos_lut[tail]
            ar, ac = (row + tail_dy) % GRID, (col + tail_dx) % GRID
            current[ar, ac] = sprite[ar, ac]

            row, col = pos_lut[head]
            ar, ac = (row + head_dy) % GRID, (col + head_dx) % GRID
            current[ar, ac, 3] = 0

            tail = (tail + 1) % TOTAL
            if tail == 0:
                tail_dx = (tail_dx + DX_STEP) % GRID
                tail_dy = (tail_dy + DY_STEP) % GRID

            head = (head + 1) % TOTAL
            if head == 0:
                head_dx = (head_dx + DX_STEP) % GRID
                head_dy = (head_dy + DY_STEP) % GRID

        compose(screen, current, SKY_BLUE)
        pygame.display.flip()
        clock.tick(60)

        frame += 1
        if frame % 30 == 0:
            fps = clock.get_fps()
            opaque_pct = 100 - threshold * 100 // TOTAL
            pygame.display.set_caption(
                f"R2 Dither  |  speed {speed}  |  {opaque_pct}% opaque  |  {fps:.1f} fps"
            )


if __name__ == "__main__":
    dither = build_dither_array()
    # plot_dither(dither)

    sprite = build_sprite()

    pygame.init()
    animate(sprite, dither, transparency=0.25)
    pygame.quit()
