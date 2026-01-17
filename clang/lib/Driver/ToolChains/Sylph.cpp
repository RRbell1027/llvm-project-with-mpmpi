#include "Sylph.h"
#include "clang/Driver/Compilation.h"
#include "llvm/Option/ArgList.h"
#include "clang/Driver/Driver.h"
#include "llvm/Support/Path.h"
#include "llvm/Option/Option.h"
#include "CommonArgs.h"

using namespace clang::driver;
using namespace clang::driver::toolchains;
using namespace clang::driver::tools;
using namespace llvm::opt;

static bool forwardToGCC(const Option &O) {
  // LinkerInput options have been forwarded. Don't duplicate.
  if (O.hasFlag(options::LinkerInput))
    return false;
  return O.matches(options::OPT_Link_Group) || O.hasFlag(options::LinkOption);
}

void sylph::Assembler::ConstructJob(Compilation &C, const JobAction &JA,
                                    const InputInfo &Output,
                                    const InputInfoList &Inputs,
                                    const llvm::opt::ArgList &Args,
                                    const char *AssembleOutput) const {
    claimNoWarnArgs(Args);
    if (Inputs.size() != 1)
        llvm_unreachable("Invalid number of input files.");

    llvm::opt::ArgStringList CmdArgs({});
    CmdArgs.push_back(Inputs[0].getFilename());

    CmdArgs.append({"-o", Output.getFilename()});

    std::string SysRoot = getToolChain().computeSysRoot();
    const char *Exec = Args.MakeArgString(SysRoot + "/../bin/as");
    C.addCommand(std::make_unique<Command>(JA, *this, ResponseFileSupport::None(),
                                         Exec, CmdArgs, Inputs[0], Output));
}

void sylph::Linker::ConstructJob(Compilation &C, const JobAction &JA,
                                 const InputInfo &Output,
                                 const InputInfoList &Inputs,
                                 const ArgList &Args,
                                 const char *LinkingOutput) const {
    claimNoWarnArgs(Args);
    ArgStringList OrigCmdArgs;
    std::string SysRoot = getToolChain().computeSysRoot();

    // 将特定于链接器的选项通过 -Wl, 传递给 g++
    OrigCmdArgs.push_back("-Wl,--no-undefined");
    OrigCmdArgs.push_back("-Wl,-Bsymbolic");
    OrigCmdArgs.push_back("-Wl,-rpath,/usr/lib");
    OrigCmdArgs.push_back(Args.MakeArgString(
        "-Wl,-dynamic-linker,/lib64/ld-linux-aarch64.so.1"));

    OrigCmdArgs.push_back("-shared");
    OrigCmdArgs.push_back("-o");
    
    // 链接器输出到固定的中间文件 "/workspace/a.so"
    const char* RealOutputElf = "/workspace/a.so";
    OrigCmdArgs.push_back("a.so");


    // 只加入 isFilename 的 Input
    for (const auto &Input : Inputs) {
        if (Input.isFilename()) {
            std::string fname = Input.getFilename();
            if (fname == "/lib64/ld-linux-x86-64.so.2") {
                continue;
            }
            OrigCmdArgs.push_back(Args.MakeArgString(fname));
        }
    }

    // 添加应用程序特定的库. g++ 会自动处理标准库 (stdc++, gcc, c, m, etc.)
    OrigCmdArgs.push_back("-lnpu");
    OrigCmdArgs.push_back("-lopencv_highgui");
    OrigCmdArgs.push_back("-lopencv_imgproc");
    OrigCmdArgs.push_back("-lopencv_core");
    OrigCmdArgs.push_back("-lopencv_plugin");

    // 使用链接器组来处理静态库之间的复杂依赖关系

    // g++ 驱动程序会自动链接 libstdc++ 和 libm，但我们是手动构建参数，
    // 因此必须手动添加。这对于链接 C++ 代码（如 OpenCV）至关重要。
    OrigCmdArgs.push_back("-lstdc++");
    OrigCmdArgs.push_back("-lm");

    OrigCmdArgs.push_back("-lpthread");
    OrigCmdArgs.push_back("-lgomp");

    // OrigCmdArgs.push_back("-lopencv");

    // 使用 g++ 作为链接器驱动程序
    const char *GXX = Args.MakeArgString(SysRoot + "/../../bin/aarch64-nuvoton-linux-gnu-g++");
    
    // 生成链接命令，输出到中间 ELF 文件
    InputInfo RealOutputInfo(types::TY_Image, RealOutputElf, nullptr);
    C.addCommand(std::make_unique<Command>(JA, *this, ResponseFileSupport::None(),
                                         GXX, OrigCmdArgs,
                                         Inputs, RealOutputInfo));

    // *** 修改点：使用 echo 命令，输出自定义头部和 ELF 文件路径到最终输出文件 ***
    // 定义自定义头部
    const char* SylphImageHeader = "#SYLPH_IMAGE_V1#";
    // 产生 output image，内容为 "自定义头部/workspace/a.so" 字串
    ArgStringList OutputCmdArgs;
    OutputCmdArgs.push_back("-c");
    // 将自定义头部和中间 ELF 文件的路径拼接后作为字符串输出
    std::string echoCmd = std::string("echo -n \"") + SylphImageHeader + RealOutputElf + std::string("\" > ") + Output.getFilename();
    OutputCmdArgs.push_back(Args.MakeArgString(echoCmd));
    const char *Shell = "/bin/sh";
    C.addCommand(std::make_unique<Command>(JA, *this, ResponseFileSupport::None(),
                                         Shell, OutputCmdArgs,
                                         Inputs, Output)); // echo 命令的输出是最终 Output
}

void SYLPHToolChain::GCCInstallationDetector::init() {
    GCCTriple.setTriple("aarch64-nuvoton-linux");
    StringRef BaseEnvPath = llvm::sys::path::parent_path(D.Dir);
    GCCInstallPath = std::string(BaseEnvPath) + "/opt/aarch64-nuvoton-linux-gnu/libexec/gcc/aarch64-nuvoton-linux-gnu/12.3.0";
    GCCParentLibPath = GCCInstallPath + "/../../..";
    SysRoot = GCCParentLibPath + "/../aarch64-nuvoton-linux-gnu/sysroot";
    GCCDefaultLibDirs.push_back("/lib");
}

SYLPHToolChain::SYLPHToolChain(const Driver &D, const llvm::Triple &Triple,
    const llvm::opt::ArgList &Args): ToolChain(D, Triple, Args), GCCInstallation(D) {
    printf("Using SYlphToolChain\n");
    GCCInstallation.init();
}

void SYLPHToolChain::AddClangCXXStdlibIncludeArgs(const llvm::opt::ArgList &DriverArgs,
    llvm::opt::ArgStringList &C11Args) const {
    printf("AddClangCXXStdlibIncludeArgs\n");

}

Tool *SYLPHToolChain::buildAssembler() const {
  return new sylph::Assembler(*this);
}

Tool *SYLPHToolChain::buildLinker() const {
    return new sylph::Linker(*this);
}


