# DNG Export Notes

The target export is a non-demosaiced Bayer CFA DNG for Bayer raws, not a demosaiced RGB image.

## Initial Direction

Start with an isolated libtiff-based writer modeled on Darktable's CFA writer approach. Do not use the Adobe DNG SDK unless its license is verified as compatible with the project.

The Milestone 3 writer should accept a `uint16` Bayer CFA buffer plus source raw metadata and write `PhotometricInterpretation=CFA`.

Current proof-of-concept command:

```bash
./build/dev/src/rawlab_cli/rawlab write-synthetic-dng build/dev/synthetic-cfa.dng
```

This writes a tiny synthetic RGGB CFA DNG for structural validation before real ARW-derived export is attempted.

Real raw-derived CFA export command:

```bash
./build/dev/src/rawlab_cli/rawlab export-cfa-dng input.raw output.dng
```

## Metadata To Preserve

- `BlackLevel`, including per-site values where available.
- `WhiteLevel`.
- `CFARepeatPatternDim` and `CFAPattern`.
- `AsShotNeutral` from inverted camera white balance coefficients.
- `ColorMatrix1` from the source camera matrix.
- Make, model, and unique model strings.
- Active area/crop metadata when needed for correct re-import.
- Optional EXIF and embedded JPEG preview later.

## Validation

Validate generated DNGs with external tools where available:

- `tiffinfo`
- `exiftool`
- RawTherapee
- darktable

Document required tags, uncertain tags, and reader compatibility findings here as they are discovered.

The first synthetic DNG was validated with `tiffinfo` and `exiftool`; both identify it as a DNG with `PhotometricInterpretation=CFA` and CFA pattern `[Red,Green][Green,Blue]`.

The first ARW-derived DNG from a Sony `ILCE-7CM2` file was validated with `exiftool`; it reports `PhotometricInterpretation=Color Filter Array`, RGGB CFA pattern, source make/model, active area, black/white levels, normalized `AsShotNeutral`, and `ColorMatrix1`.

## Known Risk

Valid CFA DNG export requires correct CFA layout tags, black/white levels, active area, color metadata, orientation, and sample packing. Treat this as a proof-of-concept milestone, not a routine TIFF export.

LinearRaw DNG is a separate later path for non-Bayer sensors or linear RGB models. It must not replace the Bayer CFA DNG path.
