#include "Mangler.h"

namespace ZCompiler {
    std::string mangleFunction(const std::string& owner, const std::string& name) {
        if (owner.empty())
            return name;

        // Two underscores, matching the scheme the plan reserves for methods
        // (`Class__method`) and constructors, so all three read alike.
        return owner + "__" + name;
    }
}
