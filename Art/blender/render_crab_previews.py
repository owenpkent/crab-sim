"""Preview renders for the fiddler crab.

  blender -b --factory-startup --python Art/blender/render_crab_previews.py -- <blend> <outdir> <mode> [engine] [extra]

modes
  turn        front / right / top / three-quarter turnaround (composite + single views)
  gamecam     55 degrees down, ~13 m away, looking along +X (the game camera)
  sheet NAME  contact sheet of 8 evenly spaced frames of action A_FiddlerCrab_NAME
  clay|clayhigh   quick 4-view clay/high-poly check (used while modelling)
engine: CYCLES (CPU, 4 threads, denoised) or BLENDER_EEVEE
"""
import bpy, sys, os, math
import numpy as np
from mathutils import Vector

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import crab_lib as L

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
BLEND, OUT, MODE = argv[0], argv[1], argv[2]
ENGINE = argv[3] if len(argv) > 3 else "BLENDER_EEVEE"
EXTRA = argv[4] if len(argv) > 4 else ""
os.makedirs(OUT, exist_ok=True)
bpy.ops.wm.open_mainfile(filepath=BLEND)
sc = bpy.context.scene

SHOWHIGH = MODE.endswith("high")
for o in list(bpy.data.objects):
    if (o.name.startswith("crab_high") or o.name.startswith("hi_")) and not SHOWHIGH:
        o.hide_render = True
        o.hide_viewport = True
    if o.name.startswith("crab_low") and SHOWHIGH:
        o.hide_render = True
        o.hide_viewport = True


def setup_render(w, h, samples=32):
    sc.render.engine = ENGINE
    sc.render.resolution_x = w
    sc.render.resolution_y = h
    sc.render.resolution_percentage = 100
    if ENGINE == "CYCLES":
        sc.cycles.device = 'CPU'
        sc.cycles.samples = samples
        sc.cycles.use_denoising = True
        sc.render.threads_mode = 'FIXED'
        sc.render.threads = 4
    sc.render.image_settings.file_format = 'PNG'
    sc.view_settings.view_transform = 'Standard'


def setup_world_lights():
    w = bpy.data.worlds.new("W")
    sc.world = w
    w.use_nodes = True
    bg = w.node_tree.nodes["Background"]
    bg.inputs[0].default_value = (0.62, 0.70, 0.80, 1)
    bg.inputs[1].default_value = 0.9
    ld = bpy.data.lights.new("sun", 'SUN')
    ld.energy = 3.0
    ld.angle = math.radians(6)
    lo = bpy.data.objects.new("sun", ld)
    sc.collection.objects.link(lo)
    lo.rotation_euler = (math.radians(50), 0, math.radians(-40))
    bpy.ops.mesh.primitive_plane_add(size=40, location=(0, 0, -0.001))
    g = bpy.context.active_object
    g.name = "ground"
    m = bpy.data.materials.new("gm")
    m.use_nodes = True
    m.node_tree.nodes["Principled BSDF"].inputs["Base Color"].default_value = (0.52, 0.47, 0.37, 1)
    m.node_tree.nodes["Principled BSDF"].inputs["Roughness"].default_value = 1.0
    g.data.materials.append(m)


def cam(loc, target, lens=50, fov=None):
    c = bpy.data.objects.get("cam")
    if c is None:
        c = bpy.data.objects.new("cam", bpy.data.cameras.new("cam"))
        sc.collection.objects.link(c)
    if fov:
        c.data.lens_unit = 'FOV'
        c.data.angle = math.radians(fov)
    else:
        c.data.lens_unit = 'MILLIMETERS'
        c.data.lens = lens
    c.location = loc
    d = Vector(target) - Vector(loc)
    if abs(d.x) < 1e-6 and abs(d.y) < 1e-6:
        c.rotation_euler = (0, 0, 0)
    else:
        c.rotation_euler = d.to_track_quat('-Z', 'Y').to_euler()
    sc.camera = c


def render(name):
    p = os.path.join(OUT, name + ".png")
    sc.render.filepath = p
    bpy.ops.render.render(write_still=True)
    return p


def load_np(path):
    img = bpy.data.images.load(path, check_existing=False)
    w, h = img.size
    px = np.empty(w * h * 4, dtype=np.float32)
    img.pixels.foreach_get(px)
    bpy.data.images.remove(img)
    return px.reshape(h, w, 4)[..., :3]      # bottom row first


def save_np(path, arr):
    a8 = (np.clip(arr, 0, 1) * 255 + 0.5).astype(np.uint8)
    L.write_png(path, a8[::-1])


def to_srgb(a):
    """PNG pixels come back already display-encoded from bpy; keep as is."""
    return np.clip(a, 0, 1)


FONT = {"0": "111101101101111", "1": "010110010010111", "2": "111001111100111", "3": "111001111001111",
        "4": "101101111001001", "5": "111100111001111", "6": "111100111101111", "7": "111001001001001",
        "8": "111101111101111", "9": "111101111001111", "f": "011010111010010", " ": "000000000000000"}


def stamp(img, text, x=6, y=6, s=3):
    """img: bottom-row-first float array; draws text near the top-left."""
    H = img.shape[0]
    for ci, ch in enumerate(text):
        bits = FONT.get(ch, FONT[" "])
        for r in range(5):
            for c in range(3):
                if bits[r * 3 + c] == "1":
                    yy = H - 1 - (y + r * s)
                    xx = x + (ci * 4 + c) * s
                    img[yy - s + 1:yy + 1, xx:xx + s] = (1.0, 1.0, 1.0)
    return img


def grid(images, cols):
    rows = (len(images) + cols - 1) // cols
    h, w = images[0].shape[:2]
    out = np.zeros((rows * h, cols * w, 3), dtype=np.float32)
    for i, im in enumerate(images):
        r, c = divmod(i, cols)
        # rows are bottom-first, so the first row of the grid must be placed at the top
        y0 = (rows - 1 - r) * h
        out[y0:y0 + h, c * w:(c + 1) * w] = im
    return out


setup_world_lights()
tgt = (0.25, 0, 0.45)
if MODE in ("clay", "clayhigh"):
    setup_render(900, 600, 24)
    views = {"front": ((6.0, 0, 1.2), tgt), "right": ((0.25, -6.0, 1.2), tgt), "top": ((0.25, 0.0, 7.0), (0.25, 0, 0.3)),
             "q3": ((3.4, -3.6, 2.4), tgt)}
    for n, (l, t) in views.items():
        cam(l, t, 50)
        render(n)
elif MODE == "close":
    setup_render(960, 640, 48)
    views = {"carapace": ((-0.9, -0.9, 1.9), (0.0, 0.0, 0.4), 50), "claw": ((2.4, -2.2, 1.3), (0.85, -0.5, 0.5), 50),
             "leg": ((0.4, -2.4, 0.9), (0.15, -0.9, 0.35), 50), "face": ((2.4, 0.6, 0.9), (0.4, 0.0, 0.35), 50),
             "back": ((-3.0, 0.0, 1.6), (0.0, 0.0, 0.35), 50)}
    for n, (l, t, ln) in views.items():
        cam(l, t, ln)
        render("close_" + n)
elif MODE == "turn":
    setup_render(960, 640, 48)
    views = [("front", (6.5, 0, 1.0), (0.2, 0, 0.42), 42), ("right", (0.25, -6.5, 1.0), (0.3, 0, 0.42), 42),
             ("top", (0.25, 0.0, 7.5), (0.3, 0, 0.3), 42), ("q3", (3.6, -3.9, 2.5), (0.3, 0, 0.42), 42)]
    ims = []
    for n, l, t, ln in views:
        cam(l, t, ln)
        p = render("turn_" + n)
        ims.append(load_np(p))
    save_np(os.path.join(OUT, "crab_turnaround.png"), to_srgb(grid(ims, 2)))
elif MODE == "gamecam":
    setup_render(1600, 900, 64)
    pitch = math.radians(55)
    d = 13.0
    target = Vector((0.2, 0, 0.3))
    loc = target + Vector((-d * math.cos(pitch), 0, d * math.sin(pitch)))
    cam(loc, target, fov=90)
    p = render("gamecam_full")
    a = load_np(p)
    h, w = a.shape[:2]
    # crop the crab region and enlarge 3x for a legible look
    cw, ch = 300, 170
    crop = a[h // 2 - ch // 2:h // 2 + ch // 2, w // 2 - cw // 2:w // 2 + cw // 2]
    big = np.repeat(np.repeat(crop, 4, axis=0), 4, axis=1)
    save_np(os.path.join(OUT, "crab_gamecam_crop.png"), to_srgb(big))
    # a tighter game-like framing (same direction, narrower FOV) for reading detail
    cam(loc, target, fov=22)
    setup_render(1280, 720, 64)
    render("crab_gamecam_tight")
elif MODE == "sheet":
    custom = None
    name = EXTRA
    if ":" in EXTRA:
        name, fl = EXTRA.split(":")
        custom = [int(x) for x in fl.split(",")]
    act = bpy.data.actions["A_FiddlerCrab_" + name]
    arm = bpy.data.objects["Armature"]
    arm.animation_data.action = act
    f0, f1 = int(act.frame_range[0]), int(act.frame_range[1])
    frames = [int(round(f0 + (f1 - f0) * i / 8.0)) for i in range(8)]     # 8 evenly spaced (loop end == start)
    if name == "Dash":
        frames = [int(round(f0 + (f1 - f0) * i / 7.0)) for i in range(8)]
    if custom:
        frames = custom
    setup_render(560, 400, 24)
    view = os.environ.get("CRAB_SHEET_VIEW", "front3q")
    if view == "front3q":
        cam((3.2, -3.0, 2.1), (0.35, -0.1, 0.5), 42)
    elif view == "front":
        cam((4.4, 0, 1.6), (0.3, 0, 0.55), 42)
    elif view == "rear3q":
        cam((-3.3, -2.9, 2.5), (0.25, 0, 0.4), 42)
    elif view == "side":
        cam((0.3, -4.6, 1.5), (0.35, 0, 0.5), 42)
    else:
        cam((0.3, 0, 5.2), (0.3, 0, 0.3), 42)
    ims = []
    for f in frames:
        sc.frame_set(f)
        p = render(f"tmp_{name}_{f}")
        im = load_np(p)
        im = to_srgb(im)
        stamp(im, f"f{f}")
        ims.append(im)
        os.remove(p)
    save_np(os.path.join(OUT, f"crab_anim_{name}{os.environ.get('CRAB_SHEET_SUFFIX', '')}.png"), grid(ims, 4))
elif MODE == "frames":
    name, fl = EXTRA.split(":")
    act = bpy.data.actions["A_FiddlerCrab_" + name]
    arm = bpy.data.objects["Armature"]
    arm.animation_data.action = act
    setup_render(int(os.environ.get("CRAB_W", "960")), int(os.environ.get("CRAB_H", "640")), 32)
    v = os.environ.get("CRAB_SHEET_VIEW", "front3q")
    views = {"front3q": ((3.3, -3.0, 2.0), (0.35, -0.1, 0.5), 45), "front": ((4.6, 0, 1.6), (0.3, 0, 0.55), 42),
             "side": ((0.3, -5.0, 1.5), (0.35, 0, 0.55), 42), "top": ((0.3, 0, 5.5), (0.3, 0, 0.3), 42),
             "back": ((-4.5, 0, 2.5), (0.3, 0, 0.5), 42), "mouth": ((1.9, 0.6, 0.8), (0.5, 0.05, 0.3), 42),
             "rear3q": ((-3.3, -2.9, 2.5), (0.25, 0, 0.4), 42)}
    l, t, ln = views[v]
    cam(l, t, ln)
    for f in [int(x) for x in fl.split(",")]:
        sc.frame_set(f)
        render(f"fr_{name}_{v}_{f}")
print("[preview] done", MODE)
