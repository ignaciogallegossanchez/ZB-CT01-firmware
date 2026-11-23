#!/usr/bin/env python3
"""Falla si el binario no cabe en el hueco de la aplicacion."""

import sys
from pathlib import Path

image, limit = Path(sys.argv[1]), int(sys.argv[2], 0)
size = image.stat().st_size
if size > limit:
    sys.exit(f"{image.name}: {size} bytes, supera el limite de {limit} bytes")
print(f"{image.name}: {size} de {limit} bytes ({limit - size} libres)")
