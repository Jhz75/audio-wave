from pathlib import Path
p=Path('src/audio-shader-source.cpp')
s=p.read_text(encoding='utf-8')
old='''\tif (meta.render_scale_set) { obs_data_set_int(settings, S_RENDER_SCALE, valid_render_scale(meta.render_scale)); applied = true; }\n'''
new='''\tif (meta.render_scale_set) {\n\t\tconst int scale = (meta.render_scale == 25 || meta.render_scale == 50 || meta.render_scale == 75 || meta.render_scale == 100)\n\t\t\t\t  ? meta.render_scale\n\t\t\t\t  : 100;\n\t\tobs_data_set_int(settings, S_RENDER_SCALE, scale);\n\t\tapplied = true;\n\t}\n'''
assert s.count(old)==1
p.write_text(s.replace(old,new,1), encoding='utf-8')
print('fixed render scale helper ordering')
