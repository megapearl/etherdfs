# EtherDFS Server for Docker

This is a containerized version of [EtherDFS](http://etherdfs.sourceforge.net/) (ethersrv-linux), designed to run on NAS systems like TrueNAS Scale.

EtherDFS creates a Layer 2 (Raw Ethernet) drive mapping for MS-DOS clients.

## ⚠️ Networking Requirements
EtherDFS operates on raw Ethernet frames (EtherType `0xEDF5`). It **does not use IP addresses**.
* **Must use `network_mode: host`**: NAT or Bridge networking will block the frames.
* **Interface Binding**: You must specify the *physical* interface of the host (e.g., `eno1`, `eth0`, `br0`).

## Usage

### Docker Compose (TrueNAS Scale)

```yaml
services:
  etherdfs:
    image: ghcr.io/YOUR_GITHUB_USER/etherdfs-docker:latest
    container_name: etherdfs
    network_mode: host
    cap_add:
      - NET_RAW
      - NET_ADMIN
    volumes:
      - /mnt/tank/dos_games:/data
    # Syntax: ethersrv-linux <HOST_INTERFACE> <CONTAINER_PATH>
    command: ethersrv-linux eno1 /data
    restart: unless-stopped
