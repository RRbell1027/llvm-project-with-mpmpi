# README.research.md

## 基於 OpenMPI 之 OpenMP Offloading AArch64 目標（研究紀錄用）

分支名稱：`research-mpmpi-aarch64-offload`

本分支為本人碩士論文之研究實作紀錄，主要目的為**保存研究成果與設計脈絡**，方便未來於工作或研究中回顧。

論文層級的細節與章節對照已整理於 `docs/thesis-code-map.md`，本 README 僅保留高階目的、架構與使用方式。

---

## 1. 專案目的與摘要

本研究在 LLVM OpenMP Offloading 架構中，新增一個**基於 OpenMPI 的自訂 Offloading Target**，使主機端（x86_64）能透過標準 `#pragma omp target` 語法，將平行化運算工作委託至 **AArch64 架構之邊緣裝置（Edge Device / CSD）** 執行。

研究核心動機為降低資料密集型應用中的 **資料搬移成本**。在傳統 Host–Accelerator 模型中，大量資料需於主機與加速裝置間頻繁傳輸，容易成為效能瓶頸。本研究採用 **Near-Data Processing** 的概念，將計算推進至接近資料所在位置的邊緣裝置端，以驗證其在 OpenMP Offloading 情境下的可行性。

本 Offloading Target 的主要特性如下：

* Host 與 Device 之間**不共享記憶體**
* 支援 **跨架構（x86_64 → AArch64）** 的 OpenMP Offloading
* 以 **OpenMPI** 作為 Runtime 通訊與行程管理機制
* **不修改 OpenMP 程式語意**，維持標準 OpenMP 使用方式

本分支為研究用途的實驗性實作，未以通用性或 upstream LLVM 整合為目標。

---

## 2. 系統架構概覽

整體系統架構如下：

```
+---------------------+        OpenMPI        +--------------------------+
| Host (x86_64)       |  <---------------->  | Device (AArch64 / CSD)   |
|                     |                      |                          |
| LLVM / Clang        |                      | OpenMP + MPI Worker      |
| libomptarget        |                      | Embedded Linux           |
+---------------------+                      +--------------------------+
```

系統主要由三個部分構成：

1. **主機端（Host）**

   * 使用 LLVM / Clang 編譯 OpenMP 程式
   * 執行階段載入自訂的 libomptarget plugin

2. **裝置端（Device / CSD）**

   * 以 MPI worker 行程形式存在
   * 負責執行 offloaded OpenMP kernel

3. **通訊層**

   * 採用 OpenMPI
   * 負責 kernel dispatch、資料傳輸與同步

---

## 3. LLVM / Clang 修改內容概覽

### 3.1 自訂 Clang ToolChain：SylphToolChain

本分支新增一套自訂 Clang ToolChain（`SylphToolChain`），用以支援 AArch64 裝置端的 OpenMP offloading 編譯流程。

設計重點：

* 使用 **Buildroot SDK 提供的 GNU cross-toolchain**
* 不影響 Clang 前端語意與一般編譯行為
* 僅在 **offloading target 編譯階段** 介入

主要實作行為：

* 自動偵測 target sysroot 與 GNU toolchain
* 使用 target 專屬 assembler 與 linker
* 避免 target linker 誤用 host 的 dynamic linker 設定

---

### 3.2 裝置端 Binary 分離設計

與 LLVM 預設將 device binary 嵌入 host binary 的行為不同，本研究採用以下設計：

* 裝置端獨立儲存 offloaded binary
* Host binary 僅保留必要的描述資訊（descriptor）

此設計有助於：

* 降低 host binary 大小
* 減少執行期 offloading 的資料傳輸量
* 讓裝置端能獨立管理與更新 offloaded binary

---

### 3.3 OpenMP Runtime 擴充：`libomptarget.rtl.sylph`

本分支新增一個 OpenMP runtime plugin：

```
libomptarget.rtl.sylph
```

其主要職責包含：

* 程式啟動時初始化 OpenMPI
* 啟動並管理裝置端 MPI worker
* 負責 device 初始化、binary 傳輸、記憶體管理與 kernel 啟動

為降低 OpenMP runtime 與 MPI 的耦合度，實作中額外設計一層抽象介面：

* `sylph.h`：定義裝置、binary 與 runtime 相關介面
* `sylph.cpp`：封裝 OpenMPI 通訊與控制流程

---

## 4. 建置與執行前需求（Prerequisites）

本專案假設 Host 與 Device 端皆已具備基本系統環境，以下僅列出與本研究實作直接相關的必要套件。

### 4.1 Host 端需求（x86_64）

* LLVM / Clang（本 fork 對應版本）
* OpenMPI（建議與 Device 端版本一致）
* AArch64 GNU cross-toolchain

常見套件名稱範例（依發行版而異）：

* `openmpi`
* `openmpi-devel` 或 `libopenmpi-dev`
* `gcc-aarch64-linux-gnu`
* `g++-aarch64-linux-gnu`

### 4.2 Device 端需求（AArch64）

* Linux（Buildroot 或等效嵌入式系統）
* OpenMPI（支援 TCP 傳輸）
* GCC / libgomp（由 Buildroot SDK 提供）

---

## 5. 使用方式（高階流程說明）

> 本節僅記錄整體流程概念，實際實驗設定細節請搭配 `docs/thesis-code-map.md` 與論文內容回顧。

### 編譯（Host 端）

```bash
clang++ -I/home/leonel_chen/Documents/docker/sylph-dev/env/include \
        -fopenmp \
        -fopenmp-targets=aarch64-nuvoton-linux \
        -o test.out \
        test.cpp trace.o \
        -lopencv_highgui -lopencv_imgproc -lopencv_core -lnpu
```

### 執行（Host 端）
```bash
scp a.so 10.0.0.4:/workspace/a.so
mpirun --hostfile ./hostfile \
        --mca pml ob1 \
        --mca btl tcp,self \
        --mca opal_heterogeneous 1 \
        --mca btl_tcp_if_exclude docker0,lo,npu1 \
        -np 1 ./test.out
```

* Host 端：LLVM Clang + libomp
* Device 端：AArch64 GNU toolchain + libgomp

### 執行流程概述

1. 裝置端系統啟動並等待 MPI worker
2. Host 端執行 OpenMP 應用程式
3. `libomptarget.rtl.sylph` 初始化 OpenMPI
4. OpenMP target region 透過 MPI dispatch 至裝置端
5. 裝置端執行 kernel 並回傳結果

---

## 6. 專案定位說明

* 本分支僅作為碩士論文之研究實作保存
* 程式碼設計以研究驗證與可讀性為優先
* 未針對通用使用情境或長期維護進行最佳化

---

## 7. 快速回顧建議（給未來的自己）

1. 先閱讀本 README（理解整體目的與架構）
2. 再閱讀 `docs/thesis-code-map.md`（定位實作位置）
3. 視需求回顧論文對應章節

---

## 8. 關鍵字

LLVM、OpenMP Offloading、OpenMPI、AArch64、邊緣運算、Near-Data Processing

---

本文件由 chatgpt 生成 \
定位為備忘與導航用途，可隨研究需求持續補充。
