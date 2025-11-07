# Aturan & Konvensi

## 1. Strategi Branching

Kita menggunakan alur kerja seperti biasanya yaitu menggunakan Pull Request (PR).

* **Branch `main`:**
    * Branch ini adalah "versi utama" dari proyek kita.
    * Kode di `main` **HARUS SELALU** dalam keadaan *stable*, bisa di-*build* (`cmake .. && make`), dan lulus semua *unit test* (jika ada).
    * **ATURAN KERAS:** **DILARANG** melakukan `push` langsung ke `main`. Branch ini tidak benar-benar dilindungi jadi mohon berhati-hati.

* **Feature Branches (Branch Kerja):**
    * Semua pekerjaan, baik itu fitur baru, *bugfix*, atau bahkan eksperimen, **WAJIB** dilakukan di *branch* terpisah.
    * Gunakan konvensi nama branch berikut untuk menjaga kerapian:
        * Format: `commit-type/grup/deskripsi-tugas`
        * **Contoh:**
            * `feat/qp/implementasi-select` (Fitur baru Query Processor)
            * `fix/sm/indeks-bplus-tree` (Bug fix untuk Storage Manager)
            * `chore/ccm/protokol-lock-based` (Comments untuk Concurrency Control)
            * `refactor/qo/bug-optimasi-join` (Refactor untuk Query Optimizer)

* **Pull Requests (PR):**
    * Ketika pekerjaan di *feature branch* selesai, buka *Pull Request* (PR) untuk menggabungkannya ke `main`.
    * Jangan *merge* PR Anda sendiri..

## 2. Aturan Emas

### Aturan #1: Direktori `include/` adalah KONTRAK API
Direktori `/include` berisi *public headers* (`.h`) yang mendefinisikan *interface* (API) antar komponen.

* **Anggap ini sebagai kontrak yang mengikat.**
* **DILARANG** mengubah file *header* milik grup lain tanpa diskusi dan persetujuan eksplisit. Grup *Query Processor*, sebagai integrator utama, harus menyetujui semua perubahan API.
* File `types.h` digunakan untuk *structs* umum yang dipakai bersama (misal: `ExecutionResult`, `Rows`, dll).

### Aturan #2: `namespace` adalah WAJIB
Untuk mencegah konflik nama di C++, setiap grup **WAJIB** membungkus semua kode mereka (baik di `.h` maupun `.cpp`) di dalam *namespace* unik.

* **Format:** `mdbms::[prefix_grup]`
* **Contoh:**
    * `mdbms::qp` (Query Processor)
    * `mdbms::qo` (Query Optimizer)
    * `mdbms::sm` (Storage Manager)
    * `mdbms::ccm` (Concurrency Control Manager)
    * `mdbms::fr` (Failure Recovery)

```cpp
// Contoh di include/storage_manager.h

#pragma once
#include "types.h" // Asumsi types.h berisi definisi Rows

namespace mdbms::sm {

class StorageEngine {
public:
    // Gunakan tipe data dari API kontrak
    mdbms::ExecutionResult read_block(...);
    int write_block(...);
};

} // namespace mdbms::sm
```

### Aturan #3: Gunakan Code Formatter (`.clang-format`)
Kita tidak akan berdebat tentang gaya koding (spasi vs tab, letak kurung kurawal, dll).

* **Tool:** Kita menggunakan `clang-format`.
* **Aturan:** File `.clang-format` di *root* repositori adalah satu-satunya standar.
* **Kewajiban:** Setiap *commit* **WAJIB** sudah diformat menggunakan `clang-format`.
* *Sangat disarankan* untuk melakukan konfigurasi agar berjalan otomatis di IDE Anda.

### Aturan #4: Proses Pull Request (PR) & Code Review

1.  Buka PR dari *feature branch* Anda ke `main`.
2.  PR **WAJIB** di-review dan di-*approve* oleh **minimal 2 orang**:
    * **1 orang dari grup Anda sendiri** (idealnya ketua grup).
    * **1 orang dari grup LAIN** (idealnya *integration lead* atau grup yang bergantung pada kode Anda).
3.  **Wajib Lulus CI (Continuous Integration):** PR harus bisa di-*build* dan lulus semua *unit test* (jika ada).
4.  Hanya setelah (2) dan (3) terpenuhi, PR boleh di-*merge* ke `main`.

## 3. Konvensi Penamaan

_"Consistency is key."_

| Elemen | Konvensi | Contoh (dari PDF) |
| :--- | :--- | :--- |
| **Classes / Structs** | `PascalCase` | `QueryProcessor`, `ExecutionResult` |
| **Methods / Functions**| `snake_case` | `execute_query()`, `parse_query()` |
| **Variables / Params** | `snake_case` | `transaction_id`, `query_tree` |
| **Constants / Enums** | `UPPER_SNAKE_CASE`| `Action::READ`, `Action::WRITE` |
| **Filenames (Header)** | `snake_case.h` | `query_processor.h`, `types.h` |
| **Filenames (Source)** | `snake_case.cpp` | `query_processor.cpp`, `main.cpp` |
| **Namespaces** | `snake_case` (prefix) | `mdbms::qp`, `mdbms::sm` |
