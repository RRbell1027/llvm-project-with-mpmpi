# 論文章節 ↔ 原始碼對照表（Thesis–Code Map）

本文件用於對照碩士論文章節與 `research-mpmpi-aarch64-offload` 分支中對應的原始碼位置，目的是讓未來回頭閱讀時，可以快速理解「某一章論文實際對應到哪一段程式碼」。

---

## 第 1 章：緒論

**內容重點：**

* 研究動機
* Memory Wall 問題
* Near-Data Processing 與 CSD 概念

**程式碼對應：**

* 無直接對應原始碼
* 作為整體系統架構與設計選擇的理論基礎

---

## 第 2 章：相關研究

**內容重點：**

* OpenMP Offloading 架構
* MPI-based 系統
* 邊緣運算與 CSD 相關研究

**程式碼對應：**

* 無直接對應原始碼
* 用於說明第 3 章設計取捨之背景

---

## 第 3 章：系統設計與實作

### §3.1 系統整體架構

**內容說明：**

* Host / Device 分離
* MPI-based Offloading 模型

**對應程式碼：**

* 無直接對應原始碼
* 為第 3 章之摘要

---

### §3.2 編譯流程與 ToolChain 設計

**內容說明：**

* 自訂 Clang ToolChain
* Offloading 編譯流程
* 裝置端 binary 分離設計

**對應程式碼：**

* `offload/plugins-nextgen/common/src/PluginInterface.cpp`
* `clang/lib/Driver/ToolChains/Sylph.*`
* `clang/lib/Driver/Driver.cpp`
* `clang/lib/Driver/CMakeLists.txt`
* `llvm/include/llvm/TargetParser/Triple.h`
* `llvm/lib/TargetParser/Triple.cpp`


**實作重點：**

* 使用外部 GNU cross-toolchain
* 正確解析 AArch64 linker 與 sysroot
* 避免 target linker 繼承 host dynamic linker

---

### §3.3 OpenMP Runtime 擴充

**內容說明：**

* MPI-based OpenMP runtime
* 裝置管理與 kernel dispatch

**對應程式碼：**

* `offload/plugins-nextgen/sylph`
* `offload/CMakeLists.txt`
* `offload/plugins-nextgen/common/src/PluginInterface.cpp`

**實作重點：**

* MPI 初始化與終止
* Device discovery
* Kernel 啟動與同步
* Device memory allocation 與 mapping

---

### §3.4 通訊模型

**內容說明：**

* MPI 行程管理
* 資料傳輸策略

**對應程式碼：**

* `offload/plugins-nextgen/sylph/dynamic_sylph/sylph.*`
* `offload/plugins-nextgen/sylph/dynamic_sylph/worker.h`
* `thesis/device/worker.cpp`

**補充說明：**

* 明確限制 MPI 使用之網路介面
* 實驗中以 TCP 作為傳輸層

---

## 第 4 章：實驗與效能分析

### §4.1 實驗環境

**內容說明：**

* 硬體與軟體配置

### §4.4 實驗環境

**內容說明：**
* npu 的實際運用

**對應程式碼：**
* `llvm-project/thesis/device/client.cpp`
* `llvm-project/thesis/npu`

---

## 第 5 章：結論與未來工作

**內容重點：**

* 系統限制
* 未來可延伸方向

**可對應之程式碼延伸點：**

* 替換 MPI 為其他通訊機制（如 RDMA）
* 改善裝置端記憶體一致性支援
* 將 SylphToolChain 一般化以利未來整合

---

## 建議閱讀順序（給未來的自己）

1. 本文件（快速定位）
2. 論文第 3 章
3. `offload/plugins-nextgen/sylph/`
4. `clang/lib/Driver/ToolChains/Sylph.*`

---

本文件由 chatgpt 生成 \
定位為備忘與導航用途，可隨研究需求持續補充。
