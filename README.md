# fat12conv

A small C11 utility that converts FAT12 filesystems into FAT16.

## Dependencies
fat12conv does not have any dependencies.

## Usage
`fat12conv INPUT OUTPUT`

INPUT is the FAT12 filesystem to convert, and OUTPUT is where the
FAT16 filesystem will be written. In-place conversion is not supported.
To convert actual disks, use `/dev/<disk>` as INPUT and a temporary file
as output, then use `dd` to copy the output back  to the disk.

## Build instructions
fat12conv can be built like any other CMake project:
```
mkdir build
cd build
cmake ..
cmake --build .
```
