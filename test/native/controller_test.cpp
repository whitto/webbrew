#include "controller.h"
#include <cstdlib>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace brew;
#define CHECK(x) do { if (!(x)) throw std::runtime_error(std::string(__FILE__)+":"+std::to_string(__LINE__)+" " #x); } while (false)
void ready(Controller& c) { c.sample(20, 0); CHECK(c.start(0)); }
void tick(Controller& c, unsigned second, double temp) { c.sample(temp, second*1000); c.tick(second*1000); }
int main() {
  unsigned passed = 0;
  auto test = [&](const char* name, std::function<void()> f) { f(); ++passed; std::cout << "PASS " << name << '\n'; };
  try {
    test("default settings and safe GPIO restrictions", [] {
      Config c; CHECK(validate(c).empty());
      for (int p : {0,1,2,3,4,5,6,7,8,9,10,11,12,15,16,17,34,35,36,39}) { c.ssrPin=p; CHECK(!validate(c).empty()); }
      c=Config{}; c.probePin=c.ssrPin; CHECK(!validate(c).empty());
    });
    test("SSR polarity always maps OFF correctly", [] {
      CHECK(ssrLevel(true,false)==0); CHECK(ssrLevel(true,true)==1);
      CHECK(ssrLevel(false,false)==1); CHECK(ssrLevel(false,true)==0);
    });
    test("reject invalid stage and nonfinite configuration", [] {
      Config c; c.volumeL=NAN; CHECK(!validate(c).empty()); c=Config{}; c.powerW=INFINITY; CHECK(!validate(c).empty());
      c=Config{}; c.stages[0].targetC=99; CHECK(!validate(c).empty());
      c=Config{}; c.stages[1].targetC=98; CHECK(!validate(c).empty());
      c=Config{}; c.maxRunSeconds=3600; CHECK(!validate(c).empty());
      c=Config{}; c.stages.clear(); CHECK(!validate(c).empty());
    });
    test("boot never energizes and start requires fresh probe", [] {
      Controller c; c.tick(1000); CHECK(c.output==0); CHECK(!c.start(1000));
      c.sample(20,1000); CHECK(!c.start(4001)); CHECK(c.start(4000));
    });
    test("stale sensor latches fault and stops heat", [] {
      Controller c; ready(c); tick(c,1,20); CHECK(c.output>0);
      c.tick(4001); CHECK(c.state==State::Fault); CHECK(c.output==0); CHECK(!c.start(4001));
    });
    test("disconnected and NaN sensors stop heat", [] {
      for (double value : {-127.0, std::numeric_limits<double>::quiet_NaN()}) {
        Controller c; ready(c); tick(c,1,20); c.sample(value,2000);
        CHECK(c.state==State::Fault); CHECK(c.output==0); CHECK(!c.resetFault(2000));
      }
    });
    test("over-temperature faults during manual control", [] {
      Controller c; ready(c); CHECK(c.setManual(true,100)); tick(c,1,104); CHECK(c.output==100);
      tick(c,2,105); CHECK(c.output==0); CHECK(c.state==State::Fault); CHECK(!c.resetFault(2000));
      c.sample(102,3000); CHECK(c.resetFault(3000)); CHECK(c.state==State::Idle); CHECK(c.output==0);
    });
    test("control-loop stall turns heating off", [] {
      Controller c; ready(c); c.sample(20,4000); c.tick(4000); CHECK(c.state==State::Fault); CHECK(c.output==0);
    });
    test("hold starts near target and pauses below band", [] {
      Controller c; c.config.stages={{"Hold",60,10,false,70}}; ready(c);
      tick(c,1,50); CHECK(!c.reached); CHECK(c.holdElapsed==0);
      tick(c,2,59.8); CHECK(c.reached); CHECK(c.holdElapsed==1);
      tick(c,3,58); CHECK(c.holdElapsed==1);
    });
    test("manual freezes stage and return to auto resumes", [] {
      Controller c; c.config.stages={{"Hold",60,10,false,70}}; ready(c); tick(c,1,60);
      CHECK(c.setManual(true,37)); tick(c,2,60); CHECK(c.output==37); CHECK(c.holdElapsed==1);
      CHECK(!c.setManual(true,101)); CHECK(!c.setManual(true,NAN));
      CHECK(c.setManual(false,0)); tick(c,3,60); CHECK(c.holdElapsed==2);
    });
    test("pause disables heater and freezes hold", [] {
      Controller c; c.config.stages={{"Hold",60,10,false,70}}; ready(c); tick(c,1,60);
      CHECK(c.pause()); tick(c,2,60); CHECK(c.output==0); CHECK(c.holdElapsed==1);
      CHECK(c.resume(2000)); tick(c,3,60); CHECK(c.holdElapsed==2);
    });
    test("hard session limit includes pauses and manual", [] {
      for (bool manual : {false,true}) {
        Controller c; c.config.maxRunSeconds=60; c.config.stages={{"Hold",60,1,false,70}}; ready(c);
        if (manual) CHECK(c.setManual(true,100)); else CHECK(c.pause());
        for (unsigned i=1;i<=60;i++) tick(c,i,20);
        CHECK(c.state==State::Fault); CHECK(c.output==0);
      }
    });
    test("boil changes from heat-up to selected power", [] {
      Controller c; c.config.stages={{"Boil",100,30,true,65}}; c.sample(90,0); CHECK(c.start(0));
      tick(c,1,90); CHECK(c.output>0); CHECK(!c.reached);
      tick(c,2,99.6); CHECK(c.reached); CHECK(c.output==65);
      tick(c,3,98); CHECK(c.output==100); CHECK(c.holdElapsed==1);
    });
    test("stage advancement and completion shut heating off", [] {
      Controller c; c.config.stages={{"A",60,2,false,70},{"B",70,1,false,70}}; ready(c);
      tick(c,1,60); tick(c,2,60); CHECK(c.stageIndex==1); CHECK(c.output==0);
      tick(c,3,70); CHECK(c.state==State::Complete); CHECK(c.output==0);
    });
    test("stop never restarts automatically", [] {
      Controller c; ready(c); tick(c,1,20); c.stop(); tick(c,2,20);
      CHECK(c.state==State::Idle); CHECK(c.output==0); CHECK(!c.setManual(true,100));
    });
    test("energy-based timing matches water heat capacity", [] {
      Controller c; c.efficiency=1; CHECK(std::abs(c.heatingSeconds(20,78)-2023.233333)<0.01);
      CHECK(c.heatingSeconds(100,78)==0);
    });
    test("learning updates within bounded model and gains each run", [] {
      Controller c; ready(c);
      for (unsigned i=1;i<=100;i++) tick(c,i,20+i*.02);
      CHECK(c.learnedSamples>=2); CHECK(c.efficiency<.8); CHECK(c.efficiency>=.2);
      CHECK(c.kp>=5&&c.kp<=25); CHECK(c.ki>=.02&&c.ki<=.12); CHECK(c.kd>=15&&c.kd<=75);
      c.stop(); c.sample(20,101000); CHECK(c.start(101000)); CHECK(c.learnedSamples==0);
    });
    test("disabled learning leaves effective efficiency unchanged", [] {
      Controller c; c.config.learning=false; ready(c);
      for (unsigned i=1;i<=100;i++) tick(c,i,20+i*.02);
      CHECK(c.learnedSamples==0); CHECK(c.efficiency==.8);
    });
    test("millis rollover preserves freshness and elapsed time", [] {
      Controller c; uint32_t start=UINT32_MAX-500; c.sample(20,start); CHECK(c.start(start));
      c.sample(20,499); c.tick(499); CHECK(c.state==State::Running); CHECK(c.runElapsed==1);
    });
    test("simulated 25 L kettle completes hold and boil", [] {
      Controller c; c.config.stages={{"Hold",78,60,false,70},{"Boil",100,60,true,70}}; ready(c);
      double t=20, peak=20;
      for(unsigned i=1;i<10000&&c.state==State::Running;i++) {
        t += (3000*c.output/100 - 2*(t-20))/(25*4186);
        t=std::min(100.0,t); peak=std::max(peak,t);
        tick(c,i,t); CHECK(c.output>=0&&c.output<=100);
      }
      CHECK(c.state==State::Complete); CHECK(c.output==0); CHECK(peak<=100);
    });
  } catch(const std::exception& e) { std::cerr<<"FAIL "<<e.what()<<'\n'; return EXIT_FAILURE; }
  std::cout<<passed<<" tests passed\n";
}
