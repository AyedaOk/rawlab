# Darktable temporary batch protocol v1

`rawlab-app --external-batch REQUEST` opens the first image for editing. Its Export
action runs all requested images in one session with a temporary queue. It neither
loads nor overwrites the standalone recovery queue and does not acquire its lock.

The LF-terminated request starts with `rawlab-batch-v1`. Following lines contain
lowercase hex-encoded UTF-8 absolute source and output paths, separated by a tab.
Duplicate paths, outputs replacing inputs and malformed requests are rejected.

`REQUEST.results` is written atomically. It starts with `rawlab-results-v1`, then
`partial` or `complete`, followed by completed source/output pairs in the same
encoding. Lua validates records and imports finished DNGs on return from RawLab,
without resetting existing imports. Creating `REQUEST.results.cancel` requests
checkpoint cancellation. Requests and receipts are removed after returning.

Exit codes: 0 = complete, 2 = invalid request, 3 = cancelled or incomplete. Partial
outputs may be imported after any exit. There is no plugin resume UI or persistent
request pointer. To process unfinished pictures, start a new selection. Standalone
RawLab batches retain their own restart recovery.
