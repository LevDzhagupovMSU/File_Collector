#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <memory>
#include <map>
#include <string>
#include <utility>
#include <vector>
#include <mutex>

namespace files {

class FileCollector{
    using Interval = std::pair<size_t, size_t>;

    struct File{
        std::vector<uint8_t> buffer;
        std::map<size_t, size_t> intervals;
        size_t file_size = 0;
        size_t received_size = 0;
        std::mutex mutex_;
        bool taken = false;

        explicit File(size_t file_size_);
    };
    std::unordered_map<uint32_t, std::shared_ptr<File>> files_;
    mutable std::mutex mutex_;
    mutable std::mutex log_mutex_;

    void LogMessage(const std::string& message) const; // Логируем в терминал.
    std::vector<Interval> GetValidIntervals(const std::shared_ptr<File> file, const Interval& cur_interval); // Берем валидный интервал, без гонки даненых и копируем из этого интевала в координатах чанка в buffer.
    void UpdateIntervals(const std::shared_ptr<File>& file, const Interval& interval); // Cоблюдаем инвариант в мапе + храним интервалы байт которые покрыты или покрываются прямо сейчас в других потоках
    
public:
    void CollectFile(uint32_t fileId, size_t fileSize);
    void OnNewChunk(uint32_t fileId, size_t pos, std::vector<uint8_t>&& chunk);

    bool IsReadyFile(uint32_t fileID) const; // Файл готов или нет, если нет файла с таким ID, то возвращаем false
    std::optional<double> GetFileProgress(uint32_t fileID) const; // Прогресс готовности файла в долях : GetFileProgress(id) * 100 = %
    std::optional<std::vector<uint8_t>> TakeFile(uint32_t fileId); // Если готовый файл большой, то мы можем не копировать, а переместить.
    std::vector<uint8_t> GetFile(uint32_t fileId); // Копируем готовый файл.
};

}
