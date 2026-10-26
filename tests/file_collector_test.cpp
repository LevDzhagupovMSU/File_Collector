#include "../file_collector.hpp"

#include <cstdlib>
#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <future>
#include <optional>
#include <thread>
#include <utility>
#include <vector>
#include <latch>

TEST(FileCollectorTest, OnNewChunk){
// Проверка корректности работы метода OnNewChunk когда Chunk могут пересекаться.
    files::FileCollector collector;
    uint32_t file_id = 1;
    const std::vector<uint8_t> expected{0, 1, 2, 3, 4, 5, 6, 7};
    collector.CollectFile(file_id, expected.size());

    EXPECT_TRUE(collector.GetFile(file_id).empty());
    collector.OnNewChunk(file_id, 4, std::vector<uint8_t>{4, 5, 6, 7});
    collector.OnNewChunk(file_id, 2, std::vector<uint8_t>{2, 3, 4, 5});
    EXPECT_TRUE(collector.GetFile(file_id).empty());
    collector.OnNewChunk(file_id, 0, std::vector<uint8_t>{0, 1, 2});

    EXPECT_EQ(collector.GetFile(file_id), expected);
}

TEST(FileCollectorTest, RepeatChunks){  
// Дубликат уже полученного чанка не должен ошибочно засчитываться как покрытие новых байт.
    files::FileCollector collector;
    uint32_t file_id = 2;
    collector.CollectFile(file_id, 6);

    collector.OnNewChunk(file_id, 0, std::vector<uint8_t>{1, 2, 3});
    collector.OnNewChunk(file_id, 0, std::vector<uint8_t>{1, 2, 3});
    EXPECT_TRUE(collector.GetFile(file_id).empty());

    collector.OnNewChunk(file_id, 3, std::vector<uint8_t>{4, 5, 6});
    EXPECT_EQ(collector.GetFile(file_id), (std::vector<uint8_t>{1, 2, 3, 4, 5, 6}));
}

TEST(FileCollectorTest, FileBound){
// Проверка на правильно вставление чанка и согласование размера файла.
    files::FileCollector collector;
    uint32_t file_id = 3;
    collector.CollectFile(file_id, 4);

    collector.OnNewChunk(file_id, 2, std::vector<uint8_t>{3, 4, 99, 99});
    collector.OnNewChunk(file_id, 0, std::vector<uint8_t>{1, 2});

    EXPECT_EQ(collector.GetFile(file_id), (std::vector<uint8_t>{1, 2, 3, 4}));
}

TEST(FileCollectorTest, GetAndTake){
// Проверка методов Get и Take
    files::FileCollector collector;
    uint32_t file_id = 4;
    collector.CollectFile(file_id, 3);

    collector.OnNewChunk(file_id, 0, std::vector<uint8_t>{1, 2});
    EXPECT_FALSE(collector.TakeFile(file_id).has_value());

    collector.OnNewChunk(file_id, 2, std::vector<uint8_t>{3});
    auto taken = collector.TakeFile(file_id);
    ASSERT_TRUE(taken.has_value());
    EXPECT_EQ(*taken, (std::vector<uint8_t>{1, 2, 3}));
    EXPECT_FALSE(collector.TakeFile(file_id).has_value());
}

TEST(FileCollectorTest, IsReadyFileCheck){
// Проверка корректности рааботы метода IsReadyfile
    files::FileCollector collector;
    uint32_t file_id = 5;

    EXPECT_FALSE(collector.IsReadyFile(file_id));
    collector.CollectFile(file_id, 3);
    EXPECT_FALSE(collector.IsReadyFile(file_id));

    collector.OnNewChunk(file_id, 1, std::vector<uint8_t>{2, 3});
    EXPECT_FALSE(collector.IsReadyFile(file_id));
    collector.OnNewChunk(file_id, 0, std::vector<uint8_t>{1});
    EXPECT_TRUE(collector.IsReadyFile(file_id));

    ASSERT_TRUE(collector.TakeFile(file_id).has_value());
    EXPECT_FALSE(collector.IsReadyFile(file_id));
}

TEST(FileCollectorTest, GetFileProgressCheck){
// Проверка метода GetFileProgress
    files::FileCollector collector;
    uint32_t file_id = 6;

    EXPECT_FALSE(collector.GetFileProgress(file_id).has_value());
    collector.CollectFile(file_id, 4);
    EXPECT_EQ(collector.GetFileProgress(file_id), (std::optional<double>{0.0}));

    collector.OnNewChunk(file_id, 0, std::vector<uint8_t>{1, 2});
    EXPECT_EQ(collector.GetFileProgress(file_id), (std::optional<double>{0.5}));
    collector.OnNewChunk(file_id, 0, std::vector<uint8_t>{1, 2});
    EXPECT_EQ(collector.GetFileProgress(file_id), (std::optional<double>{0.5}));

    collector.OnNewChunk(file_id, 2, std::vector<uint8_t>{3, 4});
    EXPECT_EQ(collector.GetFileProgress(file_id), (std::optional<double>{1.0}));
    ASSERT_TRUE(collector.TakeFile(file_id).has_value());
    EXPECT_FALSE(collector.GetFileProgress(file_id).has_value());

    uint32_t empty_file_id = 7;
    collector.CollectFile(empty_file_id, 0);
    EXPECT_EQ(collector.GetFileProgress(empty_file_id), (std::optional<double>{1.0}));
}


TEST(FileCollectorTest, CollectionFromDifferentChunks){
// Многопоточный тест на копирование из разных чанках в файл.
    files::FileCollector collector;
    uint32_t file_id = 8;
    size_t worker_count = 8;
    size_t file_size = 1024 * 1024;
    size_t block_size = file_size / worker_count;
    size_t overlap = 4096;

    std::vector<uint8_t> expected(file_size);
    for(size_t i = 0; i < expected.size(); ++i)
        expected[i] = static_cast<uint8_t>(rand() % 256);
    collector.CollectFile(file_id, file_size);

    std::latch start_signal{1};
    std::vector<std::thread> workers;
    workers.reserve(worker_count);

    for(size_t i = 0; i < worker_count; ++i){
        size_t pos = i * block_size;
        size_t end = std::min(file_size, pos + block_size + overlap);
        std::vector<uint8_t> chunk(expected.begin() + pos, expected.begin() + end);
        workers.emplace_back([&collector,file_id, &start_signal, pos, chunk = std::move(chunk)]() mutable {
            start_signal.wait();
            collector.OnNewChunk(file_id, pos, std::move(chunk));
        });
    }

    start_signal.count_down();
    for(auto& worker : workers)
        worker.join();

    EXPECT_EQ(collector.GetFile(file_id), expected);
}