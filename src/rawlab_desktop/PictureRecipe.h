#pragma once

#include "rawlab/pipeline/Mask.h"

#include <QPointF>
#include <QString>
#include <QStringList>
#include <QJsonObject>

#include <array>
#include <optional>
#include <vector>

enum class SharpenMode { Off, Normal, Ai };

struct PictureRecipePrompt
{
  float x = 0.0f;
  float y = 0.0f;
  bool foreground = true;
};

struct PictureRecipeStroke
{
  std::vector<QPointF> points;
  float radius = 0.03f;
  float hardness = 0.8f;
  float value = 1.0f;
  QString tool = QStringLiteral("Brush");
};

struct PictureRecipeLayer
{
  int id = 0;
  QString name;
  QString modelId;
  float blend = 1.0f;
  bool sharpenEnabled = false;
  float sharpenStrength = 1.0f;
  SharpenMode sharpenMode = SharpenMode::Off;
  bool enabled = true;
  bool inverted = false;
  bool showMask = true;
  float feather = 0.0f;
  float blur = 0.0f;
  std::optional<rawlab::pipeline::Mask> baseMask;
  std::array<std::optional<rawlab::pipeline::Mask>, 3> categoryMasks;
  std::array<std::vector<PictureRecipeStroke>, 3> categoryStrokes;
  std::vector<PictureRecipePrompt> prompts;
  std::vector<PictureRecipeStroke> strokes;
};

struct PictureRecipe
{
  QJsonObject desktopEdits;
  int previewWidth = 0;
  int previewHeight = 0;
  QString globalModelId;
  QString sharpenModelId;
  float globalBlend = 1.0f;
  bool globalEnabled = true;
  bool globalSharpenEnabled = false;
  float globalSharpenStrength = 1.0f;
  SharpenMode globalSharpenMode = SharpenMode::Off;
  bool preserveClipped = true;
  std::vector<PictureRecipeLayer> layers;
};

enum class PictureRecipeLoadOutcome
{
  Loaded,
  Missing,
  Unreadable,
  Invalid,
  FutureVersion,
  MissingMask
};

struct PictureRecipeLoadResult
{
  PictureRecipeLoadOutcome outcome = PictureRecipeLoadOutcome::Invalid;
  std::optional<PictureRecipe> recipe;
  QString message;
};

QString pictureRecipeLoadOutcomeName(PictureRecipeLoadOutcome outcome);

class PictureRecipeStore
{
public:
  std::optional<PictureRecipe> load(const QString &sourcePath,
                                    int targetWidth, int targetHeight) const;
  PictureRecipeLoadResult loadResult(const QString &sourcePath,
                                     int targetWidth, int targetHeight) const;
  void save(const QString &sourcePath, const PictureRecipe &recipe) const;
  void remove(const QString &sourcePath) const;
  QStringList presetNames() const;
  std::optional<PictureRecipe> loadPreset(const QString &name,
                                           int targetWidth, int targetHeight) const;
  PictureRecipeLoadResult loadPresetResult(const QString &name,
                                           int targetWidth, int targetHeight) const;
  void savePreset(const QString &name, const PictureRecipe &recipe) const;
  void removePreset(const QString &name) const;

  static QString pathForSource(const QString &sourcePath);
  static rawlab::pipeline::Mask resizeMask(const rawlab::pipeline::Mask &source,
                                           int targetWidth, int targetHeight);
  static PictureRecipe scaled(const PictureRecipe &source, int targetWidth, int targetHeight);

  // Used by durable batch snapshots; masks remain beside the recipe JSON.
  static std::optional<PictureRecipe> loadAtPath(const QString &path,
                                                  int targetWidth, int targetHeight, bool strict = false);
  static PictureRecipeLoadResult loadAtPathResult(const QString &path,
                                                   int targetWidth, int targetHeight);
  static void saveAtPath(const QString &path, const PictureRecipe &recipe,
                         const QString &presetName = {});
private:
  static QString presetPath(const QString &name);
  static QString presetDirectory();
};
