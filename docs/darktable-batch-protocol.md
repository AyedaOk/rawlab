# Darktable external batch protocol v1

The Tauri desktop accepts `--external-batch REQUEST` and opens the first image for
editing. Export runs all requested images in one session with a temporary queue. It
neither loads nor overwrites the standalone recovery queue.

The LF-terminated request starts with `rawlab-batch-v1`. Following lines contain
lowercase hex-encoded UTF-8 absolute source and output paths, separated by a tab.
Duplicate paths, outputs replacing inputs, and malformed requests are rejected.

`REQUEST.results` is written atomically. It starts with `rawlab-results-v1`, then
`partial` or `complete`, followed by completed source/output pairs in the same
encoding. Lua validates records and imports finished DNGs on return from RawLab,
without resetting existing imports. Creating `REQUEST.results.cancel` requests
checkpoint cancellation. Requests and receipts are removed after successful import.

Exit codes: 0 = complete, 2 = invalid request, 3 = cancelled or incomplete. Partial
outputs may be imported after any exit. There is no plugin resume UI or persistent
request pointer. To process unfinished pictures, start a new selection. Standalone
RawLab batches retain their own restart recovery.

Requests may not exceed 4 MiB or 10,000 jobs. They must end with LF. Encoded paths
must decode to nonempty absolute UTF-8 paths without NUL bytes. Duplicate sources,
duplicate outputs, outputs that replace any input, existing outputs, and destinations
with an existing `.xmp` sidecar are rejected. Publication never replaces a file.

RawLab checks the cancellation marker between jobs and during processing checkpoints.
A partial receipt contains completed jobs only. Every receipt pair must match a
requested pair before the Lua script imports it.

Protocol v1 transports paths, completion state, and cancellation only. Darktable owns
ratings, rejection state, color labels, and tags; the Lua script copies them from the
source catalog entry to each newly imported DNG. RawLab does not modify source RAW or
XMP files and does not embed source XMP in its output. Both RawLab and `RawLab.lua`
must change together if an incompatible protocol version is introduced.
