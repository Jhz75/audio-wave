#!/usr/bin/env python3
"""Reproducible EGL preview of the SAME .effect arithmetic (not an OBS/D3D test).

pip install moderngl numpy pillow
python tools/render-chrome-morph.py --output /tmp/chrome-qa
"""
import argparse
import json
from pathlib import Path
import re
import subprocess
import time

import moderngl
import numpy as np
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]


def fragment():
    text = (ROOT / "data/effects/chrome-morph.effect").read_text()
    uniforms = text[text.index("uniform float2 resolution;"):text.index("struct VertIn")]
    body = text[text.index("float smoothUnion("):text.index("technique Draw")]
    body = body.replace("float4 PSDefault(VertOut v_in) : TARGET", "float4 shade(float2 texcoord)").replace("v_in.uv", "texcoord")
    for old, new in [("float4", "vec4"), ("float3", "vec3"), ("float2", "vec2"), ("lerp", "mix")]:
        uniforms = re.sub(rf"\b{old}\b", new, uniforms)
        body = re.sub(rf"\b{old}\b", new, body)
    return """#version 330
in vec2 texcoord;
out vec4 color;
float saturate(float x) { return clamp(x,0.0,1.0); }
vec3 saturate(vec3 x) { return clamp(x,0.0,1.0); }
vec4 saturate(vec4 x) { return clamp(x,0.0,1.0); }
""" + uniforms + body + "\nvoid main() { color=shade(texcoord); }\n"


def components(mask):
    # Four-connected silhouette components, rejecting isolated subpixel noise.
    from collections import deque
    seen = np.zeros_like(mask)
    sizes = []
    for y, x in zip(*np.nonzero(mask)):
        if seen[y, x]:
            continue
        seen[y, x] = 1
        q = deque([(y, x)])
        count = 0
        while q:
            yy, xx = q.popleft()
            count += 1
            for ny, nx in [(yy-1, xx), (yy+1, xx), (yy, xx-1), (yy, xx+1)]:
                if 0 <= ny < mask.shape[0] and 0 <= nx < mask.shape[1] and mask[ny, nx] and not seen[ny, nx]:
                    seen[ny, nx] = 1
                    q.append((ny, nx))
        if count > 40:
            sizes.append(count)
    return sorted(sizes, reverse=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, default=Path("/tmp/chrome-morph-qa"))
    parser.add_argument("--size", type=int, default=768)
    parser.add_argument("--animation", action="store_true", help="Export a labelled synthetic morphology demonstration")
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    ctx = moderngl.create_standalone_context(backend="egl")
    program = ctx.program(vertex_shader="""#version 330
in vec2 position; out vec2 texcoord;
void main() { texcoord=position*0.5+0.5; gl_Position=vec4(position,0.0,1.0); }
""", fragment_shader=fragment())
    buffer = ctx.buffer(np.array([-1,-1, 1,-1, -1,1, 1,1], dtype="f4").tobytes())
    vao = ctx.simple_vertex_array(program, buffer, "position")
    fbo = ctx.simple_framebuffer((args.size, args.size), components=4)
    fbo.use()
    program["resolution"] = (args.size, args.size)
    program["rotation"] = (1,0,1,0)
    program["steps"] = 88
    frames = []
    report = {"renderer": ctx.info["GL_RENDERER"], "size": args.size,
              "note": "EGL arithmetic translation. Neither native OBS parser nor Windows D3D11 timing.", "cases": []}
    cases = [("Repos",0,0,(0,0,0,0)), ("Matière / basses",0.20,0.1,(0.7,0.8,0.5,0.3)),
             ("Ouverture / médiums",0.0,0.92,(0.1,0.6,0.8,0.6)), ("Division",1.0,0.15,(0.1,0.8,0.6,0.5))]
    for idx, (label, split, opening, audio) in enumerate(cases):
        program["audio"] = audio
        program["shape"] = (3.2, split, opening, 1)
        start = time.perf_counter()
        vao.render(moderngl.TRIANGLE_STRIP)
        ctx.finish()
        elapsed = (time.perf_counter()-start)*1000
        rgba = np.frombuffer(fbo.read(components=4), dtype=np.uint8).reshape(args.size,args.size,4)[::-1].copy()
        a = rgba[:,:,3]
        assert (a == 0).any() and (a == 255).any(), "Transparency or opaque surface missing"
        assert np.all(rgba[:,:,:3] <= a[:,:,None]+0), "Output must be premultiplied"
        assert np.max(a[:4]) == np.max(a[-4:]) == np.max(a[:,:4]) == np.max(a[:,-4:]) == 0, "Geometry touches boundary"
        parts = components(a > 128)
        assert len(parts) >= 3 if idx == 3 else True, "The divided state must have disconnected bodies"
        if idx == 2:
            assert a[args.size//2,args.size//2] == 0, "Opening must go through the object"
        straight = rgba.copy()
        straight[:,:,:3] = np.clip(rgba[:,:,:3].astype(float)*255/np.maximum(a[:,:,None],1),0,255).astype("uint8")
        Image.fromarray(straight).save(args.output/f"state-{idx}.png")
        yy, xx = np.indices(a.shape)
        checker = np.where(((xx//24+yy//24)%2)[:,:,None], np.array([30,33,40]), np.array([20,23,29]))
        composite = np.clip(rgba[:,:,:3].astype(float)+checker*(1-a[:,:,None]/255),0,255).astype("uint8")
        im = Image.fromarray(composite)
        frames.append(im)
        report["cases"].append({"label": label, "wall_ms_software_renderer": round(elapsed,2),
                                "components": parts, "opaque_pixels": int((a==255).sum()),
                                "partial_alpha_pixels": int(((a>0)&(a<255)).sum())})
    w = args.size
    sheet = Image.new("RGB", (w*2,(w+64)*2), (12,14,19))
    draw = ImageDraw.Draw(sheet)
    font_path = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"
    font = ImageFont.truetype(font_path,24)
    for i, im in enumerate(frames):
        x, y = (i%2)*w, (i//2)*(w+64)
        draw.text((x+24,y+17),cases[i][0],font=font,fill=(226,229,237))
        sheet.paste(im,(x,y+64))
    sheet.save(args.output/"CHROME_MORPH_preview.png")
    if args.animation:
        encoder = subprocess.Popen(["ffmpeg","-y","-loglevel","error","-f","rawvideo","-pix_fmt","rgb24",
            "-s",f"{args.size}x{args.size}","-r","24","-i","-","-an","-c:v","libx264","-crf","20",
            "-pix_fmt","yuv420p","-movflags","+faststart",str(args.output/"CHROME_MORPH_demo.mp4")],stdin=subprocess.PIPE)
        for frame in range(144):
            t = frame/24
            split = (0.5-0.5*np.cos(t*np.pi/3))**1.5
            opening = 0.85*(1-split)
            program["shape"] = (t*0.6,float(split),float(opening),1)
            program["audio"] = (float(0.3*np.exp(-(t*2.5%1)*12)),0.75,0.65,0.45)
            angle = np.sin(t)*0.2
            program["rotation"] = (float(np.cos(angle)),float(np.sin(angle)),1,0)
            vao.render(moderngl.TRIANGLE_STRIP)
            rgba = np.frombuffer(fbo.read(components=4),dtype=np.uint8).reshape(args.size,args.size,4)[::-1].copy()
            a = rgba[:,:,3]
            bg = np.zeros_like(rgba[:,:,:3])+np.array([13,16,23],dtype="uint8")
            rgb = np.clip(rgba[:,:,:3].astype(float)+bg*(1-a[:,:,None]/255),0,255).astype("uint8")
            im = Image.fromarray(rgb)
            ImageDraw.Draw(im).text((20,20),"CHROME MORPH · Démo sans audio réel",font=ImageFont.truetype(font_path,18),fill=(207,214,230))
            encoder.stdin.write(np.asarray(im).tobytes())
        encoder.stdin.close()
        assert encoder.wait() == 0
    (args.output/"render-report.json").write_text(json.dumps(report,indent=2,ensure_ascii=False)+"\n")
    print(json.dumps(report,indent=2,ensure_ascii=False))


if __name__ == "__main__":
    main()
