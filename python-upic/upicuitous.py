"""
UPICuitous — Python prototype (runs NOW, no compiler needed).
Mirrors the JUCE version: Score Page + Wavetable + Envelope.

Run:
    python upicuitous.py

Requires: Python 3.10+, numpy, tkinter (both already installed).
- Draw on score with left mouse, right-click selects, Del deletes.
- Waveform / envelope drawn with left mouse.
- Space = play/stop (exports temp WAV + plays via winsound), R = render WAV.

File format (.upic.json) is compatible with the JUCE version.
"""
import json, math, os, sys, tempfile, wave, struct
from dataclasses import dataclass, field, asdict

try:
    import numpy as np
except ImportError:
    print("need numpy: pip install numpy")
    sys.exit(1)

MINF, MAXF = 20.0, 12000.0
WAVE_N = 1024

def midi_to_freq(m): return 440.0 * 2 ** ((m - 69) / 12)
def freq_to_midi(f):
    f = min(MAXF, max(MINF, f))
    return 69 + 12 * math.log2(f / 440.0)
def freq_to_norm(f):
    lo, hi = math.log2(MINF), math.log2(MAXF)
    return max(0.0, min(1.0, (math.log2(max(MINF, min(MAXF, f))) - lo) / (hi - lo)))
def norm_to_freq(n):
    n = max(0.0, min(1.0, n))
    lo, hi = math.log2(MINF), math.log2(MAXF)
    return 2 ** (lo + n * (hi - lo))

PALETTE = ["#5ac8fa", "#ff9f0a", "#ff375f", "#30d158", "#bf5af2", "#ffd60a",
           "#64d2ff", "#ff6482", "#0a84ff", "#30b0c7", "#ac8e68", "#98989f"]

# ---------------- model ----------------
@dataclass
class Waveform:
    id: int = 0
    name: str = "Sine"
    samples: list = field(default_factory=list)
    colour: str = "#5ac8fa"
    def __post_init__(self):
        if not self.samples:
            self.samples = [math.sin(2*math.pi*i/WAVE_N) for i in range(WAVE_N)]
    def value_at(self, ph):
        ph = ph % 1.0
        x = ph * WAVE_N
        i0 = int(x) % WAVE_N
        i1 = (i0 + 1) % WAVE_N
        f = x - math.floor(x)
        return self.samples[i0]*(1-f) + self.samples[i1]*f

def make_wave(kind, wid=0, name=None, colour="#5ac8fa"):
    import random
    w = Waveform(id=wid, name=name or kind, colour=colour)
    if kind == "sine":
        w.samples = [math.sin(2*math.pi*i/WAVE_N) for i in range(WAVE_N)]
    elif kind == "saw":
        w.samples = [2*i/WAVE_N-1 for i in range(WAVE_N)]
    elif kind == "square":
        w.samples = [1.0 if i < WAVE_N//2 else -1.0 for i in range(WAVE_N)]
    elif kind == "triangle":
        s = []
        for i in range(WAVE_N):
            p = i/WAVE_N
            s.append(p*4 if p < .25 else (2-p*4 if p < .75 else p*4-4))
        w.samples = s
    elif kind == "reed":
        rnd = random.Random(7+wid)
        pts = [rnd.uniform(-1,1) for _ in range(13)]
        s = []
        for i in range(WAVE_N):
            p = i/WAVE_N*12; k = int(p); f = p-k
            a, b = pts[k], pts[(k+1)%13]
            s.append(a+(b-a)*(f*f*(3-2*f)))
        w.samples = s
    return w

@dataclass
class Envelope:
    id: int = 0
    name: str = "Flat"
    points: list = field(default_factory=lambda: [[0,1],[1,1]])  # [x,y]
    colour: str = "#ff9f0a"
    def value_at(self, t):
        t = max(0.0, min(1.0, t))
        pts = sorted(self.points)
        if t <= pts[0][0]: return pts[0][1]
        for i in range(1, len(pts)):
            if t <= pts[i][0]:
                x0,y0 = pts[i-1]; x1,y1 = pts[i]
                if x1 <= x0: return y1
                f = (t-x0)/(x1-x0)
                return y0+(y1-y0)*f
        return pts[-1][1]

def env_preset(kind, eid=0):
    presets = {
        "flat": [[0,1],[1,1]],
        "percussive": [[0,0],[0.02,1],[0.25,0.4],[0.7,0.15],[1,0]],
        "swell": [[0,0],[0.4,0.2],[0.7,1],[1,0]],
        "gate": [[0,1],[0.45,1],[0.5,0],[1,0]],
        "strings": [[0,0],[0.15,1],[0.8,0.9],[1,0]],
    }
    return Envelope(id=eid, name=kind.capitalize(), points=presets[kind], colour=PALETTE[eid % len(PALETTE)])

@dataclass
class Arc:
    id: int = 1
    waveform: int = 0
    envelope: int = 0
    intensity: float = 0.8
    muted: bool = False
    points: list = field(default_factory=list)  # [t, freq]
    def start(self): return self.points[0][0] if self.points else 0
    def end(self): return self.points[-1][0] if self.points else 0
    def freq_at(self, t):
        if not self.points: return 440.0
        if t <= self.points[0][0]: return self.points[0][1]
        if t >= self.points[-1][0]: return self.points[-1][1]
        for i in range(1, len(self.points)):
            if t <= self.points[i][0]:
                t0,f0 = self.points[i-1]; t1,f1 = self.points[i]
                if t1 <= t0: return f1
                u = (t-t0)/(t1-t0)
                l0, l1 = math.log2(max(1,f0)), math.log2(max(1,f1))
                return 2**(l0+(l1-l0)*u)
        return self.points[-1][1]
    def life_at(self, t):
        s, e = self.start(), self.end()
        if e <= s: return 0.0
        return max(0.0, min(1.0, (t-s)/(e-s)))

# ---------------- engine ----------------
def render_page(arcs, waves, envs, duration, sr=44100, master=0.8):
    n = int(duration*sr)
    out = np.zeros(n, dtype=np.float64)
    wmap = {w.id: w for w in waves}
    emap = {e.id: e for e in envs}
    for arc in arcs:
        if arc.muted or len(arc.points) < 2: continue
        w = wmap.get(arc.waveform, waves[0] if waves else None)
        e = emap.get(arc.envelope, envs[0] if envs else None)
        if w is None: continue
        s_idx = max(0, int(arc.start()*sr)); e_idx = min(n, int(math.ceil(arc.end()*sr))+1)
        phase = 0.0
        # resample freq per sample via freq_at (could be faster, but fine)
        for i in range(s_idx, e_idx):
            t = i/sr
            f = min(14000.0, max(10.0, arc.freq_at(t)))
            env = e.value_at(arc.life_at(t)) if e else 1.0
            edge = min(t-arc.start(), arc.end()-t)/0.012
            edge = max(0.0, min(1.0, edge)); edge = edge*edge*(3-2*edge)
            out[i] += w.value_at(phase)*env*edge*arc.intensity
            phase = (phase + f/sr) % 1.0
    out = np.tanh(out*0.6)*master*0.9
    return (out*32767).astype(np.int16)

def write_wav(path, mono16, sr=44100):
    with wave.open(path, "wb") as wf:
        wf.setnchannels(1); wf.setsampwidth(2); wf.setframerate(sr)
        wf.writeframes(mono16.tobytes())

def demo_arcs():
    return [
        Arc(id=1, waveform=0, envelope=0, intensity=0.8, points=[[0,110],[6,220],[12,440],[18,330],[24,440]]),
        Arc(id=2, waveform=1, envelope=1, intensity=0.7, points=[[2,880],[8,660],[14,990],[22,1320]]),
        Arc(id=3, waveform=3, envelope=2, intensity=0.6, points=[[4,55],[16,82.5],[28,110]]),
        Arc(id=4, waveform=2, envelope=1, intensity=0.5, points=[[10,523.25],[11,587.33],[12,659.25],[13,783.99],[16,1046.5]]),
    ]

# ---------------- GUI ----------------
def run_gui():
    import tkinter as tk
    from tkinter import ttk, filedialog, messagebox

    waves = [make_wave("sine",0,"Sine",PALETTE[0]), make_wave("saw",1,"Saw",PALETTE[1]),
             make_wave("square",2,"Square",PALETTE[2]), make_wave("triangle",3,"Triangle",PALETTE[3]),
             make_wave("reed",4,"Reed",PALETTE[4])]
    envs = [env_preset("flat",0), env_preset("percussive",1), env_preset("swell",2),
            env_preset("gate",3), env_preset("strings",4)]
    arcs = demo_arcs()
    state = {"dur": 30.0, "master": 0.8, "wave": 0, "env": 0, "inten": 0.8,
             "tool": "draw", "sel": -1, "snap": True, "playing": False, "next_id": 5}

    root = tk.Tk(); root.title("UPICuitous (Python) — Xenakis vector synth")
    root.geometry("1180x760")

    # top bar
    top = ttk.Frame(root); top.pack(fill="x", padx=6, pady=4)
    ttk.Label(top, text="Tool:").pack(side="left")
    tool = tk.StringVar(value="draw")
    ttk.Combobox(top, textvariable=tool, values=["draw","line","sine","select","erase"], width=8).pack(side="left", padx=4)
    ttk.Label(top, text="Wave:").pack(side="left")
    wavevar = tk.StringVar(value="0: Sine")
    wavebox = ttk.Combobox(top, textvariable=wavevar, values=[f"{w.id}: {w.name}" for w in waves], width=12); wavebox.pack(side="left", padx=4)
    ttk.Label(top, text="Env:").pack(side="left")
    envvar = tk.StringVar(value="0: Flat")
    envbox = ttk.Combobox(top, textvariable=envvar, values=[f"{e.id}: {e.name}" for e in envs], width=12); envbox.pack(side="left", padx=4)
    ttk.Label(top, text="Inten:").pack(side="left")
    inten = tk.DoubleVar(value=0.8)
    ttk.Scale(top, variable=inten, from_=0.05, to=1.0, length=90).pack(side="left", padx=4)
    snapv = tk.BooleanVar(value=True)
    ttk.Checkbutton(top, text="Snap", variable=snapv).pack(side="left", padx=4)
    ttk.Label(top, text="Dur:").pack(side="left")
    durv = tk.DoubleVar(value=30.0)
    ttk.Scale(top, variable=durv, from_=5, to=120, length=90).pack(side="left", padx=4)
    status = tk.StringVar(value="Draw: drag. Right-click: select. Del: delete. Space: play.")
    ttk.Label(root, textvariable=status).pack(fill="x", padx=6)

    # score canvas
    score = tk.Canvas(root, bg="#101014", height=380, highlightthickness=0)
    score.pack(fill="both", expand=True, padx=6, pady=4)

    # bottom: wave + env
    bot = ttk.Frame(root); bot.pack(fill="x", padx=6, pady=4)
    wavecv = tk.Canvas(bot, bg="#141419", height=150); wavecv.pack(side="left", fill="both", expand=True, padx=2)
    envcv = tk.Canvas(bot, bg="#141419", height=150); envcv.pack(side="left", fill="both", expand=True, padx=2)

    def cur_wave_id():
        try: return int(wavevar.get().split(":")[0])
        except: return 0
    def cur_env_id():
        try: return int(envvar.get().split(":")[0])
        except: return 0
    def wave_by_id(i):
        for w in waves:
            if w.id == i: return w
        return waves[0]
    def env_by_id(i):
        for e in envs:
            if e.id == i: return e
        return envs[0]

    def quant(f):
        if not snapv.get(): return f
        m = round(freq_to_midi(f)); m = max(12, min(120, m))
        return midi_to_freq(m)

    # --- drawing helpers ---
    def draw_score():
        score.delete("all")
        W = score.winfo_width() or 1000; H = score.winfo_height() or 380
        dur = durv.get()
        # grid
        for sec in range(0, int(dur)+1, max(1, int(dur/10))):
            x = sec/dur*W
            score.create_line(x, 0, x, H, fill="#2a2a35")
            score.create_text(x+8, H-8, text=f"{sec}s", fill="#8a8a99", anchor="w", font=("Arial", 8))
        for midi in range(12, 121):
            f = midi_to_freq(midi)
            if f < MINF or f > MAXF: continue
            isC = (midi % 12 == 0)
            if not (isC or midi == 69 or midi % 12 in (5, 7)): continue
            y = H - freq_to_norm(f)*H
            col = "#ff9f0a" if midi == 69 else ("#3a3a4d" if isC else "#26262f")
            score.create_line(0, y, W, y, fill=col)
            if isC or midi == 69:
                import tkinter.font as _f  # noqa
                score.create_text(4, y-6, text=f"m{midi}", fill="#8a8a99", anchor="w", font=("Arial", 8))
        # arcs
        for a in arcs:
            w = wave_by_id(a.waveform)
            col = w.colour if not (a.muted) else "#555560"
            width = 3 if a.id == state["sel"] else 2
            pts = []
            for (t, f) in a.points:
                pts += [t/dur*W, H - freq_to_norm(f)*H]
            if len(pts) >= 4:
                score.create_line(*pts, fill=col, width=width, smooth=True)
            for (t, f) in a.points:
                x, y = t/dur*W, H - freq_to_norm(f)*H
                score.create_oval(x-3, y-3, x+3, y+3, fill=col, outline="")
        # playhead
        if state.get("playhead") is not None:
            x = state["playhead"]/dur*W
            score.create_line(x, 0, x, H, fill="#ff375f", width=2)

    def draw_wave():
        wavecv.delete("all")
        W = wavecv.winfo_width() or 400; H = wavecv.winfo_height() or 150
        w = wave_by_id(cur_wave_id())
        wavecv.create_text(6, 12, text=f"WAVETABLE {w.name} (drag to draw)", fill="#8a8a99", anchor="w", font=("Arial", 9))
        wavecv.create_line(0, H/2, W, H/2, fill="#2e2e3a")
        pts = []
        for i, s in enumerate(w.samples):
            pts += [i/len(w.samples)*W, H/2 - s*H*0.44]
        wavecv.create_line(*pts, fill=w.colour, width=1)

    def draw_env():
        envcv.delete("all")
        W = envcv.winfo_width() or 400; H = envcv.winfo_height() or 150
        e = env_by_id(cur_env_id())
        envcv.create_text(6, 12, text=f"ENVELOPE {e.name} (drag points, dbl-click add, right-click del)", fill="#8a8a99", anchor="w", font=("Arial", 9))
        pts = sorted(e.points)
        poly = [0, H]
        for i in range(201):
            t = i/200; v = e.value_at(t)
            poly += [t*W, H - v*(H-20)]
        poly += [W, H]
        envcv.create_polygon(*poly, fill=e.colour, stipple="", outline="")
        # overlay alpha not supported; draw line + dots
        line = []
        for i in range(201):
            t = i/200; v = e.value_at(t)
            line += [t*W, H - v*(H-20)]
        envcv.create_line(*line, fill=e.colour, width=2)
        for (x, y) in pts:
            envcv.create_oval(x*W-4, (H-y*(H-20))-4, x*W+4, (H-y*(H-20))+4, fill="white", outline=e.colour)

    def refresh():
        draw_score(); draw_wave(); draw_env()
        root.after(100, lambda: None)
    def update_status(msg=None):
        sel = [a for a in arcs if a.id == state["sel"]]
        base = f"arcs={len(arcs)} dur={durv.get():.0f}s "
        if sel:
            a = sel[0]
            base += f"| sel #{a.id} v{a.waveform} e{a.envelope} int{a.intensity:.2f} [{a.start():.1f}-{a.end():.1f}s]"
        status.set((msg + " | " if msg else "") + base)

    # --- score interactions ---
    stroke = {"pts": [], "active": False}
    def ev_xy(ev):
        return ev.x, ev.y
    def xy_to_tf(x, y):
        W = score.winfo_width() or 1000; H = score.winfo_height() or 380
        return max(0, min(durv.get(), x/W*durv.get())), norm_to_freq(1 - max(0, min(1, y/H)))
    def pick(x, y):
        W = score.winfo_width() or 1000; H = score.winfo_height() or 380
        best, bd = -1, 14
        for a in arcs:
            for (t, f) in a.points:
                import math as _m
                d = _m.hypot(t/durv.get()*W - x, (H - freq_to_norm(f)*H) - y)
                if d < bd: bd, best = d, a.id
        return best
    def on_press(ev):
        if ev.num == 3 or tool.get() == "select":
            state["sel"] = pick(ev.x, ev.y)
            draw_score(); update_status()
            return
        if tool.get() == "erase":
            pid = pick(ev.x, ev.y)
            if pid >= 0:
                arcs[:] = [a for a in arcs if a.id != pid]
                state["sel"] = -1
                draw_score(); update_status()
            return
        stroke["pts"] = [(ev.x, ev.y)]; stroke["active"] = True
        stroke["anchor"] = (ev.x, ev.y)
    def on_drag(ev):
        if stroke["active"]:
            if abs(ev.x - stroke["pts"][-1][0]) > 3 or abs(ev.y - stroke["pts"][-1][1]) > 3:
                stroke["pts"].append((ev.x, ev.y))
    def on_release(ev):
        if not stroke["active"]: return
        stroke["active"] = False
        stroke["pts"].append((ev.x, ev.y))
        pts = sorted(stroke["pts"])
        t = tool.get()
        if t == "line":
            (x0, y0), (x1, y1) = stroke["anchor"], (ev.x, ev.y)
            t0, f0 = xy_to_tf(x0, y0); t1, f1 = xy_to_tf(x1, y1)
            if abs(t1-t0) < 0.05: draw_score(); return
            if t1 < t0: t0, t1, f0, f1 = t1, t0, f1, f0
            import math as _m
            l0, l1 = _m.log2(max(1, f0)), _m.log2(max(1, f1))
            n = max(2, min(64, int(abs(t1-t0)/0.25)+1))
            apts = []
            for i in range(n+1):
                u = i/n
                apts.append([t0+(t1-t0)*u, quant(2**(l0+(l1-l0)*u))])
            a = Arc(id=state["next_id"], waveform=cur_wave_id(), envelope=cur_env_id(), intensity=float(inten.get()), points=apts)
            state["next_id"] += 1; arcs.append(a); state["sel"] = a.id
        elif t in ("draw", "sine"):
            apts, lastT = [], -1
            for (x, y) in pts:
                tt, ff = xy_to_tf(x, y)
                if tt <= lastT + 0.015: continue
                apts.append([tt, quant(ff)]); lastT = tt
            if t == "sine" and len(apts) > 3:
                import math as _m
                f0, f1 = apts[0][1], apts[-1][1]
                l0, l1 = _m.log2(f0), _m.log2(f1)
                t0, t1 = apts[0][0], apts[-1][0]
                for p in apts:
                    u = (p[0]-t0)/max(1e-6, t1-t0)
                    p[1] = quant(2**(l0+(l1-l0)*u + _m.sin(u*_m.pi*2*3)*0.35*0.35))
            if len(apts) >= 2:
                a = Arc(id=state["next_id"], waveform=cur_wave_id(), envelope=cur_env_id(), intensity=float(inten.get()), points=apts)
                state["next_id"] += 1; arcs.append(a); state["sel"] = a.id
        stroke["pts"] = []
        draw_score(); update_status()
    score.bind("<Button-1>", on_press); score.bind("<Button-3>", on_press)
    score.bind("<B1-Motion>", on_drag); score.bind("<ButtonRelease-1>", on_release)
    def on_key(ev):
        if ev.keysym in ("Delete", "BackSpace") and state["sel"] >= 0:
            arcs[:] = [a for a in arcs if a.id != state["sel"]]
            state["sel"] = -1; draw_score(); update_status()
        if ev.keysym == "space": do_play()
        if ev.keysym.lower() == "r": do_render()
    root.bind("<Key>", on_key)

    # --- wave / env paint ---
    def wave_paint(ev):
        w = wave_by_id(cur_wave_id())
        W = wavecv.winfo_width() or 400; H = wavecv.winfo_height() or 150
        v = -((ev.y - H/2)/(H*0.44)); v = max(-1, min(1, v))
        c = int(ev.x/W*WAVE_N)
        rad = max(1, WAVE_N//128)
        import math as _m
        for d in range(-rad, rad+1):
            fall = 0.5*(1+_m.cos(_m.pi*d/(rad+1)))
            idx = (c+d) % WAVE_N
            w.samples[idx] = w.samples[idx]*(1-fall) + v*fall
        draw_wave()
    wavecv.bind("<Button-1>", wave_paint); wavecv.bind("<B1-Motion>", wave_paint)
    env_drag = {"i": -1}
    def env_xy_to_pt(ev):
        W = envcv.winfo_width() or 400; H = envcv.winfo_height() or 150
        return max(0, min(1, ev.x/W)), max(0, min(1, 1-(ev.y-0)/(H-20+1e-9)))
    def env_nearest(ev):
        e = env_by_id(cur_env_id())
        W = envcv.winfo_width() or 400; H = envcv.winfo_height() or 150
        best, bd, bi = -1, 14, -1
        for i, (x, y) in enumerate(sorted(e.points)):
            import math as _m
            d = _m.hypot(x*W-ev.x, (H-y*(H-20))-ev.y)
            if d < bd: bd, best = d, i
        return best
    def env_down(ev):
        if ev.num == 3:
            e = env_by_id(cur_env_id())
            pts = sorted(e.points)
            # find nearest
            W = envcv.winfo_width() or 400; H = envcv.winfo_height() or 150
            best, bd = -1, 16
            for i, (x, y) in enumerate(pts):
                import math as _m
                d = _m.hypot(x*W-ev.x, (H-y*(H-20))-ev.y)
                if d < bd: bd, best = d, i
            if best >= 0 and len(pts) > 2:
                pts.pop(best); e.points = pts; draw_env()
            return
        env_drag["i"] = env_nearest(ev)
    def env_move(ev):
        if env_drag["i"] < 0: return
        e = env_by_id(cur_env_id())
        pts = sorted(e.points)
        x, y = env_xy_to_pt(ev)
        if env_drag["i"] == 0: x = 0
        if env_drag["i"] == len(pts)-1: x = 1
        pts[env_drag["i"]] = [x, y]
        e.points = sorted(pts); draw_env()
    def env_up(ev): env_drag["i"] = -1
    def env_dbl(ev):
        e = env_by_id(cur_env_id())
        if len(e.points) >= 32: return
        x, y = env_xy_to_pt(ev)
        e.points.append([x, y]); e.points = sorted(e.points); draw_env()
    envcv.bind("<Button-1>", env_down); envcv.bind("<B1-Motion>", env_move)
    envcv.bind("<ButtonRelease-1>", env_up); envcv.bind("<Double-Button-1>", env_dbl)
    envcv.bind("<Button-3>", env_down)

    # --- transport ---
    btns = ttk.Frame(root); btns.pack(fill="x", padx=6, pady=4)
    def do_play():
        sr = 44100
        mono = render_page(arcs, waves, envs, durv.get(), sr, 0.8)
        tmp = tempfile.mktemp(suffix=".wav")
        write_wav(tmp, mono, sr)
        update_status(f"rendered {len(mono)/sr:.1f}s -> playing")
        try:
            import winsound
            winsound.PlaySound(tmp, winsound.SND_FILENAME | winsound.SND_ASYNC)
        except Exception:
            os.startfile(tmp)
    def do_render():
        f = filedialog.asksaveasfilename(defaultextension=".wav", filetypes=[("WAV", "*.wav")])
        if not f: return
        mono = render_page(arcs, waves, envs, durv.get(), 44100, 0.8)
        write_wav(f, mono, 44100)
        messagebox.showinfo("Render", f"Wrote {f}\n{len(mono)/44100:.1f}s")
    def do_save():
        f = filedialog.asksaveasfilename(defaultextension=".upic.json", filetypes=[("UPIC", "*.json")])
        if not f: return
        data = {"app": "UPICuitous", "version": 1, "master": 0.8, "duration": durv.get(),
                "waveforms": [{"id": w.id, "name": w.name, "colour": w.colour, "samples": w.samples} for w in waves],
                "envelopes": [{"id": e.id, "name": e.name, "colour": e.colour, "points": e.points} for e in envs],
                "pages": [{"duration": durv.get(), "arcs": [
                    {"id": a.id, "waveformId": a.waveform, "envelopeId": a.envelope,
                     "intensity": a.intensity, "muted": a.muted,
                     "points": [{"t": t, "f": fr} for (t, fr) in a.points]} for a in arcs]}]}
        open(f, "w").write(json.dumps(data, indent=2))
    def do_load():
        f = filedialog.askopenfilename(filetypes=[("UPIC", "*.json")])
        if not f or not os.path.exists(f): return
        d = json.load(open(f))
        waves.clear(); envs.clear(); arcs.clear()
        for wd in d.get("waveforms", []):
            w = Waveform(id=wd["id"], name=wd.get("name", "Wave"), colour=wd.get("colour", "#5ac8fa"))
            w.samples = wd.get("samples", w.samples)[:WAVE_N]
            waves.append(w)
        for ed in d.get("envelopes", []):
            envs.append(Envelope(id=ed["id"], name=ed.get("name", "Env"), colour=ed.get("colour", "#ff9f0a"), points=ed.get("points", [[0,1],[1,1]])))
        pg = (d.get("pages") or [{}])[0]
        durv.set(pg.get("duration", 30.0))
        for ad in pg.get("arcs", []):
            arcs.append(Arc(id=ad.get("id", 1), waveform=ad.get("waveformId", 0), envelope=ad.get("envelopeId", 0),
                            intensity=ad.get("intensity", 0.8), muted=ad.get("muted", False),
                            points=[[p["t"], p["f"]] for p in ad.get("points", [])]))
        if arcs: state["next_id"] = max(a.id for a in arcs)+1
        wavebox["values"] = [f"{w.id}: {w.name}" for w in waves]
        envbox["values"] = [f"{e.id}: {e.name}" for e in envs]
        draw_score(); draw_wave(); draw_env(); update_status("loaded")
    ttk.Button(btns, text="Play (Space)", command=do_play).pack(side="left", padx=2)
    ttk.Button(btns, text="Render WAV (R)", command=do_render).pack(side="left", padx=2)
    ttk.Button(btns, text="Save", command=do_save).pack(side="left", padx=2)
    ttk.Button(btns, text="Load", command=do_load).pack(side="left", padx=2)
    ttk.Button(btns, text="Clear", command=lambda: (arcs.clear(), draw_score(), update_status())).pack(side="left", padx=2)
    ttk.Button(btns, text="Demo", command=lambda: (arcs.clear(), arcs.extend(demo_arcs()), draw_score(), update_status())).pack(side="left", padx=2)

    def on_wave_change(_e=None): draw_wave(); draw_score()
    def on_env_change(_e=None): draw_env()
    wavebox.bind("<<ComboboxSelected>>", on_wave_change)
    envbox.bind("<<ComboboxSelected>>", on_env_change)

    root.after(200, lambda: (draw_score(), draw_wave(), draw_env(), update_status("ready — demo loaded")))
    root.mainloop()

if __name__ == "__main__":
    if "--test-render" in sys.argv:
        waves = [make_wave("sine", 0), make_wave("saw", 1)]
        envs = [env_preset("flat", 0), env_preset("percussive", 1)]
        mono = render_page(demo_arcs(), waves, envs, 5.0, 44100, 0.8)
        write_wav("test_upic.wav", mono)
        print(f"wrote test_upic.wav {len(mono)} samples max={max(abs(mono))}")
    else:
        run_gui()
