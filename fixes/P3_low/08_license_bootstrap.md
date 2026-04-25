# P3 / 08 — LICENSE file is the Bootstrap CSS license

## TL;DR
`internal_bot/LICENSE` is the MIT license text from Bootstrap (Mark Otto / Andrew Fong). It's been copied unchanged from a template and not updated. This misrepresents copyright ownership.

## Where
- File: `/Users/carterbarker/Documents/GoySDK/internal_bot/LICENSE`

```
# Released under MIT License

Copyright (c) 2013 Mark Otto.
Copyright (c) 2017 Andrew Fong.
...
```

## Problem
1. Mark Otto and Andrew Fong have nothing to do with this codebase.
2. If you intended MIT, the year and copyright holder are wrong.
3. If you intended a different license (proprietary, AGPL, etc.), the file is misleading.

## Fix

### Step 1 — Decide which license you actually want

Two common choices:
- **MIT** — permissive, allows commercial use; same license but with the right name/year.
- **Proprietary / All Rights Reserved** — keeps options open while you decide.

### Step 2 — Replace the file

Edit `/Users/carterbarker/Documents/GoySDK/internal_bot/LICENSE`.

For MIT (recommended for a research SDK), replace the entire file content with:

```
MIT License

Copyright (c) 2026 <YOUR NAME OR ORGANIZATION>

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
```

Replace `<YOUR NAME OR ORGANIZATION>` with the actual rights holder.

For "All Rights Reserved" / proprietary placeholder, replace with:

```
Copyright (c) 2026 <YOUR NAME OR ORGANIZATION>
All Rights Reserved.

This software is proprietary. No license is granted to use, copy, modify, or
distribute it without explicit written permission from the copyright holder.
```

### Step 3 — Audit other places that might restate the license

```bash
grep -rni "mark otto\|andrew fong\|bootstrap" /Users/carterbarker/Documents/GoySDK/internal_bot/ /Users/carterbarker/Documents/GoySDK/GoyLoader/ /Users/carterbarker/Documents/GoySDK/repos/
```

Update any matches.

### Step 4 — Check upstream template attribution

The README at `internal_bot/README.md:7` says "Based on a public CodeRed-style UE3 mod template." If your chosen license is incompatible with the upstream template's license, you may need to retain an attribution notice. Verify the upstream template's actual license before shipping.

## Verification
- Read the new file end-to-end. The names should be yours, not Mark Otto's.
- If you're publishing on GitHub, the LICENSE auto-detection will now correctly classify the repo.

## Don't do
- Don't keep the Bootstrap text "for now" — it's a legal misrepresentation, however harmless.
- Don't pick a license you don't understand. If unsure between MIT, Apache 2.0, AGPL, or proprietary, ask a lawyer or read <https://choosealicense.com/>.

## Related
None.
