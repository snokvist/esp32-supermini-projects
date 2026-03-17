---
paths:
  - "projects/waybeam-connect/src/main.cpp"
---

This file contains one of 4 independent CRSF CRC8/channel-packing implementations across the Waybeam ecosystem. The CRC8 DVB-S2 lookup table (PROGMEM) must be byte-identical to the canonical table in the coordination repo's protocols/crsf-rc.md. Channel packing uses 11-bit little-endian bitstream. Tick-to-microsecond: ((ticks-992)*5)/8 + 1500. Frame constants: 0xC8 address, 0x16 type, 22-byte payload. Any changes require running /audit-protocols from the coordination repo to verify all 4 implementations stay in sync.
