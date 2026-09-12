from pathlib import Path

CPP = Path("src/audio-shader-source.cpp")
HPP = Path("src/includes/audio-shader-source.hpp")


def replace_exact(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected exactly one match, found {count}")
    return text.replace(old, new, 1)


cpp = CPP.read_text(encoding="utf-8")

old_analysis = '''\tauto hz_energy = [&](float hz0, float hz1) {
\t\tconst float sample_rate = float(std::max(1, s->sample_rate));
\t\tconst float nyquist = sample_rate * 0.5f;
\t\thz0 = std::clamp(hz0, 0.0f, nyquist);
\t\thz1 = std::clamp(hz1, hz0, nyquist);
\t\tconst int bin0 = std::max(1, int(std::floor(hz0 * float(n) / sample_rate)));
\t\tconst int bin1 = std::min(usable_bins, int(std::ceil(hz1 * float(n) / sample_rate)));
\t\tif (bin1 <= bin0)
\t\t\treturn 0.0f;
\t\tfloat mag = 0.0f;
\t\tfor (int bin = bin0; bin < bin1; ++bin)
\t\t\tmag += std::abs(s->fft_work[(size_t)bin]);
\t\tmag /= float(bin1 - bin0);
\t\treturn db_to_norm(amp_to_db(mag / float(n)), s->react_db, s->peak_db);
\t};

\tconst float raw_sub = hz_energy(20.0f, 60.0f);
\tconst float raw_low = hz_energy(60.0f, 150.0f);
\tconst float raw_low_mid = hz_energy(150.0f, 500.0f);
\tconst float raw_mid_vfx = hz_energy(500.0f, 2000.0f);
\tconst float raw_high_mid = hz_energy(2000.0f, 6000.0f);
\tconst float raw_high = hz_energy(6000.0f, 16000.0f);
'''

new_analysis = '''\tauto hz_db = [&](float hz0, float hz1) {
\t\tconst float sample_rate = float(std::max(1, s->sample_rate));
\t\tconst float nyquist = sample_rate * 0.5f;
\t\thz0 = std::clamp(hz0, 0.0f, nyquist);
\t\thz1 = std::clamp(hz1, hz0, nyquist);
\t\tconst int bin0 = std::max(1, int(std::floor(hz0 * float(n) / sample_rate)));
\t\tconst int bin1 = std::min(usable_bins, int(std::ceil(hz1 * float(n) / sample_rate)));
\t\tif (bin1 <= bin0)
\t\t\treturn -120.0f;
\t\tfloat mag = 0.0f;
\t\tfor (int bin = bin0; bin < bin1; ++bin)
\t\t\tmag += std::abs(s->fft_work[(size_t)bin]);
\t\tmag /= float(bin1 - bin0);
\t\treturn amp_to_db(mag / float(n));
\t};

\t// VFX v1.1 musical calibration. Corrections are applied in dB before
\t// normalization so silence remains exactly at zero and the OBS React/Peak
\t// window keeps its expected meaning.
\tconst float sub_db = hz_db(20.0f, 60.0f);
\tconst float low_db = hz_db(60.0f, 150.0f);
\tconst float low_mid_db = hz_db(150.0f, 500.0f);
\tconst float mid_vfx_db = hz_db(500.0f, 2000.0f);
\tconst float high_mid_db = hz_db(2000.0f, 6000.0f);
\tconst float high_db = hz_db(6000.0f, 16000.0f);

\tconst float raw_sub = db_to_norm(sub_db - 1.0f, s->react_db, s->peak_db);
\tconst float raw_low = db_to_norm(low_db - 2.0f, s->react_db, s->peak_db);
\tconst float raw_low_mid = db_to_norm(low_mid_db, s->react_db, s->peak_db);
\tconst float raw_mid_vfx = db_to_norm(mid_vfx_db + 1.0f, s->react_db, s->peak_db);
\tconst float raw_high_mid = db_to_norm(high_mid_db + 2.0f, s->react_db, s->peak_db);
\tconst float raw_high = db_to_norm(high_db + 3.0f, s->react_db, s->peak_db);
'''

cpp = replace_exact(cpp, old_analysis, new_analysis, "frequency calibration block")
cpp = replace_exact(
    cpp,
    "\tconst float transient_target = clamp01(spectral_flux * 4.0f);",
    "\tconst float transient_target = clamp01(spectral_flux * 6.0f);",
    "transient x1.5",
)
cpp = replace_exact(
    cpp,
    "\tconst float kick_energy = clamp01(raw_sub * 0.45f + raw_low * 0.55f);",
    "\t// Keep kick detection on the uncalibrated SUB/LOW response so VFX band\n"
    "\t// balancing does not change the detector behaviour validated in v1.\n"
    "\tconst float kick_sub = db_to_norm(sub_db, s->react_db, s->peak_db);\n"
    "\tconst float kick_low = db_to_norm(low_db, s->react_db, s->peak_db);\n"
    "\tconst float kick_energy = clamp01(kick_sub * 0.45f + kick_low * 0.55f);",
    "kick calibration isolation",
)

for old, new, label in [
    ("\tobs_data_set_default_double(settings, S_REACT_DB, -66.0);", "\tobs_data_set_default_double(settings, S_REACT_DB, -82.0);", "React default"),
    ("\tobs_data_set_default_double(settings, S_PEAK_DB, -6.0);", "\tobs_data_set_default_double(settings, S_PEAK_DB, -28.0);", "Peak default"),
    ("\tobs_data_set_default_int(settings, S_ATTACK_MS, 25);", "\tobs_data_set_default_int(settings, S_ATTACK_MS, 14);", "Attack default"),
    ("\tobs_data_set_default_int(settings, S_RELEASE_MS, 180);", "\tobs_data_set_default_int(settings, S_RELEASE_MS, 140);", "Release default"),
    ("\tobs_data_set_default_int(settings, S_FFT_SIZE, 2048);", "\tobs_data_set_default_int(settings, S_FFT_SIZE, 4096);", "FFT default"),
]:
    cpp = replace_exact(cpp, old, new, label)

CPP.write_text(cpp, encoding="utf-8")

hpp = HPP.read_text(encoding="utf-8")
for old, new, label in [
    ("\tfloat react_db = -66.0f;", "\tfloat react_db = -82.0f;", "header React default"),
    ("\tfloat peak_db = -6.0f;", "\tfloat peak_db = -28.0f;", "header Peak default"),
    ("\tfloat attack_ms = 25.0f;", "\tfloat attack_ms = 14.0f;", "header Attack default"),
    ("\tfloat release_ms = 180.0f;", "\tfloat release_ms = 140.0f;", "header Release default"),
    ("\tint fft_size = 2048;", "\tint fft_size = 4096;", "header FFT default"),
]:
    hpp = replace_exact(hpp, old, new, label)

HPP.write_text(hpp, encoding="utf-8")
print("VFX v1.1 calibration applied successfully")
