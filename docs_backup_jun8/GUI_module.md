# GUI Module

## Introduction

Management and monitoring interface for all modules and functions of the StereoCamera module.

## Rules & Design

- The GUI design shall follow the rules in `../../Coding/GUI_rule.md`.
- Use **Three.js** for point cloud and 3D visualization.

## Pages

### Overview

| Region | Description |
|--------|-------------|
| 2D Image Display | Shows stereo camera images |
| 3D Point Cloud | Displays real-time 3D point cloud |
| General Status | Camera connection status, client connection status |
| Buttons | Connect, Disconnect |

### 3D Viewer

Dedicated page for 3D point cloud and depth map visualization.

### 2D Viewer

Dedicated page for stereo image viewing.

### IMU Viewer

Dedicated page for IMU data visualization.

### Configuration

Parameter and configuration management interface. See [parameter.md](parameter.md).
