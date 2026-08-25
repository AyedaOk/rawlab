#pragma once

#include "rawlab/pipeline/Mask.h"

#include <QPointF>
#include <QString>

#include <optional>
#include <vector>

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
};

struct PictureRecipeLayer
{
  int id = 0;
  QString name;
  QString modelId;
  float blend = 1.0f;
  bool sharpenEnabled = false;
  float sharpenStrength = 1.0f;
  bool enabled = true;
  bool inverted = false;
  bool showMask = true;
  float feather = 0.0f;
  float blur = 0.0f;
  std::optional<rawlab::pipeline::Mask> baseMask;
  std::vector<PictureRecipePrompt> prompts;
  std::vector<PictureRecipeStroke> strokes;
};

struct PictureRecipe
{
  int previewWidth = 0;
  int previewHeight = 0;
  QString globalModelId;
  float globalBlend = 1.0f;
  bool globalEnabled = true;
  bool globalSharpenEnabled = false;
  float globalSharpenStrength = 1.0f;
  bool preserveClipped = true;
  std::vector<PictureRecipeLayer> layers;
};

class PictureRecipeStore
{
public:
  std::optional<PictureRecipe> load(const QString &sourcePath,
                                    int targetWidth, int targetHeight) const;
  void save(const QString &sourcePath, const PictureRecipe &recipe) const;

  static QString pathForSource(const QString &sourcePath);
  static rawlab::pipeline::Mask resizeMask(const rawlab::pipeline::Mask &source,
                                           int targetWidth, int targetHeight);
  static PictureRecipe scaled(const PictureRecipe &source, int targetWidth, int targetHeight);
};
