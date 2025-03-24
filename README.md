# CEngine

CEngine is a memory scanning and modification tool designed for game modders, developers, and enthusiasts. It allows you to examine and modify game memory values in real-time through an intuitive graphical interface.

## ✨ Key Features

- **Advanced Memory Scanning**
  - Multi-threaded scanning with adaptive thread management
  - SIMD/vectorized operations support
  - Pattern-based and fuzzy value matching
  - Memory-mapped I/O optimization
  - Cache-optimized scanning patterns
  - Pointer chain detection and analysis
  - Configurable scan batching and buffering

- **Memory Protection**
  - Backup creation before writes
  - Write validation and verification
  - Stack and executable memory protection
  - Memory trap detection
  - Range validation and bounds checking
  - Security violation logging

- **Performance Optimization**
  - Adaptive thread count based on system load
  - Buffer pooling and memory prefetching
  - Configurable chunk sizes and alignments
  - Intel IPP support (optional)
  - Thread pinning to CPU cores

- **User Interface**
  - Dark/Light theme support
  - Compact and standard layouts
  - Customizable colors and highlighting
  - Real-time memory value updates
  - Integrated logging console
  - Memory map visualization
  - Process details viewer
  - Scan statistics display

## 📋 Value Types and Scanning

| Type | Size | Features |
|------|------|----------|
| Integer | 4 bytes | SIMD-accelerated scanning |
| Float | 4 bytes | Fuzzy value matching |
| Double | 8 bytes | High-precision scanning |
| Short | 2 bytes | Aligned/unaligned support |
| Byte | 1 byte | Pattern matching |
| Auto | Variable | Smart type detection |

## 🛠️ Advanced Settings

### Performance
- Thread count: 1-16 threads
- Scan buffer: 1-1024 MB
- Cache optimization
- Search timeout configuration
- Memory-mapped scanning
- Vectorized operations
- Batch processing
- Memory prefetching

### Security
- Write confirmation
- System process access control
- Privilege elevation management
- Memory protection handling
- Security event logging
- Range validation
- Crash prevention

### Memory Protection
- Write validation
- Stack protection
- Executable memory protection
- Memory backup
- Trap detection
- Protected region tracking

### UI Customization
- Dark/Light themes
- Custom color schemes
- Layout options
- Grid display
- Advanced options toggle
- Status and toolbar visibility

## 🔒 Security Features

- Memory write validation
- Protected region tracking
- Security violation logging
- Crash prevention measures
- System process protection
- Stack/executable memory protection
- Range checking enforcement

## ⚡ Performance Features

- SIMD-accelerated scanning
- Memory-mapped I/O
- Buffer pooling
- Memory prefetching
- Cache optimization
- Adaptive threading
- CPU core pinning
- Large page support

## 🛠️ Dependencies

- **ImGui**: Powers the user interface with a clean and responsive design
- **DirectX 11**: Provides hardware-accelerated rendering
- **Windows API**: Enables process interaction and memory access

## 💻 Building from Source

CEngine can be built using bat script

```bat
@echo off
cls

REM Kill any running instances of CEngine.exe
taskkill /F /IM CEngine.exe 2>nul
if errorlevel 1 (
    echo No running instances of CEngine.exe found.
) else (
    echo Terminated running CEngine.exe
    REM Add a small delay to ensure the process is fully terminated
    timeout /t 1 /nobreak >nul
)

g++ -o build\CEngine.exe ^
main.cpp ^
settings.cpp ^
settings_ui.cpp ^
logging.cpp ^
log_console.cpp ^
debug_info.cpp ^
memory_protection.cpp ^
advanced_scanning.cpp ^
include/imgui.cpp ^
include/imgui_demo.cpp ^
include/imgui_draw.cpp ^
include/imgui_tables.cpp ^
include/imgui_widgets.cpp ^
include/imgui_impl_win32.cpp ^
include/imgui_impl_dx11.cpp ^
-I. ^
-municode ^
-ld3d11 ^
-ldxgi ^
-luser32 ^
-lgdi32 ^
-lshell32 ^
-ldwmapi ^
-ld3dcompiler ^
-lole32 ^
-lpsapi ^
-D_UNICODE ^
-DUNICODE ^
-DWIN32_LEAN_AND_MEAN ^
-DWINVER=0x0601 ^
-D_WIN32_WINNT=0x0601 ^
-mwindows ^
-O2 ^
-std=c++11
```

### Build Requirements

- G++ compiler (MinGW-w64 recommended)
- Windows SDK
- DirectX 11 SDK

## 🚀 Getting Started

1. Launch `CEngine.exe`
2. Click **"List Processes"** to see all running applications
3. Select your target process and click **"Attach to Process"**
4. Choose a value type (int, float, etc.) from the dropdown
5. Enter the value you want to locate and click **"Scan for Value"**
6. Wait for the scan to complete - results will appear in the table
7. Use **"Narrow Results"** with new values to filter down to the exact memory location
8. Double-click any result to modify its value


## 📚 Advanced Usage

- **Memory Map**: View detailed information about memory regions including:
  - Base address and size
  - Protection flags (read/write/execute)
  - Memory state (commit/reserve/free)
  - Module association
  - Content preview

- **Scan Techniques**:
  - **Exact Value**: Find precise matches
  - **Value Range**: Search within minimum/maximum
  - **Unknown Initial**: Find values changed since first scan
  - **Changed Value**: Detect values that have changed
  - **Unchanged Value**: Identify stable memory regions
  - **Bit Pattern**: Search using hexadecimal or binary patterns
  - **Pointer Chains**: Find multi-level pointer paths (coming soon)

- **Additional Tools**:
  - Memory Map: See all allocated memory regions
  - Process Details: View information about the attached process
  - Log Console: Monitor detailed program activity
  - Settings Dialog: Configure scan behavior and UI preferences
  - Value Freezing: Lock memory values to prevent changes (coming soon)

## 🔮 Future Developments

CEngine is under active development with the following features planned for upcoming releases:

### Coming in v1.1
- **Value Freezing**: Lock memory values to prevent them from changing
- **Memory Bookmarking**: Save important addresses for future sessions
- **Pointer Scanner**: Automatically discover pointer chains to stable memory locations
- **Scripting Engine**: Basic Python integration for automated memory operations

### Coming in v1.2
- **Advanced Pointers**: Multi-level pointer scanning and manipulation
- **Disassembler View**: Examine assembly code around memory regions
- **Structure Builder**: Define and manipulate complex data structures in memory
- **Signature Scanning**: Find code patterns in memory using IDA-style signatures

### Coming in v2.0
- **Plugin System**: Extend CEngine with custom C++ or Python plugins
- **Real-Time Charts**: Visualize memory value changes over time
- **Memory Snapshots**: Save and compare full memory states
- **Remote Process Support**: Connect to processes on networked machines
- **Cross-Platform Support**: Initial Linux compatibility

## ⚠️ Known Issues and Workarounds

- **Memory Protected Area**: When scanned for Value it can result in errors such as PARTIAL READ. This means CEngine is not capable of reading the value of the protected memory address and therefore cannot write into the protected memory address.

- **Memory Access Errors**: When scanning certain protected processes, you may see error messages about inaccessible memory regions. This is normal and expected behavior due to Windows security mechanisms - CEngine will continue scanning accessible regions.

- **Performance With Large Results**: When dealing with thousands of scan results, the interface may become sluggish. Try using the "Narrow Results" function more aggressively to reduce the result set.

- **Admin Privileges**: Some processes require elevated privileges to access. Try running CEngine as administrator if you're having trouble attaching to certain applications.

- **Current Value**: When Current Value is change is may not mark it as red.

- **Auto-Detect**: Auto-Detect may be inaccurate.

## 💻 System Requirements

- Windows 10 or 11 (64-bit recommended)
- 4GB RAM minimum (8GB+ recommended for large scans)
- DirectX 11 compatible graphics
- Administrator privileges for best results 

## 📜 License

CEngine is licensed under the GNU General Public License v3.0, ensuring it remains free and open source. This software is provided for **EDUCATIONAL PURPOSES ONLY**. Users are responsible for ensuring they comply with all relevant laws and terms of service. See the [LICENSE](LICENSE) file for complete terms.

## 🙏 Acknowledgements

- [ImGui](https://github.com/ocornut/imgui): The incredible immediate mode GUI library that powers our interface
- [DirectX 11](https://docs.microsoft.com/en-us/windows/win32/direct3d11/d3d11-graphics): Microsoft's graphics API for hardware-accelerated rendering
- [Windows API](https://docs.microsoft.com/en-us/windows/win32/api/): The foundation for process and memory interaction

## 🤝 Contributing

Contributions are welcome! Feel free to submit pull requests or open issues for bugs and feature requests.

## 📬 Contact

For questions, support or feedback, please open an issue on the [GitHub repository](https://github.com/Azreyo/CEngine).

---

*CEngine © 2025 Azreyo - Made for the modding and game development community*
