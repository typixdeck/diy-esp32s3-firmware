"""Explicit sanitizer selection for host checks, independent of board model."""
import os


def sanitizer_name():
    value = os.environ.get("TYPIX_HOST_SANITIZERS", "address,undefined")
    if value not in {"address,undefined", "undefined"}:
        raise ValueError("TYPIX_HOST_SANITIZERS must be address,undefined or undefined")
    return value


def sanitizer_flags():
    return ["-fsanitize=" + sanitizer_name(), "-fno-sanitize-recover=all"]
