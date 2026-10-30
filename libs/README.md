# Legacy prebuilt libraries

This tree vendors old toolchain link artifacts (`GP2XSDL`, `WSDL`, `Wsdl-mingw`, Windows `SDL2` `.lib`/`.dll`) for historical platform targets.

Current desktop and Brick builds (`PLATFORM=MACOS`, Linux/`DEB`, `TG5040`, etc.) link against **system / distro SDL2** and do not require these binaries.

Do not add new platforms here unless they truly cannot use a packaged SDL.
