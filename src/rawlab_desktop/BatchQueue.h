#pragma once

#include "ApplicationSettings.h"
#include "PictureRecipe.h"
#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <utility>
#include <vector>

// A session queue owns its recipe and settings snapshot. Worker attempts return
// an updated copy; the GUI never reads queue state while a worker is changing it.
struct BatchQueue
{
  enum class State { Pending, Running, Completed, Failed, Cancelled };
  struct Job
  {
    QString source;
    QString output;
    State state = State::Pending;
    QString message;
    QString publishedHash;
    bool retryable() const
    {
      return state == State::Pending || state == State::Failed || state == State::Cancelled;
    }
  };
  PictureRecipe recipe;
  ApplicationSettingsData settings;
  std::vector<Job> jobs;
  QString snapshotId;
  QString receiptPath;
  bool temporary = false;

  static BatchQueue create(const QStringList &sources, const QString &directory,
                           PictureRecipe recipe, ApplicationSettingsData settings)
  {
    BatchQueue queue;
    queue.recipe = std::move(recipe);
    queue.settings = std::move(settings);
    QSet<QString> reserved;
    QSet<QString> seenSources;
    for(const QString &source : sources)
    {
      const QFileInfo info{source};
      const QString identity = info.canonicalFilePath().isEmpty() ? info.absoluteFilePath() : info.canonicalFilePath();
      if(seenSources.contains(identity)) continue;
      seenSources.insert(identity);
      const QString stem = info.completeBaseName() + QStringLiteral("-rawlab");
      QString output = QDir{directory}.filePath(stem + QStringLiteral(".dng"));
      int suffix = 1;
      while(reserved.contains(output) || QFileInfo::exists(output) || QFileInfo::exists(output + ".xmp"))
        output = QDir{directory}.filePath(stem + QStringLiteral("_%1.dng").arg(suffix++));
      reserved.insert(output);
      queue.jobs.push_back({info.absoluteFilePath(), QFileInfo{output}.absoluteFilePath(), State::Pending, {}, {}});
    }
    return queue;
  }
  int completed() const
  {
    int count = 0;
    for(const auto &job : jobs) if(job.state == State::Completed) ++count;
    return count;
  }
  int remaining() const
  {
    int count = 0;
    for(const auto &job : jobs) if(job.retryable()) ++count;
    return count;
  }
};
