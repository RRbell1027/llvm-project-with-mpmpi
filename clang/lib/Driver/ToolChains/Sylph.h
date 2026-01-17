#ifndef LLVM_CLANG_LIB_DRIVER_TOOLCHAINS_SYLPH_H
#define LLVM_CLANG_LIB_DRIVER_TOOLCHAINS_SYLPH_H

#include "clang/Driver/Driver.h"
#include "clang/Driver/Tool.h"
#include "clang/Driver/ToolChain.h"
#include "llvm/Support/Compiler.h"

namespace clang {
namespace driver {

namespace tools {
namespace sylph {

class LLVM_LIBRARY_VISIBILITY Assembler final : public Tool {
public:
  Assembler(const ToolChain &TC) : Tool("sylph::Assembler", "assembler", TC) {
    printf("Using Sylph Assembler\n");
  }
  bool hasIntegratedAssembler() const override { return false; }
  bool hasIntegratedCPP() const override { return true; }
  void ConstructJob(Compilation &C, const JobAction &JA,
                    const InputInfo &Output, const InputInfoList &Inputs,
                    const llvm::opt::ArgList &TCArgs,
                    const char *LinkingOutput) const override;
};

class LLVM_LIBRARY_VISIBILITY Linker final : public Tool {
public:
    Linker(const ToolChain &TC) : Tool("sylph::Linker", "linker", TC) {
        printf("Using Sylph Linker\n");
    }
    bool hasIntegratedAssembler() const override { return true; }
    bool hasIntegratedCPP() const override { return false; }
    bool isLinkJob() const override { return true; }
    void ConstructJob(Compilation &C, const JobAction &JA,
                    const InputInfo &Output, const InputInfoList &Inputs,
                    const llvm::opt::ArgList &TCArgs,
                    const char *LinkingOutput) const override;
};

} // namespace sylph
} // namespace tools

namespace toolchains {

class LLVM_LIBRARY_VISIBILITY SYLPHToolChain : public ToolChain {
public:

    class GCCInstallationDetector {
        llvm::Triple GCCTriple;
        const Driver &D;

        std::string GCCInstallPath;
        std::string GCCParentLibPath;
        SmallVector<std::string, 4> GCCDefaultLibDirs;
        std::string SysRoot;

    public:
        explicit GCCInstallationDetector(const Driver &D) : D(D) {}
        void init();
        const llvm::Triple &getTriple() const { return GCCTriple; }
        StringRef getInstallPath() const { return GCCInstallPath; }
        StringRef getParentLibPath() const { return GCCParentLibPath; }
        std::string getSysRoot() const { return SysRoot; }
    };

protected:
    GCCInstallationDetector GCCInstallation;

public:
    SYLPHToolChain(const Driver &D, const llvm::Triple &Triple,
              const llvm::opt::ArgList &Args);

    // ~SYLPHToolChain();

    bool IsMathErrnoDefault() const override { return false; }
    bool isCrossCompiling() const override { return true; }
    bool isPICDefault() const override { return false; }
    bool isPIEDefault( const llvm::opt::ArgList &Args) const override { return true; }
    bool isPICDefaultForced() const override { return false; }
    bool IsIntegratedAssemblerDefault() const override { return false; }

    void AddClangCXXStdlibIncludeArgs(const llvm::opt::ArgList &DriverArgs,
                                      llvm::opt::ArgStringList &C11Args) const override;
    std::string computeSysRoot() const override { return GCCInstallation.getSysRoot(); }

protected:
    Tool *buildAssembler() const override;
    Tool *buildLinker() const override;

};

} // namespace toolchains

} // namespace driver
} // namespace clang

#endif