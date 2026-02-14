# Dypiro OS
A simple open-source x64 bit hobby os which has a simple terminal.
## Installation
```bash
$ sudo apt-get install xorriso build-essential qemu nasm 
$ make
```

## Usage
```bash
$ make run
```
## A useful debug command
```bash
$ qemu-system-x86_64 -M q35 -m 2G -d int -no-reboot -cdrom Dypiro-os.iso -boot d
```
Precompiled iso included
