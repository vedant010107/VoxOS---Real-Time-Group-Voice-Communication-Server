# VoxOS - Real-Time Group Voice Communication Server

VoxOS is a multi-client real-time voice chat server built in C. Designed as an Operating Systems course mini-project, it focuses on low-latency audio transmission, concurrent connection handling, and strict synchronization mechanisms. It utilizes a dual-protocol architecture: TCP for reliable command handling and UDP for real-time audio transport.

## Features

- **Real-Time Audio**: Low-latency UDP audio streaming at 16kHz sample rate with 100ms buffering and continuous ALSA hardware synchronization.
- **Robust Audio Pipeline**: Implements server-side jitter buffers with playout delays to absorb network latency and Wi-Fi packet variance, preventing audio stuttering.
- **Concurrency & Synchronization**: Uses thread pools, mutexes, semaphores, and `SCHED_FIFO` scheduling for concurrent audio mixing. High-precision monotonic clocks prevent hardware underruns.
- **Thread Management**: Explicit thread separation for microphone capture and speaker playback to resolve device contention.
- **Reliable Data Storage**: Ensures data consistency through `fcntl` file locking, Write-Ahead Logging (WAL), and sequence numbered packets.
- **IPC Mechanisms**: Uses shared memory for zero-copy transfers and FIFOs for emergency shutdown signaling.
- **Interactive Command CLI**:
  - **TCP Commands**: `LOGIN`, `CREATE`, `JOIN`, `LEAVE`, `LIST`, `STATUS`, `LOGOUT`
  - **Audio Commands**: `START` (listen only), `UNMUTE` (transmit audio), `MUTE` (stop transmitting)

## Prerequisites

- GCC Compiler
- `make` utility
- ALSA utilities (`alsa-utils` providing `aplay` and `arecord` for the client)
- POSIX-compliant OS (Linux recommended)

## Build Instructions

To compile the project, run:

```bash
make
```

This will create two executables in the `bin/` directory:
- `bin/voxos_server`
- `bin/voxos_client`

To clean the build files:
```bash
make clean
```

## Usage

### Starting the Server

```bash
./bin/voxos_server [port]
```
If no port is specified, it defaults to a pre-configured port.

### Starting a Client

```bash
./bin/voxos_client <server_ip> <port>
```

### Client Workflow Example

1. Start the client and connect to the server's IP and Port.
2. Authenticate or interact with the server using standard TCP commands (e.g., `JOIN`).
3. Type `START` to begin the audio listener thread (Mic is OFF by default, you will only hear others).
4. Type `UNMUTE` to open your microphone and start transmitting audio to the channel.
5. Type `MUTE` to stop transmitting while still listening to others.
6. Type `QUIT` or `EXIT` to safely disconnect and exit.

## Future Improvements
- Advanced noise cancellation integration.
- Scalability enhancements for massive multi-user support.

## Context
This project was developed as an Operating Systems mini-project to demonstrate real-world applications of multithreading, inter-process communication, hardware synchronization, and socket programming.