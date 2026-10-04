"""Exercise settings and verified HTTPS catalog loading through the worker protocol."""
import json
import subprocess
import sys

for operation in ("settings", "catalog"):
    result = subprocess.run(
        [sys.argv[1], "--serve"], input=json.dumps({"operation": operation}) + "\n",
        text=True, capture_output=True, timeout=90,
    )
    assert result.returncode == 0, result.stderr
    responses = [json.loads(line) for line in result.stdout.splitlines()]
    assert responses and "error" not in responses[-1], (result.stdout, result.stderr)
    if operation == "catalog":
        assert responses[-1].get("catalog"), result.stdout
    print(f"Packaged worker {operation} request passed")
