# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Overview

This repository contains a CXL (Compute Express Link) sideband communication implementation featuring MCTP (Management Component Transport Protocol) and PLDM (Platform Level Data Model) functionality. The codebase provides kernel module development for MCTP bridging and userspace tools for CXL CCI (Compute Express Link Configuration Interface) communication.

## Project Structure

- `sfx/driver/`: Main driver and userspace code
  - `mctp_bridge.c`: Linux kernel module for MCTP bridging
  - `pldm.c/h`: PLDM message handling implementation
  - `mctp.h`: MCTP packet definitions
  - `cxl_cci.h`: CXL CCI message format definitions
  - `send.c`: MCTP send/receive demo application
  - `Makefile`: Build configuration for kernel module and userspace tools
  - Various shell scripts for setup and testing

- `build.md`: Build instructions for external dependencies (MCTP toolkit, PLDM tools)

## Build Commands

### Kernel Module Build
```bash
cd sfx/driver
make -C /lib/modules/$(uname -r)/build M=$(pwd) modules
sudo insmod mctp_bridge.ko
```

### Userspace Tools
```bash
cd sfx/driver
gcc -o demo demo.c
gcc -o send send.c
```

### External Dependencies (from build.md)
- **MCTP Userspace Toolkit**: `git clone https://github.com/CodeConstruct/mctp`
- **PLDM Tools**: `git clone https://github.com/openbmc/pldm.git`
- **libpldm**: `git clone https://github.com/openbmc/libpldm`
- **CXL CCI Library**: `git clone https://github.com/computexpresslink/libcxlmi.git`

Build uses Meson build system for external tools.

## Architecture Overview

### MCTP Bridge Driver (`mctp_bridge.c`)
- Linux kernel module providing MCTP protocol bridging
- Creates character device for userspace communication
- Implements network device interface with MCTP protocol
- Handles packet transfer between kernel and userspace using sk_buff queues
- Supports both TX and RX paths for MCTP packets

### PLDM Protocol Implementation (`pldm.c/h`)
- Implements PLDM (Platform Level Data Model) message handling
- Supports base PLDM commands (GetTID, GetPLDMVersion)
- Handles MCTP packet encapsulation for PLDM messages
- Provides request/response message handling with proper header construction

### CXL CCI Support (`cxl_cci.h`)
- Defines CXL Configuration Interface message format (CXL r3.1 Figure 7-19)
- Supports CXL sideband communication over MCTP transport
- Message category/tag/command structure definitions

### Communication Flow
1. **Kernel Module**: MCTP bridge creates `/dev/mctp_bridge0` device
2. **Userspace**: Applications read/write MCTP packets to character device
3. **PLDM Handler**: Processes PLDM messages received via MCTP
4. **CXL CCI**: Optional extension for CXL management commands

## Development Notes

- The codebase targets Linux kernel 6.14+ with MCTP protocol support
- Uses standard Linux kernel driver APIs and char device interfaces
- PLDM implementation follows DMTF (Distributed Management Task Force) specifications
- Debug logging available via `dev_dbg` macros in kernel module
- Build system supports both kernel module and userspace applications

## Testing Tools

- `send.c`: Demo application for sending/receiving MCTP packets
- `pldmtool` commands for PLDM protocol testing
- Kernel module creates `/dev/mctp_bridge0` for manual testing