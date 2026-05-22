# Theo Qt Helper

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![Release](https://img.shields.io/github/v/release/theo0r1z/theo-qt-helper?label=release)](https://github.com/theo0r1z/theo-qt-helper/releases)
[![Platform](https://img.shields.io/badge/Platform-Windows%20%7C%20Linux%20%7C%20macOS-lightgrey)](#building-from-source)
[![Qt](https://img.shields.io/badge/Qt-6.5%2B-41cd52)](https://www.qt.io/)
[![CMake](https://img.shields.io/badge/Build-CMake-064F8C)](https://cmake.org/)

**English** · [中文说明](#中文说明)

Theo Qt Helper is a cross-platform desktop application for browsing **Qt 6 Simplified Chinese** API documentation offline. Built with Qt 6 and CMake, it provides an Assistant-style workflow: table of contents, index, full-text search, bookmarks, and a multi-pane tabbed layout for reading and comparing topics.

| | |
|---|---|
| **Repository** | https://github.com/theo0r1z/theo-qt-helper |
| **Prebuilt package** | Windows x64 only — [GitHub Releases](https://github.com/theo0r1z/theo-qt-helper/releases) |
| **Source in this repo** | Application source only (no bundled `.qch` help) |

## Screenshots

![Main window — navigation, toolbar, and documentation view](docs/screenshots/main-window.png)

## Features

- Offline Qt Help (`.qch` / `.qhc`) with a Chinese UI
- Content tree, keyword index, full-text search, and bookmarks
- Multi-tab and multi-pane layouts with drag-and-drop split
- Index filter oriented toward classes and members
- Page zoom, print, always-on-top, and light/dark themes
- Session restore for open tabs and window layout

## Download (Windows x64)

1. Open [Releases](https://github.com/theo0r1z/theo-qt-helper/releases/latest).
2. Download `TheoQtHelper-<version>-win64-zh.zip`.
3. Extract the archive and run `TheoQtHelper.exe`.

The portable package includes the application, Qt runtime, and Qt 6.11 Simplified Chinese help under `docs/qt-6.11/qt-zh.qhc`. Windows 10/11 x64 is required; install `vc_redist.x64.exe` from the package if the app fails to start.

## Building from source

This project builds on **Windows**, **Linux**, and **macOS** with CMake and Qt 6.5+.

### Prerequisites

| Component | Version |
|-----------|---------|
| [Qt](https://www.qt.io/download) | 6.5+ — Widgets, Help, PrintSupport, Network, Svg |
| [CMake](https://cmake.org/) | 3.21+ |
| C++ toolchain | C++17 — MSVC 2022, GCC 11+, or Clang 14+ |
| [Ninja](https://ninja-build.org/) | Recommended |

You must supply your own Qt Help collection (`.qch` / `.qhc`) or build one with Qt’s help tools. The Chinese documentation bundle in the Windows release is **not** part of this repository.

### Windows (MSVC)

```powershell
git clone https://github.com/theo0r1z/theo-qt-helper.git
cd theo-qt-helper

cmake -S . -B build -G Ninja `
  -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_PREFIX_PATH="D:\Qt\6.11.0\msvc2022_64"

cmake --build build
```

Output: `release\TheoQtHelper.exe`

### Linux

```bash
git clone https://github.com/theo0r1z/theo-qt-helper.git
cd theo-qt-helper

cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release

cmake --build build
```

On Debian/Ubuntu, install development packages such as `qt6-base-dev`, `qt6-tools-dev`, `libqt6help6`, `libqt6svg6-dev`, and `libqt6printsupport6`.

Output: `release/TheoQtHelper`

### macOS

```bash
git clone https://github.com/theo0r1z/theo-qt-helper.git
cd theo-qt-helper

brew install qt@6 ninja cmake

cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$(brew --prefix qt@6)"

cmake --build build
```

Output: `release/TheoQtHelper.app` (or `release/TheoQtHelper` depending on the generator)

### Continuous integration

Every push and pull request is built on Linux, Windows, and macOS via [GitHub Actions](.github/workflows/ci.yml).

## Project layout

```
.github/workflows/   CI (Linux, Windows, macOS)
CMakeLists.txt       Build system
src/                 Application source
docs/screenshots/    README screenshots
docs/                Legal notice for bundled Qt docs (release package)
LICENSE              MIT license (application)
VERSION              Project version
```

## Qt documentation and trademarks

Qt®, Qt Assistant®, and related names are trademarks of The Qt Company Ltd. The Simplified Chinese documentation in the Windows release is derived from [doc.qt.io](https://doc.qt.io/qt-6/zh/) with technical corrections to API signatures only. See [docs/QT_DOC_NOTICE.md](docs/QT_DOC_NOTICE.md).

## License

Copyright © 2025 Theo Zhao. Released under the [MIT License](LICENSE).

## Author

**Theo Zhao** — [@theo0r1z](https://github.com/theo0r1z)

---

## 中文说明

[English](#theo-qt-helper)

**Theo Qt Helper** 是一款跨平台 **Qt 6 简体中文 API 文档** 离线阅读器，基于 Qt 6 与 CMake 构建。提供接近 Qt Assistant 的使用体验：目录、索引、全文检索、书签，以及多分栏标签页布局，便于对照阅读。

| | |
|---|---|
| **仓库** | https://github.com/theo0r1z/theo-qt-helper |
| **预编译包** | 仅 Windows x64 — [GitHub Releases](https://github.com/theo0r1z/theo-qt-helper/releases) |
| **本仓库内容** | 仅应用程序源码（不含 `.qch` 文档包） |

### 界面截图

![主界面 — 导航、工具栏与文档阅读区](docs/screenshots/main-window.png)

### 功能

- 离线 Qt Help（`.qch` / `.qhc`），中文界面
- 内容树、关键字索引、全文搜索、书签
- 多标签、多分栏，支持拖拽分屏
- 面向类与成员的索引过滤
- 页面缩放、打印、窗口置顶、浅色/深色主题
- 会话恢复（已打开标签与窗口布局）

### 下载（Windows x64）

1. 打开 [Releases](https://github.com/theo0r1z/theo-qt-helper/releases/latest)。
2. 下载 `TheoQtHelper-<version>-win64-zh.zip`。
3. 解压后运行 `TheoQtHelper.exe`。

便携包内含程序、Qt 运行库及 `docs/qt-6.11/qt-zh.qhc` 中文帮助。需要 Windows 10/11 64 位；若无法启动，可运行包内 `vc_redist.x64.exe`。

### 从源码构建

支持在 **Windows**、**Linux**、**macOS** 上使用 CMake 与 Qt 6.5+ 编译。

#### 依赖

| 组件 | 版本 |
|------|------|
| [Qt](https://www.qt.io/download) | 6.5+ — Widgets、Help、PrintSupport、Network、Svg |
| [CMake](https://cmake.org/) | 3.21+ |
| C++ 工具链 | C++17 — MSVC 2022、GCC 11+ 或 Clang 14+ |
| [Ninja](https://ninja-build.org/) | 推荐 |

需自行准备 Qt Help 集合（`.qch` / `.qhc`）。Windows Release 中的中文文档包**不在**本仓库内。

#### Windows（MSVC）

```powershell
git clone https://github.com/theo0r1z/theo-qt-helper.git
cd theo-qt-helper

cmake -S . -B build -G Ninja `
  -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_PREFIX_PATH="D:\Qt\6.11.0\msvc2022_64"

cmake --build build
```

输出：`release\TheoQtHelper.exe`

#### Linux

```bash
git clone https://github.com/theo0r1z/theo-qt-helper.git
cd theo-qt-helper

cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release

cmake --build build
```

Debian/Ubuntu 可安装 `qt6-base-dev`、`qt6-tools-dev`、`libqt6help6`、`libqt6svg6-dev`、`libqt6printsupport6` 等开发包。

输出：`release/TheoQtHelper`

#### macOS

```bash
git clone https://github.com/theo0r1z/theo-qt-helper.git
cd theo-qt-helper

brew install qt@6 ninja cmake

cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$(brew --prefix qt@6)"

cmake --build build
```

输出：`release/TheoQtHelper.app`（或 `release/TheoQtHelper`）

#### 持续集成

推送与 Pull Request 会在 Linux、Windows、macOS 上通过 [GitHub Actions](.github/workflows/ci.yml) 自动构建。

### 目录结构

```
.github/workflows/   CI（Linux / Windows / macOS）
CMakeLists.txt       构建系统
src/                 应用源码
docs/screenshots/    README 截图
docs/                Release 中文文档法律说明
LICENSE              应用 MIT 许可证
VERSION              版本号
```

### Qt 文档与商标

Qt®、Qt Assistant® 等为 The Qt Company Ltd. 的商标。Windows 发行包中的简体中文文档来源于 [doc.qt.io 中文版](https://doc.qt.io/qt-6/zh/)，仅对 API 签名等技术细节做了修正。详见 [docs/QT_DOC_NOTICE.md](docs/QT_DOC_NOTICE.md)。

### 许可证

Copyright © 2025 Theo Zhao。本项目采用 [MIT License](LICENSE)。

### 作者

**Theo Zhao** — [@theo0r1z](https://github.com/theo0r1z)
