#include "rawlab/raw/RawInspectInfo.h"

#include <string>

namespace rawlab::raw {

std::string toString(CfaColor color)
{
  switch(color)
  {
    case CfaColor::Red: return "R";
    case CfaColor::Green: return "G";
    case CfaColor::Blue: return "B";
    case CfaColor::Unknown: return "?";
  }
  return "?";
}

std::string toString(const CfaPattern &pattern)
{
  std::string text;
  text.reserve(4);
  text += toString(pattern.cells[0][0]);
  text += toString(pattern.cells[0][1]);
  text += toString(pattern.cells[1][0]);
  text += toString(pattern.cells[1][1]);
  return text;
}

CfaColor colorAt(const CfaPattern &pattern, const int x, const int y)
{
  return pattern.cells[y & 1][x & 1];
}

int siteIndex(const int x, const int y)
{
  return ((y & 1) << 1) | (x & 1);
}

bool redOrigin(const CfaPattern &pattern, int &x, int &y)
{
  if(!pattern.isBayer) return false;
  for(int row = 0; row < 2; ++row)
  {
    for(int col = 0; col < 2; ++col)
    {
      if(pattern.cells[row][col] == CfaColor::Red)
      {
        x = col;
        y = row;
        return true;
      }
    }
  }
  return false;
}

}
