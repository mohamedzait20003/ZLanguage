#include "Mangler.h"

namespace ZCompiler {
    std::string mangleFunction(const std::string& owner, const std::string& name) {
        if (owner.empty())
            return name;

        std::string mangled;
        mangled.reserve(owner.size() + name.size() + 4);

        for (char c : owner)
            if (c == '.') mangled += "__"; else mangled += c;

        return mangled + "__" + name;
    }
}
