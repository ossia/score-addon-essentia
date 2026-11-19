#!/usr/bin/env python3
"""Generate the native CMake sources for Essentia's libossia/libsamplerate profile."""
import argparse
from contextlib import redirect_stdout
from io import StringIO
from pathlib import Path
import sys


# Keep the pinned Waf lightweight registry without optional integrations.
# The patch retains the original FFTW registry classes but uses libossia in them.
EXCLUDED = {
    # Other FFT implementations
    "FFTK", "IFFTK", "FFTKComplex", "IFFTKComplex",
    "FFTA", "IFFTA", "FFTAComplex", "IFFTAComplex",
    # FFmpeg, TagLib, libyaml, Gaia, and Chromaprint
    "AudioLoader", "MonoLoader", "EqloudLoader", "EasyLoader", "MonoWriter", "AudioWriter",
    "MetadataReader", "MusicExtractor", "FreesoundExtractor", "YamlInput", "YamlOutput",
    "GaiaTransform", "MusicExtractorSVM", "Chromaprinter",
    # TensorFlow and ONNX Runtime
    "TensorflowPredict", "TensorflowPredictMusiCNN", "TensorflowPredictVGGish",
    "TensorflowPredictTempoCNN", "TensorflowPredictCREPE", "PitchCREPE", "TempoCNN",
    "TensorflowPredictEffnetDiscogs", "TensorflowPredict2D", "TensorflowPredictFSDSINet",
    "TensorflowPredictMAEST", "OnnxPredict",
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    source = args.source.resolve()
    output = args.output.resolve()

    sys.dont_write_bytecode = True
    from patch_essentia import REVISION

    sys.path.insert(0, str(source))
    from utils.algorithms_info import create_registration_cpp, create_version_h, get_all_algorithms

    src = source / "src"
    # The upstream parser warns about non-algorithm helper headers in this tree.
    diagnostics = StringIO()
    try:
        with redirect_stdout(diagnostics):
            algorithms = get_all_algorithms(str(src / "algorithms"), root_dir=str(src))
    except (Exception, SystemExit):
        sys.stderr.write(diagnostics.getvalue())
        raise

    selected = {name: algorithms[name] for name in sorted(algorithms.keys() - EXCLUDED)}
    sources = []
    for algo in selected.values():
        algo["header"] = Path(algo["header"]).as_posix()
        cpp = src / algo["source"]
        if not cpp.is_file():
            raise SystemExit(f"Selected Essentia algorithm implementation not found: {cpp}")
        sources.append(cpp.as_posix())

    output.mkdir(parents=True, exist_ok=True)
    create_version_h(output / "version.h", (source / "VERSION").read_text().strip(), REVISION)
    create_registration_cpp(selected, output / "essentia_algorithms_reg.cpp", use_streaming=True)
    (output / "sources.cmake").write_text(
        "set(score_essentia_algorithm_sources\n"
        + "".join(f"  [=[{cpp}]=]\n" for cpp in sources)
        + ")\n",
        encoding="utf-8",
    )


if __name__ == "__main__":
    main()
