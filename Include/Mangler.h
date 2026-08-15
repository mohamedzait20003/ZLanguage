#pragma once

#include <string>

namespace ZCompiler {
    // Maps Z-level names to the symbol names actually emitted.
    //
    // A namespace-scoped function becomes `NS__name`, so `mymath.square` and
    // `mygeom.square` can coexist in one module. File-scope functions keep their
    // plain name so that `main` stays `main` and C runtime entry points remain
    // callable by their real names.
    //
    // Parameter type codes join the scheme when overloading lands in M16; until
    // then a name is unique within its scope, so encoding them would be noise.
    // Both the definition and every call site must go through here — that is the
    // point of having one function rather than string concatenation at each site.
    std::string mangleFunction(const std::string& owner, const std::string& name);
}
