#include "rawlab/raw/BayerProcessing.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

int main()
{
  using namespace rawlab::raw;
  try
  {
    for(int size : {1, 3, 65})
    {
      RawMetadata m;
      m.cfa.isBayer = true;
      m.cfa.cells[0][0] = CfaColor::Red; m.cfa.cells[0][1] = CfaColor::Green;
      m.cfa.cells[1][0] = CfaColor::Green; m.cfa.cells[1][1] = CfaColor::Blue;
      m.whiteLevel = 4095; m.blackLevel = {100, 110, 120, 130};
      m.activeArea = {1, 1, size, size};
      const int w = size + 2;
      std::vector<float> pixels(w*w, 0);
      for(int y = 1; y <= size; ++y) for(int x = 1; x <= size; ++x)
        pixels[y*w+x] = 800 + 100*siteIndex(x,y) + (((x/2+y/2)%2) ? 8 : -8);
      RawImage input{w,w,pixels,m};
      if(mildRawDenoise(input,0).samples() != pixels) throw std::runtime_error{"Zero is not identity"};
      const auto output = mildRawDenoise(input,1);
      double before = 0, after = 0;
      for(int y = 1; y <= size; ++y) for(int x = 1; x <= size; ++x)
      {
        const float expected = 800 + 100*siteIndex(x,y);
        before += std::pow(input.sample(x,y)-expected,2);
        after += std::pow(output.sample(x,y)-expected,2);
      }
      if(size == 65 && !(after < before * 0.5)) throw std::runtime_error{"Noise not reduced"};
      if(output.sample(0,0) != 0 || !std::isfinite(after)) throw std::runtime_error{"Invalid borders/output"};
      bool rejected = false;
      try { mildRawDenoise(input,std::numeric_limits<float>::quiet_NaN()); }
      catch(const std::invalid_argument &) { rejected = true; }
      if(!rejected) throw std::runtime_error{"Invalid strength accepted"};
    }
  }
  catch(const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
