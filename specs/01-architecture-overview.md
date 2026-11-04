# Spec 01: Architecture Overview & Project Guidelines

## 1. Overview
**Kai** is a native productivity platform and execution engine for
developers. It centralizes Shell command execution, HTTP requests (API
client), environment variable injection scoped by folder/project, and
workflow automation (hooks), with global shortcuts, custom themes, and a
project selector.

## 2. Tech stack
- **Language:** C++20 (or newer).
- **Framework:** Qt 6 (Qt Widgets, for performance, a low memory footprint,
  and a native/customizable look).
- **Networking and processes:** `QProcess`, `QNetworkAccessManager`.
- **Global shortcuts:** QHotkey.
- **Interactive terminal:** libvterm (VT100/xterm emulation) for the
  per-command TUI mode.

## 3. Module architecture (C++)
1. **Core engine:** state management, config storage, event bus.
2. **Project & folder manager:** reading, importing and linking local
   project folders and environment scopes.
3. **Execution engine:** async subprocess abstraction (`QProcess`) and
   pipeline orchestration (hooks, execution conditions).
4. **HTTP engine:** async requests and dynamic JSON payload extraction into
   environment variables.
5. **Environment manager:** variable hierarchy and injection (Global →
   Folder/Project → Dynamic → Command).
6. **Theme manager:** styling via theme files (JSON/QSS) with live reload.
7. **UI layer:** native window frame, project selector, fuzzy search, tabs
   and the embedded output panel / interactive terminal.
8. **IPC / CLI layer (`kai-ipc`):** a `QLocalServer` (named pipe on
   Windows, unix socket on Linux) that also guarantees a **single
   instance**. A thin command-line client (`kai run <cmd>`, `kai list`,
   `kai env list|use <env>`, `kai ps`, `kai attach`, `kai kill`, `kai
   show`) talks to the running instance, reusing the `ExecutionPipeline`
   and `Environments`. Line-delimited JSON protocol. Inspired by `copyq` as
   a client of its own daemon.

## 4. Code guidelines
- **Asynchronicity:** no disk I/O, process, or network operation may block
  the main (GUI) thread.
- **Memory management:** RAII and smart pointers (`std::unique_ptr`,
  `std::shared_ptr`).
- **Decoupling:** business-logic modules and the UI communicate through
  Qt signals and slots (`QObject::connect`).
