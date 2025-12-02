# IL2CPP METADATA DUMPER (DMA)

**DESIGNED FOR REVERSE ENGINEERING, RESEARCH, AND EDUCATIONAL PURPOSES ONLY**

Dumper attaches to EFT and extracts IL2CPP class metadata.

---

## Requirements

### Hardware / Acquisition

Physical memory acquisition setup supported by LeechCore/MemProcFS, for example:

- PCIe DMA hardware supported by **PCILeech / LeechCore** (FPGA, USB3380, etc.)
- MemProcFS supported software acquisition method (DumpIt, WinPMEM, etc.)

Refer to ufrisk projects for supported devices and methods:

- LeechCore: <https://github.com/ufrisk/LeechCore>
- MemProcFS: <https://github.com/ufrisk/MemProcFS>
- PCILeech: <https://github.com/ufrisk/pcileech>

### Libraries / DLLs

You must have compatible versions of:

- `vmm.dll` (MemProcFS native C/C++ API)
- `leechcore.dll` (LeechCore acquisition library)

Notes:
- Check the **`leechcore`** and **`vmmdll`** headers shipped with the libraries you're using to confirm the expected versions and exported symbols
