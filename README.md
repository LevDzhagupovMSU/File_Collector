# FileCollector

`FileCollector` собирает файл из чанков, которые могут приходить в произвольном порядке, пересекаться или повторяться. Для каждого файла буфер выделяется при вызове `CollectFile`. Разные потоки могут передавать чанки одного файла одновременно.

## Сборка и тесты

Из корня проекта:

```bash
cmake -S . -B build
cmake --build build
сd build
ctest 
```

По умолчанию CMake собирает библиотеку `file_collector` и тесты `file_collector_tests`. Для запуска тестового исполняемого файла напрямую:

```bash
./build/file_collector_tests
```

## Использование

```cpp
#include "file_collector.hpp"

#include <cstdint>
#include <utility>
#include <vector>

int main() {
    files::FileCollector collector;
    const uint32_t file_id = 1;

    collector.CollectFile(file_id, 4);
    collector.OnNewChunk(file_id, 2, std::vector<uint8_t>{3, 4});
    collector.OnNewChunk(file_id, 0, std::vector<uint8_t>{1, 2});

    if (collector.IsReadyFile(file_id)) {
        auto data = collector.TakeFile(file_id);
        // data содержит {1, 2, 3, 4}.
    }
}
```

`GetFileProgress(file_id)` возвращает долю полученных байт от `0.0` до `1.0` или `std::nullopt`, если файла нет. `GetFile(file_id)` возвращает копию готового файла; до готовности он возвращает пустой вектор. `TakeFile(file_id)` перемещает готовый буфер в `std::optional<std::vector<uint8_t>>` и удаляет файл из сборщика, поэтому подходит для больших файлов без полного копирования.