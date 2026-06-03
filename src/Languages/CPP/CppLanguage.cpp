/**
 * @file CppLanguage.cpp
 * @brief C++ language plugin implementation.
 */

#include <Execution/Process.hpp>
#include <Languages/CPP/CppLanguage.hpp>

namespace Sew {
namespace Languages {

using namespace Execution;

Array<ImportSpec> CppLanguage::parseImports(const String &source,
                                            const String &filePath) {
  PreprocessorResult ppResult = _preprocessor.process(source, filePath);
  Array<ImportSpec> imports;

  // Local includes
  for (usz i = 0; i < ppResult.localIncludes.size(); ++i) {
    ImportSpec spec;
    spec.specifier = ppResult.localIncludes[i];
    spec.fromFile = filePath;
    spec.line = 0;
    spec.isSystem = false;
    imports.push(Xi::Move(spec));
  }

  // Sibling source files (auto-discovered .cpp for .h)
  for (usz i = 0; i < ppResult.siblingSourceFiles.size(); ++i) {
    ImportSpec spec;
    spec.specifier = ppResult.siblingSourceFiles[i];
    spec.fromFile = filePath;
    spec.line = 0;
    spec.isSystem = false;
    imports.push(Xi::Move(spec));
  }

  // Sibling source files for the header itself (e.g. String.cpp for String.hpp)
  Array<String> selfSiblings = _preprocessor.findSiblingSourceFiles(filePath);
  for (usz i = 0; i < selfSiblings.size(); ++i) {
    ImportSpec spec;
    spec.specifier = selfSiblings[i];
    spec.fromFile = filePath;
    spec.line = 0;
    spec.isSystem = false;
    imports.push(Xi::Move(spec));
  }

  return imports;
}

CompileResult CppLanguage::compile(const CompileRequest &req) {
  // Preprocess to get stripped source
  PreprocessorResult ppResult =
      _preprocessor.process(req.sourceContent, req.sourcePath);

  return invokeClang(req, ppResult.strippedSource);
}

CompileResult CppLanguage::invokeClang(const CompileRequest &req,
                                       const String &strippedSource) {
  CompileResult result;

  Process p;
  p.file = "clang++";

  // Base flags
  p.arg.push("-c");
  p.arg.push("-std=c++17");

  // Target triple
  if (req.targetTriple.length() > 0) {
    String targetFlag = "--target=";
    targetFlag += req.targetTriple;
    p.arg.push(targetFlag);
  }

  // Output
  if (req.outputPath.length() > 0) {
    p.arg.push("-o");
    p.arg.push(req.outputPath);
  }

  // Extra flags
  for (usz i = 0; i < req.flags.size(); ++i) {
    p.arg.push(req.flags[i]);
  }

  // Source file
  p.arg.push(req.sourcePath);

  // Execute
  p.wait();

  result.success = (p.exitCode == 0);
  result.outputPath = req.outputPath;

  // Collect stderr
  while (p.stderr.size() > 0) {
    String chunk = p.stderr.shift();
    result.errors += chunk;
  }

  return result;
}

} // namespace Languages
} // namespace Sew
