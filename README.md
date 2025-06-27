# FileSync-Systems

Projects from my 3rd-year *Systems Programming* course at the **National and Kapodistrian University of Athens**.

This repository contains two file synchronization systems:

- **FSS (FileSync System)**: Synchronizes local directories using Linux system calls, processes, and inotify.
- **NFS (Network FileSync System)**: Synchronizes remote directories over the network using TCP sockets and threads.

---

## 🗂 Local FileSync System (`fss/`)

Monitors source directories and mirrors changes to target directories in real time. Built using:

- `inotify` for filesystem events
- `fork`/`exec` for spawning worker processes
- Named pipes for communication between manager and console

Supports commands like `add`, `sync`, `cancel`, `status`, and `shutdown`. Includes a bash script for reporting and cleanup.

📎 [See full details →](./fss/README.md)

---

## 🌐 Network FileSync System (`nfs/`)

Enables directory synchronization across machines over TCP. Components:

- `nfs_manager`: Coordinates tasks and threads
- `nfs_client`: Passive server for file operations
- `nfs_console`: User interface over TCP

Uses worker threads and a shared task queue to manage parallel file transfers. Clients support `LIST`, `PULL`, and `PUSH` commands.

📎 [See full details →](./nfs/README.md)
