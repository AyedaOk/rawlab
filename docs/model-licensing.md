# Model and dependency licensing

RawLab uses GPL-3.0-or-later. Keep license notices and attribution with copied or
adapted source, including the Darktable-derived demosaicing code.

Release packages include dependencies such as Qt, LibRaw, libtiff, ONNX Runtime,
and Lensfun where supported. Check the licenses and redistribution requirements of
the versions and build options shipped, including optional execution providers.
See `desktop/README.md` for frontend dependency licenses.

RawLab does not bundle pretrained model weights. A model's code, weights, and
training data can have different licenses; the application's GPL does not grant
rights to those materials. Check the upstream terms for each model before
redistributing it, including RawNIND, RawForge-derived models, and SAM 2.1.

For SAM 2.1, distinguish weight licensing from the separate SA-1B dataset terms.
Retain source and license information in model distributions and catalogs. This
document does not establish redistribution permission for any particular model.
