#pragma once
#include <Sew/Parser.hpp>

namespace Sew {

class BindingGenerator {
public:
    // Generate C++ extern "C" bridge code
    static String generateCppBridge(const Array<ParsedClass>& classes,
                                    const Array<ParsedFunction>& functions,
                                    const Array<String>& namespaces,
                                    const Array<String>& headerIncludePaths);

    // Generate TypeScript JS wrapper code
    static String generateTsGlue(const Array<ParsedClass>& classes,
                                 const Array<ParsedFunction>& functions,
                                 const String& wasmFileName);
};

} // namespace Sew
