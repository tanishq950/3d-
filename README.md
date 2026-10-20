# RoomStudio — 3D Interior Designer

RoomStudio is a native Windows desktop app written in C. Arrange and recolor
furniture in an interactive isometric room, change the room size, and save or
open designs as `.room` files.

## Run

Download `RoomStudio.exe` from this repository and double-click it. No separate
runtime or third-party graphics library is required.

## Build from source

Install Visual Studio 2022 Build Tools with the **Desktop development with C++**
workload and a Windows SDK. Then double-click `build.bat` or run it in a
Developer Command Prompt. The script builds `RoomStudio.exe` in the project
root.

## Controls

- Use the furniture buttons to add sofas, chairs, tables, lamps, plants,
  bookshelves, and rugs.
- Click furniture in the room to select it; drag to move it.
- Choose a material color for new furniture or the selected piece.
- Resize the room with the room-size buttons.
- Remove selected furniture with the button or the Delete key. Use the arrow
  keys for precise placement.
- Use **Save design** and **Open saved room** to save and restore `.room` files.
- **New room** restores the furnished starter layout.
