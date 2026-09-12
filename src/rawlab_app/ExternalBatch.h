#pragma once
#include "BatchQueue.h"
#include <QFile>
#include <QSaveFile>
#include <stdexcept>

// UTF-8 paths are hex-encoded so tabs, newlines, spaces and shell characters
// never become protocol delimiters. Lua does not need a JSON dependency.
struct ExternalBatch
{
  static QString decode(const QByteArray &hex)
  {
    if(hex.isEmpty() || hex.size() % 2) throw std::runtime_error{"Invalid batch path encoding"};
    for(char c : hex)
      if(!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
        throw std::runtime_error{"Invalid batch path encoding"};
    const auto bytes = QByteArray::fromHex(hex);
    const QString path = QString::fromUtf8(bytes);
    if(path.toUtf8() != bytes || bytes.contains('\0') || !QDir::isAbsolutePath(path))
      throw std::runtime_error{"Batch paths must be absolute UTF-8 paths"};
    return path;
  }
  static std::vector<BatchQueue::Job> read(const QString &path)
  {
    QFile file{path};
    if(!file.open(QIODevice::ReadOnly) || file.size() > 4 * 1024 * 1024)
      throw std::runtime_error{"Cannot read Darktable batch request"};
    if(file.readLine().trimmed() != "rawlab-batch-v1")
      throw std::runtime_error{"Unsupported Darktable batch protocol; update RawLab and RawLab.lua together"};
    std::vector<BatchQueue::Job> jobs;
    QSet<QString> sources, outputs;
    while(!file.atEnd())
    {
      const auto fields = file.readLine().trimmed().split('\t');
      if(fields.size() != 2) throw std::runtime_error{"Invalid batch request record"};
      const QString source = decode(fields[0]);
      const QString output = decode(fields[1]);
      if(sources.contains(source) || outputs.contains(output) || jobs.size() >= 10000)
        throw std::runtime_error{"Duplicate or excessive batch jobs"};
      sources.insert(source);
      outputs.insert(output);
      jobs.push_back({source, output, BatchQueue::State::Pending, {}, {}});
    }
    if(jobs.empty()) throw std::runtime_error{"Empty Darktable batch"};
    for(const auto &job : jobs)
      if(outputs.contains(job.source)) throw std::runtime_error{"Batch output cannot replace an input"};
    return jobs;
  }
  static void writeReceipt(const BatchQueue &queue)
  {
    if(queue.receiptPath.isEmpty()) return;
    QByteArray bytes = "rawlab-results-v1\n";
    bytes += queue.completed() == static_cast<int>(queue.jobs.size()) ? "complete\n" : "partial\n";
    for(const auto &job : queue.jobs)
      if(job.state == BatchQueue::State::Completed)
        bytes += job.source.toUtf8().toHex() + '\t' + job.output.toUtf8().toHex() + '\n';
    QSaveFile file{queue.receiptPath};
    if(!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
      throw std::runtime_error{"Cannot write Darktable completion receipt"};
  }
};
