# Server Guide

## Server 1: ubuntu-home01

| Item | Value |
|------|-------|
| Server ID | 1 |
| Server Name | ubuntu-home01 |
| IP Address | 100.86.203.33 |
| Protocol | SSH |
| Username | `irlovan` |

### Remote Directories

| Purpose | Path |
|---------|------|
| Project Folder | `/home/irlovan/Downloads/Project/One` |
| Software Packages | `/home/irlovan/Downloads/Software` |

### Quick Commands

```bash
ssh irlovan@100.86.203.33
cd /home/irlovan/Downloads/Project/One          # Navigate to project folder
cd /home/irlovan/Downloads/Software              # Navigate to software folder
```

---

## Server 2: EDGE01

| Item | Value |
|------|-------|
| Server ID | 2 |
| Server Name | EDGE01 |
| IP Address | 100.85.117.73 |
| Protocol | SSH |
| Username | `user` |
| Password | `$EDGE01_PASSWORD` |

### Remote Directories

| Purpose | Path |
|---------|------|
| Project Folder | `/home/user/ECIDS` |
| StereoCamera | `/home/user/ECIDS/StereoCamera` |
| ZEDVisionSDK | `/home/user/ECIDS/ZEDVisionSDK` |

### Quick Commands

```bash
ssh user@100.85.117.73
cd /home/user/ECIDS                              # Navigate to project folder
cd /home/user/ECIDS/StereoCamera                 # Navigate to StereoCamera
cd /home/user/ECIDS/ZEDVisionSDK                 # Navigate to ZEDVisionSDK
```

---

## Git Repositories

| Server ID | Local Folder | Remote Folder | GitHub Repo |
|-----------|-------------|---------------|-------------|
| 2 | `StereoCamera` | `/home/user/ECIDS/StereoCamera` | https://github.com/irlovanwon/Lib.StereoCamera |
| 2 | `ZEDVisionSDK` | `/home/user/ECIDS/ZEDVisionSDK` | https://github.com/irlovanwon/ZEDVisionSDK.git |
