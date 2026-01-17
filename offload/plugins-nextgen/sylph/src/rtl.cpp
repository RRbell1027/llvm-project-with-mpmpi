#include <string>

#include "omptarget.h"
#include "PluginInterface.h"

#include "llvm/Support/Error.h"
#include "Shared/Debug.h"

#include "sylph.h"

namespace llvm {
namespace omp {
namespace target {
namespace plugin {

/// Forward declarations for all specialized data structures.
struct SYLPHKernelTy;
struct SYLPHDeviceTy;
struct SYLPHPluginTy;
struct SYLPHStreamTy;
struct SYLPHEventTy;
struct SYLPHStreamManagerTy;
struct SYLPHEventManagerTy;
struct SYLPHDeviceImageTy;

/// 結合了 cuda 和 host 的演算法
/// OpenMP 跟 Sylph 溝通方法採用 cuda 的
/// Sylph 處理 Process 的方法則採用 host
/// 提供 loadDL, unloadDL, getDL 方法
/// [完璧]
struct SYLPHDeviceImageTy : public DeviceImageTy {

    SYLPHDeviceImageTy(int32_t ImageId, GenericDeviceTy &Device,
                       const __tgt_device_image *TgtImage)
                       : DeviceImageTy(ImageId, Device, TgtImage), DynLib() {}

    /// 在裝置上建立目標行程
    Error loadDynamicLibrary(const char *LibPath);

    /// 在裝置上關閉目標行程 
    Error unloadDynamicLibrary();

    /// 得到目標行程
    sy_dl_ptr getDynamicLibrary() { return DynLib; }

private:
    sy_dl_ptr DynLib;

};

struct SYLPHKernelTy : public GenericKernelTy {

    SYLPHKernelTy(const char *Name) : GenericKernelTy(Name) {}

    Error initImpl(GenericDeviceTy &GenericDevice,
                   DeviceImageTy &Image) override {

        // Functions have zero size.
        GlobalTy Global(getName(), 0);

        // Get the metadata (address) of the kernel function.
        GenericGlobalHandlerTy &GHandler = GenericDevice.Plugin.getGlobalHandler();
        if (auto Err = GHandler.getGlobalMetadataFromDevice(GenericDevice, Image, Global))
            return Err;

        // Check that the function pointer is valid.
        if (!Global.getPtr())
            return Plugin::error("Invalid function for kernel %s", getName());

        // Save the function pointer.
        Func = (void (*)())Global.getPtr();

        MaxNumThreads = 1;
        return Plugin::success();
    }

    Error launchImpl(GenericDeviceTy &GenericDevice,
                     uint32_t NumThreads[3], uint32_t NumBlocks[3],
                     KernelArgsTy &KernelArgs,
                     KernelLaunchParamsTy LaunchParams,
                     AsyncInfoWrapperTy &AsyncInfoWrapper) const override;

private:
    void (*Func)(void);    

};

struct SYLPHDeviceTy : public GenericDeviceTy {

    SYLPHDeviceTy(GenericPluginTy &Plugin, int32_t DeviceId, int32_t NumDevices)
        : GenericDeviceTy(Plugin, DeviceId, NumDevices, {}) {
        }

    ~SYLPHDeviceTy() {}

    Error initImpl(GenericPluginTy &Plugin) override {
        sy_status_t Res = sy_spawn(DevicePtr);
        if (auto Err = Plugin::check(Res, "Error in setDynamicLibrary/sy_spawn: %s")) {
            return Err;
        }
        return Plugin::success();
    }

    Error deinitImpl() override {
        if (!LoadedImages.empty()) {
            // Each image has its own module.
            for (DeviceImageTy *Image : LoadedImages) {
                SYLPHDeviceImageTy &SYLPHImage = static_cast<SYLPHDeviceImageTy &>(*Image);
                // Unload the executable of the image.
                if (auto Err = SYLPHImage.unloadDynamicLibrary()) {
                    return Err;
                }
            }
        }

        sy_status_t Res = sy_quit(DevicePtr);
        if (auto Err = Plugin::check(Res, "Error in setDynamicLibrary/sy_spawn: %s")) {
            return Err;
        }
        return Plugin::success();
    }

    std::string getComputeUnitKind() const override { return "generic-64bit"; }

    Expected<GenericKernelTy &> constructKernel(const char *Name) override {

        // Allocate and construct the kernel.
        SYLPHKernelTy *SYLPHKernel = Plugin.allocate<SYLPHKernelTy>();
        if (!SYLPHKernel)
        return Plugin::error("Failed to allocate memory for GenELF64 kernel");

        new (SYLPHKernel) SYLPHKernelTy(Name); // [debug] dont forget to change

        return *SYLPHKernel;
    }    

    Error setContext() override {
        MESSAGE0("SetContext\n");
        return Plugin::success(); 
    }

    Expected<DeviceImageTy *>
    loadBinaryImpl(const __tgt_device_image *TgtImage, int32_t ImageId) override {



///
        SYLPHDeviceImageTy *Image = new SYLPHDeviceImageTy(ImageId, *this, TgtImage);

        // 讀取 TgtImage 的內容作為 so 路徑
        const char *LibPath = nullptr;
        if (TgtImage && TgtImage->ImageStart && TgtImage->ImageEnd) {
            StringRef Buffer(reinterpret_cast<const char *>(TgtImage->ImageStart),
                    utils::getPtrDiff(TgtImage->ImageEnd, TgtImage->ImageStart));

            // 检查自定义头部
            if (!Buffer.compare(SylphImageHeader)) {
                return Plugin::error("SYLPH device: Invalid image format, missing custom header.");
            }

            // 提取 ELF 文件路径
            size_t SylphImageHeaderSize = strlen(SylphImageHeader.data());
            StringRef ElfFilePath = Buffer.drop_front(SylphImageHeaderSize);
            std::string ElfFilePathStr = ElfFilePath.str();

            // 去除可能的換行符號
            ElfFilePathStr.erase(std::remove(ElfFilePathStr.begin(), ElfFilePathStr.end(), '\n'), 
                                 ElfFilePathStr.end());
            LibPath = strdup(ElfFilePathStr.c_str());
        }
        if (LibPath)
            Image->loadDynamicLibrary(LibPath);

        return Image;
    }

    void *allocate(size_t Size, void *, TargetAllocTy Kind) override {
        if (Size == 0)
            return nullptr;
        sy_ptr_t MemAlloc;
        switch (Kind) {
        case TARGET_ALLOC_DEFAULT:
        case TARGET_ALLOC_DEVICE:
        case TARGET_ALLOC_HOST:
        case TARGET_ALLOC_SHARED:
        case TARGET_ALLOC_DEVICE_NON_BLOCKING:
            sy_malloc(MemAlloc, Size, DevicePtr);
        break;
        }
        return reinterpret_cast<void *>(MemAlloc);
    }

    int free(void *TgtPtr, TargetAllocTy Kind) override {
        switch (Kind) {
        case TARGET_ALLOC_DEFAULT:
        case TARGET_ALLOC_DEVICE:
        case TARGET_ALLOC_HOST:
        case TARGET_ALLOC_SHARED:
        case TARGET_ALLOC_DEVICE_NON_BLOCKING:
            sy_ptr_t dev_ptr = reinterpret_cast<sy_ptr_t>(TgtPtr);
            sy_free(dev_ptr, DevicePtr);
        break;
        }
        return 0;
    }

    Expected<void *> dataLockImpl(void *HstPtr, int64_t Size) override {
        return HstPtr; }

    Error dataUnlockImpl(void *HstPtr) override {
        return Plugin::success(); }

    Expected<bool> isPinnedPtrImpl(void *HstPtr, void *&BaseHstPtr,
                                   void *&BaseDevAccessiblePtr,
                                   size_t &BaseSize) const override {
        return false;
    }

    Error dataSubmitImpl(void *TgtPtr, const void *HstPtr, int64_t Size,
                         AsyncInfoWrapperTy &AsyncInfoWrapper) override {
        sy_ptr_t dev_ptr = reinterpret_cast<sy_ptr_t>(TgtPtr);
        sy_ptr_t host_ptr = reinterpret_cast<sy_ptr_t>(HstPtr);
        size_t size = static_cast<size_t>(Size);

        sy_data_transfer(host_ptr, dev_ptr, size, SYHOST2DEVICE, DevicePtr);
        return Plugin::success();
    }

    Error dataRetrieveImpl(void *HstPtr, const void *TgtPtr, int64_t Size,
                            AsyncInfoWrapperTy &AsyncInfoWrapper) override {
        sy_ptr_t dev_ptr = reinterpret_cast<sy_ptr_t>(TgtPtr);
        sy_ptr_t host_ptr = reinterpret_cast<sy_ptr_t>(HstPtr);
        size_t size = static_cast<size_t>(Size);

        sy_data_transfer(dev_ptr, host_ptr, size, SYDEVICE2HOST, DevicePtr);
        return Plugin::success();
    }

    Error dataExchangeImpl(const void *SrcPtr, GenericDeviceTy &DstDev,
                           void *DstPtr, int64_t Size,
                           AsyncInfoWrapperTy &AsyncInfoWrapper) override {
                            return Plugin::success(); }


    Error synchronizeImpl(__tgt_async_info &AsyncInfo) override {
        return Plugin::success(); }


    Error queryAsyncImpl(__tgt_async_info &AsyncInfo) override {
        return Plugin::success(); }


    Error initAsyncInfoImpl(AsyncInfoWrapperTy &AsyncInfoWrapper) override {
        return Plugin::success(); }


    Error initDeviceInfoImpl(__tgt_device_info *DeviceInfo) override {
        return Plugin::success(); }


    Error createEventImpl(void **EventPtrStorage) override {
        return Plugin::success(); }


    Error destroyEventImpl(void *EventPtr) override {
        return Plugin::success(); }

    Error recordEventImpl(void *EventPtr,
                          AsyncInfoWrapperTy &AsyncInfoWrapper) override {
                            return Plugin::success(); }

    Error waitEventImpl(void *EventPtr,
                        AsyncInfoWrapperTy &AsyncInfoWrapper) override {
                            return Plugin::success(); }

    Error syncEventImpl(void *EventPtr) override {
        return Plugin::success(); }

    Error obtainInfoImpl(InfoQueueTy &Info) override {
        return Plugin::success(); }

    bool shouldSetupDeviceEnvironment() const override { return false; };
    bool shouldSetupDeviceMemoryPool() const override { return false; };

    Error getDeviceStackSize(uint64_t &V) override {
        V = 0;
        return Plugin::success(); 
    }

    Error setDeviceStackSize(uint64_t V) override { return Plugin::success(); }

    Error getDeviceHeapSize(uint64_t &V) override { 
        V = 0;
        return Plugin::success(); 
    }

    Error setDeviceHeapSize(uint64_t V) override { return Plugin::success(); }

    sy_device_ptr getDevicePtr() { return DevicePtr; }

private:
    sy_device_ptr DevicePtr;

    StringRef SylphImageHeader = "#SYLPH_IMAGE_V1#";

    static constexpr GV SYLPHGridValues = {
        1, // GV_Slot_Size
        1, // GV_Warp_Size
        1, // GV_Max_Teams
        1, // GV_Default_Num_Teams
        1, // GV_SimpleBufferSize
        1, // GV_Max_WG_Size
        1, // GV_Default_WG_Size
    };

};

/// 拿全域變數指標的方法
/// devicety.getDevicePtr() 尚未定義
class SYLPHGlobalHandlerTy final : public GenericGlobalHandlerTy {
public:
    /// Set pointer to deviceglobal
    Error getGlobalMetadataFromDevice(GenericDeviceTy &Device,
                                    DeviceImageTy &Image,
                                    GlobalTy &DeviceGlobal) override {

    // 得到目標 global variable 名字
    const char *GlobalName = DeviceGlobal.getName().data();

    // 得到要跑得 device ptr
    SYLPHDeviceTy &SYLPHDevice = static_cast<SYLPHDeviceTy &>(Device);
    sy_device_ptr SYDevice = SYLPHDevice.getDevicePtr();

    // 得到要跑得 dl ptr
    SYLPHDeviceImageTy &SYLPHImage = static_cast<SYLPHDeviceImageTy &>(Image);
    sy_dl_ptr SYDynlib = SYLPHImage.getDynamicLibrary();

    int SYSize;
    sy_ptr_t SYPtr;
    sy_status_t Res = sy_get_symbol_ptr(SYPtr, SYSize, GlobalName, SYDevice, SYDynlib);

    DeviceGlobal.setPtr(reinterpret_cast<void *>(SYPtr));
    return Plugin::success();
    }
};

/// 判斷 openmp scope 是否相容
/// 尋找並初始化裝置
/// 目前因為還未做編譯器，所以無法下手
struct SYLPHPluginTy : public GenericPluginTy  {

    SYLPHPluginTy() : GenericPluginTy(getTripleArch()) {
    }

    /// Initialize devices and return numbers of device.
    Expected<int32_t> initImpl() override {
        sy_status_t Res = sy_init();
        if (auto Err = Plugin::check(Res, "Sylph initialize error: %s\n")) {
            return 0;
        }
        return NUM_SYLPH;
    }

    Error deinitImpl() override {
        sy_status_t Res = sy_finalize();

        if (auto Err = Plugin::check(Res, "Sylph initialize error: %s\n")) {
            return Err;
        }
        return Plugin::success();
    }

    GenericGlobalHandlerTy *createGlobalHandler() override {
        return new SYLPHGlobalHandlerTy();
    }

    GenericDeviceTy *createDevice(GenericPluginTy &Plugin,
                                  int32_t DeviceID,
                                  int32_t NumDevice) override {
        return new SYLPHDeviceTy(Plugin, DeviceID, NumDevice);
    }

    /// Call during detecting whether plugin is compatible with image.
    /// Image header needs to contain elf machine information.
    uint16_t getMagicElfBits() const override { return ELF::EM_AARCH64; }

    Triple::ArchType getTripleArch() const override { return Triple::aarch64; }

    const char *getName() const override { return "SYLPH"; }

    bool isDataExchangable(int32_t SrcDEviceId, int32_t DstDeviceId) override {
        return false;
    }

    /// Call in PluginManager::registerLib
    /// Detect whether device is compatible with image. (not image and plugin)
    Expected<bool> isELFCompatible(uint32_t DeviceID, StringRef Image) const override {
        return true;
    }
};


Error SYLPHDeviceImageTy::loadDynamicLibrary(const char *LibPath)
{
    // Spawn a child process onto target device
    SYLPHDeviceTy &Device = static_cast<SYLPHDeviceTy &>(getDevice());
    sy_device_ptr DevicePtr = Device.getDevicePtr();

    // Load target dynamic library
    sy_status_t Res = sy_open(LibPath, DynLib, DevicePtr);
    if (auto Err = Plugin::check(Res, "Error in setDynamicLibrary/sy_open: %s")) {
        return Err;
    }

    return Plugin::success();
}

Error SYLPHDeviceImageTy::unloadDynamicLibrary() {
    SYLPHDeviceTy &Device = static_cast<SYLPHDeviceTy &>(getDevice());
    sy_device_ptr DevicePtr = Device.getDevicePtr();
    sy_status_t Res = sy_close(DynLib, DevicePtr);
    if (auto Err = Plugin::check(Res, "Error in setDynamicLibrary/sy_open: %s")) {
        return Err;
    }
    return Error::success();
}

template <typename... ArgsTy>
static Error Plugin::check(int32_t Code, const char *ErrFmt, ArgsTy... Args) {
  sy_status_t ResultCode = static_cast<sy_status_t>(Code);
  if (ResultCode == sy_status_t::SYLPH_STATUS_SUCCESS)
    return Error::success();

  // 不再查詢 sy_dlerror，直接回傳錯誤
  return createStringError<ArgsTy...>(inconvertibleErrorCode(), ErrFmt, Args...);
}

Error SYLPHKernelTy::launchImpl(GenericDeviceTy &GenericDevice,
                                uint32_t NumThreads[3], uint32_t NumBlocks[3],
                                KernelArgsTy &KernelArgs,
                                KernelLaunchParamsTy LaunchParams,
                                AsyncInfoWrapperTy &AsyncInfoWrapper) const {

    SYLPHDeviceTy &Device = static_cast<SYLPHDeviceTy &>(GenericDevice);
    sy_device_ptr DevicePtr = Device.getDevicePtr();

    int numArgs = KernelArgs.NumArgs;
    void **argPtrs = (void **)LaunchParams.Ptrs; // Pointer to the array of parameter data pointers
    size_t totalSize = LaunchParams.Size;       // Total size of the packed parameter data

    // Create a temporary array to store the calculated sizes
    std::vector<int64_t> calculatedArgSizes(numArgs);

    // Calculate size for each parameter using pointers and total size
    if (numArgs > 0 && argPtrs != nullptr) {
        for (int i = 0; i < numArgs - 1; ++i) {
            // Size of i-th argument is the difference between the address of (i+1)-th and i-th argument data
            calculatedArgSizes[i] = reinterpret_cast<int64_t>(argPtrs[i+1]) - reinterpret_cast<int64_t>(argPtrs[i]);
        }
        // Size of the last argument is total size minus the sum of sizes of previous arguments
        calculatedArgSizes[numArgs - 1] = totalSize - (reinterpret_cast<int64_t>(argPtrs[numArgs-1]) - reinterpret_cast<int64_t>(argPtrs[0]));

    } else if (numArgs == 1 && argPtrs != nullptr && totalSize > 0) {
        // Special case for a single argument, size is just the total size
         calculatedArgSizes[0] = totalSize;
    }
    // If numArgs is 0 or argPtrs is null/totalSize is 0, calculatedArgSizes will remain empty or all zeros.

    sy_ptr_t FuncAddr = reinterpret_cast<sy_ptr_t>(Func);

    // Pass the calculated sizes to sy_exec
    // We need to pass a pointer to the first element of the vector
    sy_exec(DevicePtr, FuncAddr, numArgs, argPtrs, calculatedArgSizes.data());

    return Plugin::success();
}

} // namespace plugin
} // namespace target
} // namespace omp
} // namespacae llvm


extern "C" {
llvm::omp::target::plugin::GenericPluginTy *createPlugin_sylph() {
    return new llvm::omp::target::plugin::SYLPHPluginTy();
}
}