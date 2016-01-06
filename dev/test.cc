/*This is not a substitute for a proper HPWH Test Tool, it is merely a short program
 * to aid in the testing of the new HPWH.cc as it is being written.
 * 
 * -NDK
 * 
 * 
 */
#include "HPWH.hh"


#define F_TO_C(T) ((T-32.0)*5.0/9.0)
#define GAL_TO_L(GAL) (GAL*3.78541)

int main(void)
{


HPWH hpwh;

//hpwh.HPWHinit_presets(1);

//int HPWH::runOneStep(double inletT_C, double drawVolume_L, 
					//double ambientT_C, double externalT_C,
					//double DRstatus, double minutesPerStep)
//hpwh.runOneStep(0, 120, 50, 50, 1, 1);
//hpwh.printTankTemps();
//hpwh.runOneStep(0, 5, 50, 50, 1, 1);
//hpwh.printTankTemps();
//hpwh.runOneStep(0, 5, 50, 50, 1, 1);
//hpwh.printTankTemps();
//hpwh.runOneStep(0, 5, 50, 50, 1, 1);
//hpwh.printTankTemps();
//hpwh.runOneStep(0, 5, 50, 50, 1, 1);
//hpwh.printTankTemps();
//hpwh.runOneStep(0, 5, 50, 50, 1, 1);
//hpwh.printTankTemps();
//hpwh.runOneStep(0, 10, 50, 50, 1, 1);
//hpwh.printTankTemps();
//hpwh.runOneStep(0, 10, 50, 50, 1, 1);
//hpwh.printTankTemps();
//hpwh.runOneStep(0, 0, 50, 50, 1, 1);
//hpwh.printTankTemps();
//hpwh.runOneStep(0, 0, 50, 50, 1, 1);
//hpwh.printTankTemps();
//hpwh.runOneStep(0, 0, 50, 50, 1, 1);
//hpwh.printTankTemps();
//hpwh.runOneStep(0, 0, 50, 50, 1, 1);
//hpwh.printTankTemps();
//hpwh.runOneStep(0, 0, 50, 50, 1, 1);
//hpwh.printTankTemps();
//hpwh.runOneStep(0, 0, 50, 50, 1, 1);
//hpwh.printTankTemps();
//hpwh.runOneStep(0, 0, 50, 50, 1, 1);
//hpwh.printTankTemps();
//hpwh.runOneStep(0, 0, 50, 50, 1, 1);
//hpwh.printTankTemps();
//hpwh.runOneStep(0, 0, 50, 50, 1, 1);
//hpwh.printTankTemps();
//hpwh.runOneStep(0, 0, 50, 50, 1, 1);
//hpwh.printTankTemps();
//hpwh.runOneStep(0, 0, 50, 50, 1, 1);
//hpwh.printTankTemps();
//hpwh.runOneStep(0, 0, 50, 50, 1, 1);
//hpwh.printTankTemps();
//hpwh.runOneStep(0, 0, 50, 50, 1, 1);
//hpwh.printTankTemps();
//hpwh.runOneStep(0, 0, 50, 50, 1, 1);
//hpwh.printTankTemps();
//hpwh.runOneStep(0, 0, 50, 50, 1, 1);
//hpwh.printTankTemps();
//hpwh.runOneStep(0, 0, 50, 50, 1, 1);
//hpwh.printTankTemps();
//hpwh.runOneStep(0, 0, 50, 50, 1, 1);
//hpwh.printTankTemps();
//hpwh.runOneStep(0, 0, 50, 50, 1, 1);
//hpwh.printTankTemps();
//hpwh.runOneStep(0, 0, 50, 50, 1, 1);
//hpwh.printTankTemps();
//hpwh.runOneStep(0, 0, 50, 50, 1, 1);
//hpwh.printTankTemps();
//hpwh.runOneStep(0, 0, 50, 50, 1, 1);
//hpwh.printTankTemps();
//hpwh.runOneStep(0, 0, 50, 50, 1, 1);
//hpwh.printTankTemps();
//hpwh.runOneStep(0, 0, 50, 50, 1, 1);
//hpwh.printTankTemps();
//hpwh.runOneStep(0, 0, 50, 50, 1, 1);
//hpwh.printTankTemps();
//hpwh.runOneStep(0, 0, 50, 50, 1, 1);
//hpwh.printTankTemps();
//hpwh.runOneStep(0, 0, 50, 50, 1, 1);
//hpwh.printTankTemps();
//hpwh.runOneStep(0, 0, 50, 50, 1, 1);
//hpwh.printTankTemps();
//hpwh.runOneStep(0, 0, 50, 50, 1, 1);
//hpwh.printTankTemps();
//hpwh.runOneStep(0, 0, 50, 50, 1, 1);
//hpwh.printTankTemps();
//hpwh.runOneStep(0, 10, 50, 50, 1, 1);
//hpwh.printTankTemps();
//hpwh.runOneStep(0, 10, 50, 50, 1, 1);
//hpwh.printTankTemps();
//hpwh.runOneStep(0, 10, 50, 50, 1, 1);
//hpwh.printTankTemps();
//hpwh.runOneStep(0, 10, 50, 50, 1, 1);
//hpwh.printTankTemps();
//hpwh.runOneStep(0, 0, 50, 50, 1, 1);
//hpwh.printTankTemps();
//hpwh.runOneStep(0, 0, 50, 50, 1, 1);
//hpwh.printTankTemps();
//hpwh.runOneStep(0, 0, 50, 50, 1, 1);
//hpwh.printTankTemps();
//hpwh.runOneStep(0, 0, 50, 50, 1, 1);
//hpwh.printTankTemps();
//hpwh.runOneStep(0, 0, 50, 50, 1, 1);
//hpwh.printTankTemps();
//hpwh.runOneStep(0, 0, 50, 50, 1, 1);
//hpwh.printTankTemps();


hpwh.HPWHinit_presets(1);
//int HPWH::runOneStep(double inletT_C, double drawVolume_L, 
					//double ambientT_C, double externalT_C,
					//double DRstatus, double minutesPerStep)

          
hpwh.runOneStep(0, 25, 0, 50, 1, 1);
hpwh.printTankTemps();
hpwh.runOneStep(0, 0, 0, 50, 1, 1);
hpwh.printTankTemps();
hpwh.runOneStep(0, 25, 0, 50, 1, 1);
hpwh.printTankTemps();
hpwh.runOneStep(0, 0, 0, 50, 1, 1);
hpwh.printTankTemps();
hpwh.runOneStep(0, 25, 0, 50, 1, 1);
hpwh.printTankTemps();
hpwh.runOneStep(0, 0, 0, 50, 1, 1);
hpwh.printTankTemps();
hpwh.runOneStep(0, 25, 0, 50, 1, 1);
hpwh.printTankTemps();
hpwh.runOneStep(0, 0, 0, 50, 1, 1);
hpwh.printTankTemps();
hpwh.runOneStep(0, 0, 0, 50, 1, 1);
hpwh.printTankTemps();
hpwh.runOneStep(0, 0, 0, 50, 1, 1);
hpwh.printTankTemps();
hpwh.runOneStep(0, 0, 0, 50, 1, 1);
hpwh.printTankTemps();
hpwh.runOneStep(0, 0, 0, 50, 1, 1);
hpwh.printTankTemps();
hpwh.runOneStep(0, 0, 0, 50, 1, 1);
hpwh.printTankTemps();
//number 22 below
hpwh.runOneStep(0, 0, 0, 50, 1, 1);
hpwh.printTankTemps();
hpwh.runOneStep(0, 0, 0, 50, 1, 1);
hpwh.printTankTemps();
hpwh.runOneStep(0, 0, 0, 50, 1, 1);
hpwh.printTankTemps();
hpwh.runOneStep(0, 0, 0, 50, 1, 1);
hpwh.printTankTemps();
hpwh.runOneStep(0, 0, 0, 50, 1, 1);
hpwh.printTankTemps();
hpwh.runOneStep(0, 0, 0, 50, 1, 1);
hpwh.printTankTemps();
hpwh.runOneStep(0, 0, 0, 50, 1, 1);
hpwh.printTankTemps();
hpwh.runOneStep(0, 0, 0, 50, 1, 1);
hpwh.printTankTemps();
hpwh.runOneStep(0, 0, 0, 50, 1, 1);
hpwh.printTankTemps();
hpwh.runOneStep(0, 0, 0, 50, 1, 1);
hpwh.printTankTemps();
hpwh.runOneStep(0, 0, 0, 50, 1, 1);
hpwh.printTankTemps();
hpwh.runOneStep(0, 0, 0, 50, 1, 1);
hpwh.printTankTemps();
hpwh.runOneStep(0, 0, 0, 50, 1, 1);
hpwh.printTankTemps();
hpwh.runOneStep(0, 0, 0, 50, 1, 1);
hpwh.printTankTemps();
hpwh.runOneStep(0, 0, 0, -10, 1, 1);
hpwh.printTankTemps();
hpwh.runOneStep(0, 0, 0, -10, 1, 1);
hpwh.printTankTemps();
hpwh.runOneStep(0, 0, 0, -10, 1, 1);
hpwh.printTankTemps();
hpwh.runOneStep(0, 0, 0, 50, 1, 1);
hpwh.printTankTemps();
hpwh.runOneStep(0, 0, 0, 50, 1, 1);
hpwh.printTankTemps();
hpwh.runOneStep(0, 0, 0, 50, 1, 1);
hpwh.printTankTemps();
hpwh.runOneStep(0, 0, 0, 50, 1, 1);
hpwh.printTankTemps();
hpwh.runOneStep(0, 0, 0, 50, 1, 1);
hpwh.printTankTemps();



//hpwh.HPWHinit_presets(3);
//for (int i = 0; i < 1440*365; i++) {
    //hpwh.runOneStep(0, 0.2, 0, 50, 1, 1);
//}



return 0;

}