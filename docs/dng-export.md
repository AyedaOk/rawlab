# DNG Export Notes

The target export is a non-demosaiced Bayer CFA DNG for Bayer raws, not a demosaiced RGB image.

## Current Workflow

Start with an isolated libtiff-based writer modeled on Darktable's CFA writer approach. Do not use the Adobe DNG SDK unless its license is verified as compatible with the project.

The stable CLI export command is:

```bash
./build/dev/src/rawlab_cli/rawlab process input.raw output.dng
./build/dev/src/rawlab_cli/rawlab process input.raw output.dng \
  --denoise model.onnx --blend 1.0 --clipped-highlights
```

Without `--denoise`, the command exports the decoded CFA directly. With a model,
it runs the production raw-domain pipeline before writing the Bayer CFA DNG.

## Metadata To Preserve

- `BlackLevel`, including per-site values where available.
- `WhiteLevel`.
- `CFARepeatPatternDim` and `CFAPattern`.
- `AsShotNeutral` from inverted camera white balance coefficients.
- `ColorMatrix1` from the source camera matrix.
- Make, model, and unique model strings.
- Active area/crop metadata when needed for correct re-import.
- Source ISO, aperture, exposure time, focal length, and capture time in a dedicated EXIF IFD.
- Lens make/model/specification/serial, camera body serial, artist, and image description when LibRaw exposes them.
- Embedded JPEG thumbnail and display preview for GUI exports.

GPS, MakerNotes, XMP, and arbitrary source structural tags are intentionally not copied. This keeps the generated DNG layout valid and avoids silently carrying location metadata.

## Validation

Validate generated DNGs with external tools where available:

- `tiffinfo`
- `exiftool`
- RawTherapee
- darktable

Document required tags, uncertain tags, and reader compatibility findings here as they are discovered.

The first synthetic DNG was validated with `tiffinfo` and `exiftool`; both identify it as a DNG with `PhotometricInterpretation=CFA` and CFA pattern `[Red,Green][Green,Blue]`.

The first ARW-derived DNG from a Sony `ILCE-7CM2` file was validated with `exiftool`; it reports `PhotometricInterpretation=Color Filter Array`, RGGB CFA pattern, source make/model, active area, black/white levels, normalized `AsShotNeutral`, and `ColorMatrix1`. Capture exposure bias and safe lens metadata are written through a referenced EXIF IFD when available.

## Known Risk

Valid CFA DNG export requires correct CFA layout tags, black/white levels, active area, color metadata, orientation, and sample packing. Treat this as a proof-of-concept milestone, not a routine TIFF export.

LinearRaw DNG is a separate later path for non-Bayer sensors or linear RGB models. It must not replace the Bayer CFA DNG path.
