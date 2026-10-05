#include "../Source/AssblasterDSP.h"
#include <cstdio>
#include <vector>
using namespace assblaster;
int main(){
  const float fs=96000;
  // 1. thyratron free-run pitch accuracy
  for (float f : {55.f,110.f,440.f,2000.f}) {
    Thyratron t; t.prepare(fs); int fires=0; float prev=-1; 
    for(int i=0;i<(int)fs*2;i++){ t.process(f,0,0); }
    // count resets over 2 s by watching raw v
    t.reset(); float lastv=t.v; for(int i=0;i<(int)fs*2;i++){ t.process(f,0,0); if(t.v<lastv) ++fires; lastv=t.v; }
    std::printf("thyratron target %7.1f Hz -> measured %7.2f Hz\n", f, fires/2.0f);
  }
  // 2. pulser sync: 110 Hz sine in, ratio r -> estimated period & saw rate
  for (float r : {1.f,2.f,3.f}) {
    Pulser p; p.prepare(fs); double dummy=0; 
    for(int i=0;i<(int)fs;i++){ float x=0.5f*std::sin(2*kPi*110.f*i/fs); dummy+=p.process(x,0.3f,r); }
    std::printf("pulser ratio %.0f: detected input f0 = %.2f Hz (true 110)\n", r, fs/p.period);
  }
}
