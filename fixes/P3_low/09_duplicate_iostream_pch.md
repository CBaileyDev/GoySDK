# P3 / 09 — Duplicate `#include <iostream>` in pch.hpp

## Where
- File: `/Users/carterbarker/Documents/GoySDK/internal_bot/pch.hpp`
- Lines: **9** and **12**

```cpp
#include <iostream>
#include <iomanip>
#include <functional>
#include <iostream>      // <-- duplicate
```

## Problem
Header guards make this benign at runtime, but it indicates the include list was edited carelessly. Cleaning it up makes the file readable.

## Fix

Edit `/Users/carterbarker/Documents/GoySDK/internal_bot/pch.hpp`. Find:

```cpp
#include <iostream>
#include <iomanip>
#include <functional>
#include <iostream>
#include <sstream>
```

Replace with:

```cpp
#include <iostream>
#include <iomanip>
#include <functional>
#include <sstream>
```

(Removes the duplicate `#include <iostream>` line.)

While you're there, optionally sort the includes into groups (Win32, STL containers, STL streams, STL utilities, project headers) for readability. Not required.

## Verification
- Build. No new warnings.
- `grep -c '#include <iostream>' /Users/carterbarker/Documents/GoySDK/internal_bot/pch.hpp` returns `1`.

## Don't do
Nothing applicable.
