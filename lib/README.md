# Project-private libraries

Put reusable modules here, one folder each, and PlatformIO will build and link
them automatically:

```
lib/
  TrapSensor/
    TrapSensor.h
    TrapSensor.cpp
  TrapProtocol/
    ...
```

Anything under `lib/` gets its own include path, so `#include <TrapSensor.h>`
works from `src/`. Third-party libraries belong in `lib_deps` in
`platformio.ini` instead — keep this folder for code that only ever ships with
this project.

See https://docs.platformio.org/page/librarymanager/ldf.html
