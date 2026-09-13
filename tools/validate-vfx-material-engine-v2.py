from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
CPP = ROOT / "src/audio-shader-source.cpp"
HPP = ROOT / "src/includes/audio-shader-source.hpp"
FX = ROOT / "data/effects/vfx-material-engine-v2-pbr-sphere.effect"
INI = ROOT / "data/effects/vfx-material-engine-v2-pbr-sphere.effect.ini"

errors = []


def require(cond, msg):
    if not cond:
        errors.append(msg)


def balanced(text, left, right):
    return text.count(left) == text.count(right)

cpp = CPP.read_text(encoding="utf-8")
hpp = HPP.read_text(encoding="utf-8")
fx = FX.read_text(encoding="utf-8")
ini = INI.read_text(encoding="utf-8")

# Core C++ invariants.
require('#include <graphics/image-file.h>' in hpp, 'missing OBS image-file include')
require('std::array<gs_image_file_t, 6> material_images' in hpp, 'missing six material images')
require(cpp.count('load_material_textures_if_needed(s);') == 1, 'material texture loader must run exactly once per render path')
require(cpp.count('destroy_material_textures(s);') == 1, 'material textures must be destroyed exactly once')
for token in [
    'material_albedo_texture', 'material_normal_texture', 'material_roughness_texture',
    'material_metallic_texture', 'material_height_texture', 'material_environment_texture',
    'material_raymarch_steps', 'material_quality', 'material_triplanar_scale'
]:
    require(token in cpp, f'missing C++ material binding: {token}')

# Shader structural checks. These do not replace OBS runtime compilation, but catch
# accidental code-generation corruption before cross-platform builds are packaged.
require(balanced(fx, '{', '}'), 'shader braces are unbalanced')
require(balanced(fx, '(', ')'), 'shader parentheses are unbalanced')
require('technique Draw' in fx, 'shader has no Draw technique')
require(fx.count('float3 V=normalize(-rd);') == 1, 'shader view vector V must be declared exactly once')
require(fx.count('const int MAX_STEPS=112;') == 1, 'shader MAX_STEPS guard missing or duplicated')
require('material_raymarch_steps' in fx, 'quality raymarch budget uniform missing')
require('DistributionGGX' in fx and 'GeometrySmith' in fx and 'FresnelSchlick' in fx, 'GGX PBR functions missing')
require('sampleEnvironment' in fx and 'envUV' in fx, 'environment mapping path missing')
require('sampleAlbedo' in fx and 'sampleHeight' in fx and 'applyNormalMap' in fx, 'triplanar material sampling path incomplete')
require('audio_beat' in fx, 'independent beat envelope missing from material shader')

for bad in ['ffloat', 'float3 float3', ';;;', '\x00']:
    require(bad not in fx, f'shader contains known corruption token: {bad!r}')

# Metadata should expose the expected first-generation material controls and Rendu Clean 1 audio defaults.
for label in ['Object Scale', 'Liquid Flow', 'HEARTBEAT Depth', 'Environment Reflection', 'Master Audio React']:
    require(label in ini, f'missing shader UI label: {label}')
for setting in ['react_db=-63', 'peak_db=-20', 'attack_ms=27', 'release_ms=0', 'fft_size=4096', 'band_count=64']:
    require(setting in ini, f'missing Rendu Clean 1 default: {setting}')

# Crude duplicate function declaration protection for hand-edited effect code.
function_names = re.findall(r'^(?:float|float2|float3|float4|VertOut)\s+([A-Za-z_]\w*)\s*\(', fx, re.M)
duplicates = sorted({name for name in function_names if function_names.count(name) > 1})
require(not duplicates, 'duplicate shader functions: ' + ', '.join(duplicates))

if errors:
    print('VFX Material Engine v2 validation FAILED:')
    for error in errors:
        print(' -', error)
    sys.exit(1)

print('VFX Material Engine v2 static validation: PASS')
print(' - C++ material bindings/lifecycle: OK')
print(' - shader structure: OK')
print(' - GGX/triplanar/environment paths: OK')
print(' - Rendu Clean 1 metadata: OK')
