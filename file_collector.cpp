#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

#include "file_collector.hpp"

files::FileCollector::File::File(size_t file_size_) : file_size(file_size_) {};

void files::FileCollector::LogMessage(const std::string& message) const{
    std::lock_guard<std::mutex> lock(log_mutex_);
    std::cout << message << std::endl;
}

void files::FileCollector::CollectFile(uint32_t fileId, size_t fileSize){
    std::shared_ptr<File> file = std::make_shared<File>(fileSize);
    file->buffer.resize(file->file_size);

    {
        std::lock_guard<std::mutex> lc(mutex_);
        auto it = files_.find(fileId);
        if(it == files_.end()){
            files_[fileId] = std::move(file);
            return;
        }
    }
    LogMessage("This fileID is already taken.");
}

void files::FileCollector::OnNewChunk(uint32_t fileId, size_t pos, std::vector<uint8_t>&& chunk){
    if(chunk.empty())
        return;

    std::shared_ptr<File> cur_file = nullptr;

    {
        std::lock_guard<std::mutex> lc(mutex_);
        auto it = files_.find(fileId);
        if(it != files_.end())
            cur_file = it->second;
    }
    if(!cur_file){
        LogMessage("There is no file with this FileID.");
        return;
    }

    if(pos >= cur_file->file_size){
        LogMessage("Position is not valid: " + std::to_string(pos) + ">" +
                   std::to_string(cur_file->file_size));

        return;
    }

    size_t chunk_begin = pos, chunk_end = pos + std::min(chunk.size(), cur_file->file_size - pos);
    std::vector<Interval> intervals;

    {
        std::lock_guard<std::mutex> lc(cur_file->mutex_);
        intervals = GetValidIntervals(cur_file, {chunk_begin, chunk_end});
        if(!intervals.empty())
            UpdateIntervals(cur_file, {chunk_begin, chunk_end});
    }

    for(const auto& [begin, end] : intervals){
        std::memcpy(cur_file->buffer.data() + begin, chunk.data() + (begin - pos), end - begin);
        std::lock_guard<std::mutex> lc(cur_file->mutex_);
        cur_file->received_size += end - begin;
    }
}

std::vector<uint8_t> files::FileCollector::GetFile(uint32_t fileId){
    std::shared_ptr<File> file = nullptr;

    {
        std::lock_guard<std::mutex> lc(mutex_);
        auto it = files_.find(fileId);
        if(it != files_.end())
            file = it->second;
    }
    if(!file){
        LogMessage("No such a file");
        
        return {};
    }

    std::vector<uint8_t> result;
    bool ready;
    {
        std::lock_guard<std::mutex> lc(file->mutex_);
        if(file->taken){
            LogMessage("File was taken");

            return {};
        }
        ready = file->received_size == file->file_size;
        if(ready)
            result = file->buffer;
    }

    LogMessage(ready ? "File successfully received" : "The file is incomplete");
    return result;
}

std::vector<files::FileCollector::Interval> files::FileCollector::GetValidIntervals(const std::shared_ptr<files::FileCollector::File> file, 
                                                                                    const files::FileCollector::Interval& cur_interval){
    std::vector<Interval> result;
    auto first = file->intervals.upper_bound(cur_interval.first);
    if(first != file->intervals.begin())
        first = std::prev(first); 
    auto last = file->intervals.lower_bound(cur_interval.second);
    size_t bytes_iterator = cur_interval.first;
    for(auto it = first; it != last; it = std::move(next(it))){
        if(it->second <= bytes_iterator)
            continue;

        if(it->first > bytes_iterator)
            result.push_back({bytes_iterator, it->first});
        
        bytes_iterator = std::min(it->second, cur_interval.second);
        if(bytes_iterator == cur_interval.second)
            break;
    }
    
    if(bytes_iterator < cur_interval.second){
        result.push_back({bytes_iterator, cur_interval.second});
        bytes_iterator = cur_interval.second;
    }

    return result;
}

void files::FileCollector::UpdateIntervals(const std::shared_ptr<File>& file, const Interval& interval){
    size_t merged_begin = interval.first;
    size_t merged_end = interval.second;

    auto it = file->intervals.lower_bound(merged_begin);
    if(it != file->intervals.begin() && std::prev(it)->second >= merged_begin)
        it = std::move(std::prev(it));

    while(it != file->intervals.end() && it->first <= merged_end){
        merged_begin = std::min(merged_begin, it->first);
        merged_end = std::max(merged_end, it->second);
        it = file->intervals.erase(it);
    }

    file->intervals.emplace(merged_begin, merged_end);
}

bool files::FileCollector::IsReadyFile(uint32_t fileID) const{
    std::shared_ptr<File> file = nullptr;
    {
        std::lock_guard<std::mutex> lc(mutex_);
        auto it  = files_.find(fileID);
        if(it == files_.end()){
            LogMessage("No such a file");

            return false;
        }
        file = it->second;
    }
    std::lock_guard<std::mutex> lc_file(file->mutex_);
    if(file->taken){
        LogMessage("File was taken");

        return false;
    }
    return file->file_size == file->received_size;
}

std::optional<double> files::FileCollector::GetFileProgress(uint32_t fileID) const{
    std::shared_ptr<File> file = nullptr;
    {
        std::lock_guard<std::mutex> lc(mutex_);
        auto it = files_.find(fileID);
        if(it == files_.end()){
            LogMessage("No such a file");

            return std::nullopt;
        }
        file = it->second;
    }
    std::lock_guard<std::mutex> lc_file(file->mutex_);
    if(file->taken){
        LogMessage("File was taken");
        
        return std::nullopt;
    }
    if(file->file_size == 0)
        return std::make_optional(1.0);
    return std::make_optional(static_cast<double>(file->received_size) / static_cast<double>(file->file_size));
}

std::optional<std::vector<uint8_t>> files::FileCollector::TakeFile(uint32_t fileID){
    std::shared_ptr<File> file = nullptr;

    {
        std::lock_guard<std::mutex> lc(mutex_);
        auto it = files_.find(fileID);
        if(it != files_.end())
            file = it->second;
    }
    
    if(!file){
        LogMessage("No such a file");

        return {};
    }
    std::optional<std::vector<uint8_t>> result;
    {
        std::lock_guard<std::mutex> lc(file->mutex_);
        if(file->taken){
            LogMessage("File was taken");
            return std::nullopt;
        }
        if(file->received_size != file->file_size){
            LogMessage("The file is incomplete");
            return std::nullopt;
        }
        file->taken = true;
        result.emplace(std::move(file->buffer));
    }

    {
        std::lock_guard<std::mutex> lc(mutex_);
        auto it = files_.find(fileID);
        if(it != files_.end() && it->second == file)
            files_.erase(it);
    }

    return result;
}