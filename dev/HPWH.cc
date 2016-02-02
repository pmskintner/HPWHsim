#include "HPWH.hh"


using std::endl;
using std::string;


//the HPWH functions
//the publics
HPWH::HPWH() :
  simHasFailed(false), isHeating(false), hpwhVerbosity(VRB_silent), messageCallback(NULL),
  messageCallbackContextPtr( NULL)
{  }

HPWH::~HPWH() {
  delete[] tankTemps_C;
  delete[] setOfSources;  
}


int HPWH::runOneStep(double inletT_C, double drawVolume_L, 
                     double tankAmbientT_C, double heatSourceAmbientT_C,
                     DRMODES DRstatus, double minutesPerStep) {
//returns 0 on successful completion, HPWH_ABORT on failure
  static char outputString[MAXOUTSTRING];  //this is used for debugging outputs
  if(hpwhVerbosity >= VRB_typical) {
    sayMessage("Beginning runOneStep.  \nTank Temps: ");
    printTankTemps();
    sprintf(outputString, "Step Inputs: InletT_C:  %.2lf, drawVolume_L:  %.2lf, tankAmbientT_C:  %.2lf, heatSourceAmbientT_C:  %.2lf, DRstatus:  %d, minutesPerStep:  %.2lf \n",
                          inletT_C, drawVolume_L, tankAmbientT_C, heatSourceAmbientT_C, DRstatus, minutesPerStep);
    sayMessage(string(outputString));
  }
  //is the failure flag is set, don't run
  if (simHasFailed) {
    if(hpwhVerbosity >= VRB_reluctant) sayMessage("simHasFailed is set, aborting.  \n");
    return HPWH_ABORT;
  }
  

  
  //reset the output variables
  outletTemp_C = 0;
  energyRemovedFromEnvironment_kWh = 0;
  standbyLosses_kWh = 0;

  for (int i = 0; i < numHeatSources; i++) {
    setOfSources[i].runtime_min = 0;
    setOfSources[i].energyInput_kWh = 0;
    setOfSources[i].energyOutput_kWh = 0;
  }

  // if you are doing temp. depression, set tank and heatSource ambient temps
  // to the tracked locationTemperature
  double temperatureGoal = tankAmbientT_C;
  if (doTempDepression) {
    tankAmbientT_C = locationTemperature;
    heatSourceAmbientT_C = locationTemperature;
  }
  

  //process draws and standby losses
  updateTankTemps(drawVolume_L, inletT_C, tankAmbientT_C, minutesPerStep);


  //do HeatSource choice
  for (int i = 0; i < numHeatSources; i++) {
    if (hpwhVerbosity >= VRB_emetic){
      sprintf(outputString, "Heat source choice:\theatsource %d can choose from %lu turn on logics and %lu shut off logics\n", i, setOfSources[i].turnOnLogicSet.size(), setOfSources[i].shutOffLogicSet.size());
      sayMessage(string(outputString));
    }


    if (isHeating == true) {
      //check if anything that is on needs to turn off (generally for lowT cutoffs)
      //things that just turn on later this step are checked for this in shouldHeat
      if (setOfSources[i].isEngaged() && setOfSources[i].shutsOff(heatSourceAmbientT_C)) {
        setOfSources[i].disengageHeatSource();
        //check if the backup heat source would have to shut off too
        if (setOfSources[i].backupHeatSource != NULL && setOfSources[i].backupHeatSource->shutsOff(heatSourceAmbientT_C) != true) {
          //and if not, go ahead and turn it on 
          setOfSources[i].backupHeatSource->engageHeatSource(heatSourceAmbientT_C);
        }
      }

      //if there's a priority HeatSource (e.g. upper resistor) and it needs to 
      //come on, then turn everything off and start it up
      if (setOfSources[i].isVIP) {
        if (hpwhVerbosity >= VRB_emetic) sayMessage("\tVIP check");
        if (setOfSources[i].shouldHeat(heatSourceAmbientT_C)) {
          turnAllHeatSourcesOff();
          setOfSources[i].engageHeatSource(heatSourceAmbientT_C);
          //stop looking if the VIP needs to run
          break;
        }
      }
    }
    //if nothing is currently on, then check if something should come on
    else /* (isHeating == false) */ {
      if (setOfSources[i].shouldHeat(heatSourceAmbientT_C)) {
        setOfSources[i].engageHeatSource(heatSourceAmbientT_C);
        //engaging a heat source sets isHeating to true, so this will only trigger once
      }
    }




  }  //end loop over heat sources

  if (hpwhVerbosity >= VRB_emetic){
    sayMessage("after heat source choosing:  ");
    for (int i = 0; i < numHeatSources; i++) {
      sprintf(outputString, "heat source %d: %d \t", i , setOfSources[i].isEngaged());
      sayMessage(string(outputString));
    }
  sayMessage("\n");
  }

  //change the things according to DR schedule
  if (DRstatus == DR_BLOCK) {
    //force off
    turnAllHeatSourcesOff();
    isHeating = false;
  }
  else if (DRstatus == DR_ALLOW) {
    //do nothing
  }
  else if (DRstatus == DR_ENGAGE) {
    //if nothing else is on, force the first heat source on
    //this may or may not be desired behavior, pending more research (and funding)
    if (areAllHeatSourcesOff() == true) {
      setOfSources[0].engageHeatSource(heatSourceAmbientT_C);
    }
  }





  //do heating logic
  double minutesToRun = minutesPerStep;
  
  for (int i = 0; i < numHeatSources; i++) {
    //going through in order, check if the heat source is on
    if (setOfSources[i].isEngaged()) {
      //and add heat if it is
      setOfSources[i].addHeat(heatSourceAmbientT_C, minutesToRun);
      //if it finished early
      if (setOfSources[i].runtime_min < minutesToRun) {
        //debugging message handling
        if (hpwhVerbosity >= VRB_emetic){
          sprintf(outputString, "done heating! runtime_min minutesToRun %.2lf %.2lf\n", setOfSources[i].runtime_min, minutesToRun);
          sayMessage(string(outputString));
        }
        
        //subtract time it ran and turn it off
        minutesToRun -= setOfSources[i].runtime_min;
        setOfSources[i].disengageHeatSource();
        //and if there's another heat source in the list, that's able to come on,
        if (numHeatSources > i+1 && setOfSources[i + 1].shutsOff(heatSourceAmbientT_C) == false) {
          //turn it on
          setOfSources[i + 1].engageHeatSource(heatSourceAmbientT_C);
        }
      }
    }
  }

  if (areAllHeatSourcesOff() == true) {
    isHeating = false;
  }


  //track the depressed local temperature
  if (doTempDepression) {
    bool compressorRan = false;
    for (int i = 0; i < numHeatSources; i++) {
      if (setOfSources[i].isEngaged() && setOfSources[i].depressesTemperature) {
        compressorRan = true;
      }
    }
    
    if(compressorRan){
      temperatureGoal -= 4.5;		//hardcoded 4.5 degree total drop - from experimental data
    }
    else{
      //otherwise, do nothing, we're going back to ambient
    }

    // shrink the gap by the same percentage every minute - that gives us
    // exponential behavior the percentage was determined by a fit to
    // experimental data - 9.4 minute half life and 4.5 degree total drop
    //minus-equals is important, and fits with the order of locationTemperature
    //and temperatureGoal, so as to not use fabs() and conditional tests
    locationTemperature -= (locationTemperature - temperatureGoal)*(1 - 0.9289);
  }
  
  

  //settle outputs

  //outletTemp_C and standbyLosses_kWh are taken care of in updateTankTemps

  //sum energyRemovedFromEnvironment_kWh for each heat source;
  for (int i = 0; i < numHeatSources; i++) {
    energyRemovedFromEnvironment_kWh += (setOfSources[i].energyOutput_kWh - setOfSources[i].energyInput_kWh);
  }



  if (simHasFailed) {
    if (hpwhVerbosity >= VRB_reluctant) sayMessage("The simulation has encountered an error.  \n");
    return HPWH_ABORT;
  }


  if (hpwhVerbosity >= VRB_typical) sayMessage("Ending runOneStep.  \n\n\n\n");
  return 0;  //successful completion of the step returns 0
} //end runOneStep



int HPWH::runNSteps(int N,  double *inletT_C, double *drawVolume_L, 
                            double *tankAmbientT_C, double *heatSourceAmbientT_C,
                            DRMODES *DRstatus, double minutesPerStep) {
  //returns 0 on successful completion, HPWH_ABORT on failure

  //these are all the accumulating variables we'll need
  double energyRemovedFromEnvironment_kWh_SUM = 0;
  double standbyLosses_kWh_SUM = 0;
  double outletTemp_C_AVG = 0;
  double totalDrawVolume_L = 0;
  std::vector<double> heatSources_runTimes_SUM(numHeatSources);
  std::vector<double> heatSources_energyInputs_SUM(numHeatSources);
  std::vector<double> heatSources_energyOutputs_SUM(numHeatSources);
  static char outputString[MAXOUTSTRING];  //this is used for debugging outputs

  if (hpwhVerbosity >= VRB_typical) sayMessage("Begin runNSteps.  \n");
  //run the sim one step at a time, accumulating the outputs as you go
  for (int i = 0; i < N; i++) {
    runOneStep( inletT_C[i], drawVolume_L[i], tankAmbientT_C[i], heatSourceAmbientT_C[i],
                DRstatus[i], minutesPerStep );

    if (simHasFailed) {
      if (hpwhVerbosity >= VRB_reluctant) {
        sprintf(outputString, "RunNSteps has encountered an error on step %d of N and has ceased running.  \n", i+1);
        sayMessage(string(outputString));
      }
      return HPWH_ABORT;
    }

    energyRemovedFromEnvironment_kWh_SUM += energyRemovedFromEnvironment_kWh;
    standbyLosses_kWh_SUM += standbyLosses_kWh;

    outletTemp_C_AVG += outletTemp_C * drawVolume_L[i];
    totalDrawVolume_L += drawVolume_L[i];
    
    for (int j = 0; j < numHeatSources; j++) {
      heatSources_runTimes_SUM[j] += getNthHeatSourceRunTime(j);
      heatSources_energyInputs_SUM[j] += getNthHeatSourceEnergyInput(j);
      heatSources_energyOutputs_SUM[j] += getNthHeatSourceEnergyOutput(j);
    }

  }
  //finish weighted avg. of outlet temp by dividing by the total drawn volume
  outletTemp_C_AVG /= totalDrawVolume_L;

  //now, reassign all of the accumulated values to their original spots
  energyRemovedFromEnvironment_kWh = energyRemovedFromEnvironment_kWh_SUM;
  standbyLosses_kWh = standbyLosses_kWh_SUM;
  outletTemp_C = outletTemp_C_AVG;

  for (int i = 0; i < numHeatSources; i++) {
    setOfSources[i].runtime_min = heatSources_runTimes_SUM[i];
    setOfSources[i].energyInput_kWh = heatSources_energyInputs_SUM[i];
    setOfSources[i].energyOutput_kWh = heatSources_energyOutputs_SUM[i];
  }

  if (hpwhVerbosity >= VRB_typical) sayMessage("Ending runNSteps.  \n\n\n\n");
  return 0;
}



void HPWH::setVerbosity(VERBOSITY hpwhVrb){
  hpwhVerbosity = hpwhVrb;
}
void HPWH::setMessageCallback( void (*callbackFunc)(const string message, void* contextPtr), void* contextPtr){
  messageCallback = callbackFunc;
  messageCallbackContextPtr = contextPtr;
}
void HPWH::sayMessage(const string message) const{
  if (messageCallback != NULL) {
    (*messageCallback)(message, messageCallbackContextPtr);
  }
  else {
    std::cout << message;
  } 
}

void HPWH::printHeatSourceInfo(){
  std::stringstream ss;
  
  ss << std::left;
  ss << std::fixed;
  ss << std::setprecision(2);
  for (int i = 0; i < getNumHeatSources(); i++) {
    ss << "heat source " << i << ": " << isNthHeatSourceRunning(i) << "\t\t";   
  }
  ss << endl;

  for (int i = 0; i < getNumHeatSources(); i++) {
    ss << "input energy kwh: " << std::setw(7) << getNthHeatSourceEnergyInput(i) << "\t";   
  }
  ss << endl;

  for (int i = 0; i < getNumHeatSources(); i++) {
    ss << "input power kw: " << std::setw(7) << getNthHeatSourceEnergyInput(i) / (getNthHeatSourceRunTime(i)/60.0) << "\t\t";   
  }
  ss << endl;

  for (int i = 0; i < getNumHeatSources(); i++) {
    ss << "output energy kwh: " << std::setw(7) << getNthHeatSourceEnergyOutput(i) << "\t";   
  }
  ss << endl;
  
  for (int i = 0; i < getNumHeatSources(); i++) {
    ss << "output power kw: " << std::setw(7) << getNthHeatSourceEnergyOutput(i) / (getNthHeatSourceRunTime(i)/60.0) << "\t";   
  }
  ss << endl;
  
  for (int i = 0; i < getNumHeatSources(); i++) {
    ss << "run time min: " << std::setw(7) << getNthHeatSourceRunTime(i) << "\t\t";   
  }
  ss << endl << endl << endl;

  
  sayMessage( ss.str());
}


void HPWH::printTankTemps() {
  std::stringstream ss;

  ss << std::left;
  
  for (int i = 0; i < getNumNodes(); i++) {
    ss << std::setw(9) << getTankNodeTemp(i) << " ";
  }
  ss << endl;

  sayMessage( ss.str());
}




int HPWH::setSetpoint(double newSetpoint){
  setpoint_C = newSetpoint;
  return 0;
}
int HPWH::setSetpoint(double newSetpoint, UNITS units) {
  if (units == UNITS_C) {
    setpoint_C = newSetpoint;
  }
  else if (units == UNITS_F) {
    setpoint_C = F_TO_C(newSetpoint);
  }
  else {
    if(hpwhVerbosity >= VRB_reluctant) sayMessage("Incorrect unit specification for getNthSimTcouple.  \n");
    return HPWH_ABORT;
  }
  return 0;
}
int HPWH::resetTankToSetpoint(){
  for (int i = 0; i < numNodes; i++) {
    tankTemps_C[i] = setpoint_C;
  }
  return 0;
}

  
int HPWH::getNumNodes() const {
  return numNodes;
  }

double HPWH::getTankNodeTemp(int nodeNum) const {
  if (nodeNum > numNodes || nodeNum < 0) {
    if(hpwhVerbosity >= VRB_reluctant) sayMessage("You have attempted to access the temperature of a tank node that does not exist.  \n");
    return double(HPWH_ABORT);
  }
  return tankTemps_C[nodeNum];
}

double HPWH::getTankNodeTemp(int nodeNum,  UNITS units) const {
  double result = getTankNodeTemp(nodeNum);
  if (result == double(HPWH_ABORT)) {
    return result;
  }
  
  if (units == UNITS_C) {
    return result;
  }
  else if (units == UNITS_F) {
    return C_TO_F(result);
  }
  else {
    if(hpwhVerbosity >= VRB_reluctant) sayMessage("Incorrect unit specification for getTankNodeTemp.  \n");
    return double(HPWH_ABORT);
  }
}


double HPWH::getNthSimTcouple(int N) const {
  if (N > 6 || N < 1) {
    if(hpwhVerbosity >= VRB_reluctant) sayMessage("You have attempted to access a simulated thermocouple that does not exist.  \n");
    return double(HPWH_ABORT);
  }
  
  double averageTemp_C = 0;
  //specify N from 1-6, so use N-1 for node number
  for (int i = (N-1)*(numNodes/6); i < N*(numNodes/6); i++) {
    averageTemp_C += getTankNodeTemp(i);
  }
  averageTemp_C /= (numNodes/6);
  return averageTemp_C;
}

double HPWH::getNthSimTcouple(int N, UNITS units) const {
  double result = getNthSimTcouple(N);
  if (result == double(HPWH_ABORT)) {
    return result;
  }
  
  if (units == UNITS_C) {
    return result;
  }
  else if (units == UNITS_F) {
    return C_TO_F(result);
  }
  else {
    if(hpwhVerbosity >= VRB_reluctant) sayMessage("Incorrect unit specification for getNthSimTcouple.  \n");
    return double(HPWH_ABORT);
  }
}


int HPWH::getNumHeatSources() const {
  return numHeatSources;
}


double HPWH::getNthHeatSourceEnergyInput(int N) const {
  //energy used by the heat source is positive - this should always be positive
  if (N > numHeatSources || N < 0) {
    if(hpwhVerbosity >= VRB_reluctant) sayMessage("You have attempted to access the energy input of a heat source that does not exist.  \n");
    return double(HPWH_ABORT);
  }
  return setOfSources[N].energyInput_kWh;
}

double HPWH::getNthHeatSourceEnergyInput(int N, UNITS units) const {
  //energy used by the heat source is positive - this should always be positive
  double returnVal = getNthHeatSourceEnergyInput(N);
  if (returnVal == double(HPWH_ABORT) ) {
    return returnVal;
  }
  
  if (units == UNITS_KWH) {
    return returnVal;
  }
  else if (units == UNITS_BTU) {
    return KWH_TO_BTU(returnVal);
  }
  else if (units == UNITS_KJ) {
    return KWH_TO_KJ(returnVal);
  }
  else {
    if(hpwhVerbosity >= VRB_reluctant) sayMessage("Incorrect unit specification for getNthHeatSourceEnergyInput.  \n");
    return double(HPWH_ABORT);
  }
}


double HPWH::getNthHeatSourceEnergyOutput(int N) const {
//returns energy from the heat source into the water - this should always be positive
  if (N > numHeatSources || N < 0) {
    if(hpwhVerbosity >= VRB_reluctant) sayMessage("You have attempted to access the energy output of a heat source that does not exist.  \n");
    return double(HPWH_ABORT);
  }
  return setOfSources[N].energyOutput_kWh;
}

double HPWH::getNthHeatSourceEnergyOutput(int N, UNITS units) const {
//returns energy from the heat source into the water - this should always be positive
  double returnVal = getNthHeatSourceEnergyOutput(N);
  if (returnVal == double(HPWH_ABORT) ) {
    return returnVal;
  }
  
  if (units == UNITS_KWH) {
    return returnVal;
  }
  else if (units == UNITS_BTU) {
    return KWH_TO_BTU(returnVal);
  }
  else if (units == UNITS_KJ) {
    return KWH_TO_KJ(returnVal);
  }
  else {
    if(hpwhVerbosity >= VRB_reluctant) sayMessage("Incorrect unit specification for getNthHeatSourceEnergyInput.  \n");
    return double(HPWH_ABORT);
  }
}


double HPWH::getNthHeatSourceRunTime(int N) const {
  if (N > numHeatSources || N < 0) {
    if(hpwhVerbosity >= VRB_reluctant) sayMessage("You have attempted to access the run time of a heat source that does not exist.  \n");
    return double(HPWH_ABORT);
  }
  return setOfSources[N].runtime_min;
}	


int HPWH::isNthHeatSourceRunning(int N) const{
  if (N > numHeatSources || N < 0) {
    if(hpwhVerbosity >= VRB_reluctant) sayMessage("You have attempted to access the status of a heat source that does not exist.  \n");
    return HPWH_ABORT;
  }
  if ( setOfSources[N].isEngaged() ){
    return 1;
    }
  else{
    return 0;
  }
}


double HPWH::getOutletTemp() const {
    return outletTemp_C;
}

double HPWH::getOutletTemp(UNITS units) const {
  double returnVal = getOutletTemp();
  if (returnVal == double(HPWH_ABORT) ) {
    return returnVal;
  }
  
  if (units == UNITS_C) {
    return returnVal;
  }
  else if (units == UNITS_F) {
    return C_TO_F(returnVal);
  }
  else {
    if(hpwhVerbosity >= VRB_reluctant) sayMessage("Incorrect unit specification for getOutletTemp.  \n");
    return double(HPWH_ABORT);
  }
}


double HPWH::getEnergyRemovedFromEnvironment() const {
  //moving heat from the space to the water is the positive direction
  return energyRemovedFromEnvironment_kWh;
}

double HPWH::getEnergyRemovedFromEnvironment(UNITS units) const {
  //moving heat from the space to the water is the positive direction
  double returnVal = getEnergyRemovedFromEnvironment();

  if (units == UNITS_KWH) {
    return returnVal;
  }
  else if (units == UNITS_BTU) {
    return KWH_TO_BTU(returnVal);
  }
  else if (units == UNITS_KJ){
    return KWH_TO_KJ(returnVal);
  }
  else {
    if(hpwhVerbosity >= VRB_reluctant) sayMessage("Incorrect unit specification for getEnergyRemovedFromEnvironment.  \n");
    return double(HPWH_ABORT);
  }
}


double HPWH::getStandbyLosses() const {
  //moving heat from the water to the space is the positive direction
  return standbyLosses_kWh;
}

double HPWH::getStandbyLosses(UNITS units) const {
  //moving heat from the water to the space is the positive direction
  double returnVal = getStandbyLosses();

 if (units == UNITS_KWH) {
    return returnVal;
  }
  else if (units == UNITS_BTU) {
    return KWH_TO_BTU(returnVal);
  }
  else if (units == UNITS_KJ){
    return KWH_TO_KJ(returnVal);
  }
  else {
    if(hpwhVerbosity >= VRB_reluctant) sayMessage("Incorrect unit specification for getStandbyLosses.  \n");
    return double(HPWH_ABORT);
  }
}



//the privates
void HPWH::updateTankTemps(double drawVolume_L, double inletT_C, double tankAmbientT_C, double minutesPerStep) {
  //set up some useful variables for calculations
  double volPerNode_LperNode = tankVolume_L/numNodes;
  double drawFraction;
  int wholeNodesToDraw;
  this->outletTemp_C = 0;

  //calculate how many nodes to draw (wholeNodesToDraw), and the remainder (drawFraction)
  drawFraction = drawVolume_L/volPerNode_LperNode;
  wholeNodesToDraw = (int)std::floor(drawFraction);
  drawFraction -= wholeNodesToDraw;

  //move whole nodes
  if (wholeNodesToDraw > 0) {        
    for (int i = 0; i < wholeNodesToDraw; i++) {
      //add temperature of drawn nodes for outletT average
      outletTemp_C += tankTemps_C[numNodes-1 - i];  
    }

    for (int i = numNodes-1; i >= 0; i--) {
      if (i > wholeNodesToDraw-1) {
        //move nodes up
        tankTemps_C[i] = tankTemps_C[i - wholeNodesToDraw];
      }
      else {
        //fill in bottom nodes with inlet water
        tankTemps_C[i] = inletT_C;  
      }
    }
  }
  //move fractional node
  if (drawFraction > 0) {
    //add temperature for outletT average
    outletTemp_C += drawFraction*tankTemps_C[numNodes - 1];
    //move partial nodes up
    for (int i = numNodes-1; i > 0; i--) {
      tankTemps_C[i] = tankTemps_C[i] *(1.0 - drawFraction) + tankTemps_C[i-1] * drawFraction;
    }
    //fill in bottom partial node with inletT
    tankTemps_C[0] = tankTemps_C[0] * (1.0 - drawFraction) + inletT_C*drawFraction;
  }

  //fill in average outlet T
  this->outletTemp_C /= (wholeNodesToDraw + drawFraction);


  //Account for mixing at the bottom of the tank
  if (tankMixesOnDraw == true && drawVolume_L > 0) {
    int mixedBelowNode = numNodes / 3;
    double ave = 0;
    
    for (int i = 0; i < mixedBelowNode; i++) {
      ave += tankTemps_C[i];
    }
    ave /= mixedBelowNode;
    
    for (int i = 0; i < mixedBelowNode; i++) {
      tankTemps_C[i] += ((ave - tankTemps_C[i]) / 3.0);
    }
  }

  //calculate standby losses
  //get average tank temperature
  double avgTemp = 0;
  for (int i = 0; i < numNodes; i++) avgTemp += tankTemps_C[i];
  avgTemp /= numNodes;

  //kJ's lost as standby in the current time step
  double standbyLosses_kJ = (tankUA_kJperHrC * (avgTemp - tankAmbientT_C) * (minutesPerStep / 60.0));  
  standbyLosses_kWh = KJ_TO_KWH(standbyLosses_kJ);

  //The effect of standby loss on temperature in each segment
  double lossPerNode_C = (standbyLosses_kJ / numNodes)    /    ((volPerNode_LperNode * DENSITYWATER_kgperL) * CPWATER_kJperkgC);
  for (int i = 0; i < numNodes; i++) tankTemps_C[i] -= lossPerNode_C;
}  //end updateTankTemps


void HPWH::turnAllHeatSourcesOff() {
  for (int i = 0; i < numHeatSources; i++) {
    setOfSources[i].disengageHeatSource();
  }
  isHeating = false;
}


bool HPWH::areAllHeatSourcesOff() const {
  bool allOff = true;
  for (int i = 0; i < numHeatSources; i++) {
    if (setOfSources[i].isEngaged() == true) {
      allOff = false;
    }
  }
  return allOff;
}


double HPWH::topThirdAvg_C() const {
  double sum = 0;
  int num = 0;
  
  for (int i = 2*(numNodes/3); i < numNodes; i++) {
    sum += tankTemps_C[i];
    num++;
  }
  
  return sum/num;
}


double HPWH::bottomThirdAvg_C() const {
  double sum = 0;
  int num = 0;
  
  for (int i = 0; i < numNodes/3; i++) {
    sum += tankTemps_C[i];
    num++;
  }
  
  return sum/num;
}


double HPWH::bottomTwelthAvg_C() const {
  double sum = 0;
  int num = 0;
  
  for (int i = 0; i < numNodes/12; i++) {
    sum += tankTemps_C[i];
    num++;
  }
  
  return sum/num;
}






//these are the HeatSource functions
//the public functions
HPWH::HeatSource::HeatSource(HPWH *parentInput)
  :hpwh(parentInput), isOn(false), backupHeatSource(NULL), companionHeatSource(NULL),
                  hysteresis_dC(0) {}


void HPWH::HeatSource::setCondensity(double cnd1, double cnd2, double cnd3, double cnd4, 
                  double cnd5, double cnd6, double cnd7, double cnd8, 
                  double cnd9, double cnd10, double cnd11, double cnd12) {
  condensity[0] = cnd1;  
  condensity[1] = cnd2;  
  condensity[2] = cnd3;  
  condensity[3] = cnd4;  
  condensity[4] = cnd5;  
  condensity[5] = cnd6;  
  condensity[6] = cnd7;  
  condensity[7] = cnd8;  
  condensity[8] = cnd9;  
  condensity[9] = cnd10;  
  condensity[10] = cnd11;  
  condensity[11] = cnd12;  
}
                  

bool HPWH::HeatSource::isEngaged() const {
  return isOn;
}


void HPWH::HeatSource::engageHeatSource(double heatSourceAmbientT_C) {
  isOn = true;
  hpwh->isHeating = true;
  if (companionHeatSource != NULL &&
        companionHeatSource->shutsOff(heatSourceAmbientT_C) != true &&
        companionHeatSource->isEngaged() == false) {
    companionHeatSource->engageHeatSource(heatSourceAmbientT_C);
  }
  
}              

                  
void HPWH::HeatSource::disengageHeatSource() {
  isOn = false;
}              

                  
bool HPWH::HeatSource::shouldHeat(double heatSourceAmbientT_C) const {
  //return true if the heat source logic tells it to come on, false if it doesn't,
  //or if an unsepcified selector was used
  bool shouldEngage = false;
  static char outputString[MAXOUTSTRING];  //this is used for debugging outputs

  for (int i = 0; i < (int)turnOnLogicSet.size(); i++) {
    if (hpwh->hpwhVerbosity >= VRB_emetic){
      sprintf(outputString, "\tshouldHeat logic #%d ", turnOnLogicSet[i].selector);
      hpwh->sayMessage(string(outputString));
    }
    switch (turnOnLogicSet[i].selector) {
      case ONLOGIC_topThird:
        //when the top third is too cold - typically used for upper resistance/VIP heat sources
        if (hpwh->topThirdAvg_C() < hpwh->setpoint_C - turnOnLogicSet[i].decisionPoint) {
          shouldEngage = true;

          //debugging message handling
          if (hpwh->hpwhVerbosity >= VRB_typical) hpwh->sayMessage("engages!\n");
        }
        break;
      
      case ONLOGIC_bottomThird:
        //when the bottom third is too cold - typically used for compressors
        if (hpwh->bottomThirdAvg_C() < hpwh->setpoint_C - turnOnLogicSet[i].decisionPoint) {
          shouldEngage = true;

          //debugging message handling
          if (hpwh->hpwhVerbosity >= VRB_typical) hpwh->sayMessage("engages!\n");
          if (hpwh->hpwhVerbosity >= VRB_emetic){
            sprintf(outputString, "bottom third: %.2lf \t setpoint: %.2lf \t decisionPoint: %.2lf \n", hpwh->bottomThirdAvg_C(), hpwh->setpoint_C, turnOnLogicSet[i].decisionPoint);
            hpwh->sayMessage(string(outputString));
          }
        }    
        break;
        
      case ONLOGIC_standby:
        //when the top node is too cold - typically used for standby heating
        if (hpwh->tankTemps_C[hpwh->numNodes - 1] < hpwh->setpoint_C - turnOnLogicSet[i].decisionPoint) {
          shouldEngage = true;

          //debugging message handling
          if (hpwh->hpwhVerbosity >= VRB_typical) hpwh->sayMessage("engages!\n");
          if (hpwh->hpwhVerbosity >= VRB_emetic){
            sprintf(outputString, "tanktoptemp: %.2lf \t setpoint: %.2lf \t decisionPoint: %.2lf \n ", hpwh->tankTemps_C[hpwh->numNodes - 1], hpwh->setpoint_C, turnOnLogicSet[i].decisionPoint);
            hpwh->sayMessage(string(outputString));
          }
        }
        break;
        
      default:
        hpwh->simHasFailed = true;
        
        //debugging message handling
        if (hpwh->hpwhVerbosity >= VRB_reluctant) {
          sprintf(outputString, "You have input an incorrect turnOn logic choice specifier: %d \n", turnOnLogicSet[i].selector);
          hpwh->sayMessage(string(outputString));
        }
        break;
    }

    //quit searching the logics if one of them turns it on
    if (shouldEngage) break; 

    if (hpwh->hpwhVerbosity >= VRB_emetic){
      sprintf(outputString, "returns: %d \t", shouldEngage);
      hpwh->sayMessage(string(outputString));
    }
  }  //end loop over set of logic conditions


  //if everything else wants it to come on, but if it would shut off anyways don't turn it on
  if (shouldEngage == true && shutsOff(heatSourceAmbientT_C) == true ) {
    shouldEngage = false;
    if (hpwh->hpwhVerbosity >= VRB_typical) hpwh->sayMessage("but is denied by shutsOff");
  }

  if (hpwh->hpwhVerbosity >= VRB_typical) hpwh->sayMessage("\n");
  return shouldEngage;
}


bool HPWH::HeatSource::shutsOff(double heatSourceAmbientT_C) const {
  bool shutOff = false;
  static char outputString[MAXOUTSTRING];  //this is used for debugging outputs


  for (int i = 0; i < (int)shutOffLogicSet.size(); i++) {
    if (hpwh->hpwhVerbosity >= VRB_emetic){
      sprintf(outputString, "\tshutsOff logic #%d: ", shutOffLogicSet[i].selector);
      hpwh->sayMessage(string(outputString));
    }
    switch (shutOffLogicSet[i].selector) {
      case OFFLOGIC_lowT:
        //when the "external" temperature is too cold - typically used for compressor low temp. cutoffs
        //when running, use hysteresis
        if (isEngaged() == true && heatSourceAmbientT_C < shutOffLogicSet[i].decisionPoint - hysteresis_dC) {
          shutOff = true;
          if (hpwh->hpwhVerbosity >= VRB_typical) hpwh->sayMessage("shut down running lowT\t");
        }
        //when not running, don't use hysteresis
        else if (isEngaged() == false && heatSourceAmbientT_C < shutOffLogicSet[i].decisionPoint) {
          shutOff = true;
          if (hpwh->hpwhVerbosity >= VRB_typical) hpwh->sayMessage("shut down lowT\t");
        }
        
        break;

      case OFFLOGIC_lowTreheat:
        //don't run if the temperature is too warm
        if (isEngaged() == true && heatSourceAmbientT_C > shutOffLogicSet[i].decisionPoint + hysteresis_dC) {
          shutOff = true;
          if (hpwh->hpwhVerbosity >= VRB_typical) hpwh->sayMessage("shut down lowTreheat\t");
        }
        //there is no option for isEngaged() == false because this logic choice
        //is meant for shutting off the resistance element when it has heated
        //enough - not for preventing it from coming on at other times
        break;
      
      case OFFLOGIC_bottomNodeMaxTemp:
        //don't run if the bottom node is too hot - typically for "external" configuration
        if (hpwh->tankTemps_C[0] > shutOffLogicSet[i].decisionPoint) {
          shutOff = true;
          if (hpwh->hpwhVerbosity >= VRB_typical) hpwh->sayMessage("shut down bottom node temp\t");
        }
        break;

      case OFFLOGIC_bottomTwelthMaxTemp:
        //don't run if the bottom twelth of the tank is too hot
        //typically for "external" configuration
        if (hpwh->bottomTwelthAvg_C() > shutOffLogicSet[i].decisionPoint) {
          shutOff = true;
          if (hpwh->hpwhVerbosity >= VRB_typical) hpwh->sayMessage("shut down bottom twelth temp\t");
        }
        break;

      case OFFLOGIC_largeDraw:
        //don't run if the bottom third of the tank is too cold
        //typically for GE overreliance on resistance element
        if (hpwh->bottomThirdAvg_C() < shutOffLogicSet[i].decisionPoint) {
          shutOff = true;
          if (hpwh->hpwhVerbosity >= VRB_typical) hpwh->sayMessage("shut down bottom third temp large draw\t");
        }
        break;
      
      default:
        hpwh->simHasFailed = true;
        if (hpwh->hpwhVerbosity >= VRB_reluctant) hpwh->sayMessage("You have input an incorrect shutOff logic choice specifier.  \n");
        break;
    }
  }

  if (hpwh->hpwhVerbosity >= VRB_emetic){
    sprintf(outputString, "returns: %d \n", shutOff);
    hpwh->sayMessage(string(outputString));
  }
  return shutOff;
}


void HPWH::HeatSource::addHeat(double externalT_C, double minutesToRun) {
  double input_BTUperHr, cap_BTUperHr, cop, captmp_kJ, leftoverCap_kJ;
  static char outputString[MAXOUTSTRING];  //this is used for debugging outputs

  // Reset the runtime of the Heat Source
  this->runtime_min = 0.0;
  leftoverCap_kJ = 0.0;

  switch(configuration){
    case CONFIG_SUBMERGED:
    case CONFIG_WRAPPED:
	{
      static std::vector<double> heatDistribution(hpwh->numNodes);
      //clear the heatDistribution vector, since it's static it is still holding the
      //distribution from the last go around
      heatDistribution.clear();
      //calcHeatDist takes care of the swooping for wrapped configurations
      calcHeatDist(heatDistribution);

      // calculate capacity btu/hr, input btu/hr, and cop
      getCapacity(externalT_C, getCondenserTemp(), input_BTUperHr, cap_BTUperHr, cop);

      //some outputs for debugging
      if (hpwh->hpwhVerbosity >= VRB_typical){
        sprintf(outputString, "capacity_kWh %.2lf \t\t cap_BTUperHr %.2lf \n", BTU_TO_KWH(cap_BTUperHr)*(minutesToRun)/60.0, cap_BTUperHr);
        hpwh->sayMessage(string(outputString));
      }
      if (hpwh->hpwhVerbosity >= VRB_emetic){
        sprintf(outputString, "heatDistribution: %4.3lf %4.3lf %4.3lf %4.3lf %4.3lf %4.3lf %4.3lf %4.3lf %4.3lf %4.3lf %4.3lf %4.3lf \n", heatDistribution[0], heatDistribution[1], heatDistribution[2], heatDistribution[3], heatDistribution[4], heatDistribution[5], heatDistribution[6], heatDistribution[7], heatDistribution[8], heatDistribution[9], heatDistribution[10], heatDistribution[11]);
        hpwh->sayMessage(string(outputString));
      }
      //the loop over nodes here is intentional - essentially each node that has
      //some amount of heatDistribution acts as a separate resistive element
  //maybe start from the top and go down?  test this with graphs
      for(int i = hpwh->numNodes -1; i >= 0; i--){
      //for(int i = 0; i < hpwh->numNodes; i++){
        captmp_kJ = BTU_TO_KJ(cap_BTUperHr * minutesToRun / 60.0 * heatDistribution[i]);
        if(captmp_kJ != 0){
          //add leftoverCap to the next run, and keep passing it on
          leftoverCap_kJ = addHeatAboveNode(captmp_kJ + leftoverCap_kJ, i, minutesToRun);
        }
      }

      //after you've done everything, any leftover capacity is time that didn't run
      this->runtime_min = (1.0 - (leftoverCap_kJ / BTU_TO_KJ(cap_BTUperHr * minutesToRun / 60.0))) * minutesToRun;
	}
      break;
    
    case CONFIG_EXTERNAL:
      //Else the heat source is external. Sanden system is only current example
      //capacity is calculated internal to this function, and cap/input_BTUperHr, cop are outputs
      this->runtime_min = addHeatExternal(externalT_C, minutesToRun, cap_BTUperHr, input_BTUperHr, cop);
      break;
      
    default:
      hpwh->simHasFailed = true;
      if (hpwh->hpwhVerbosity >= VRB_reluctant) {
        sprintf(outputString, "Invalid heat source configuration chosen: %d \n", configuration);
        hpwh->sayMessage(string(outputString));
      }
      break;
  }
  
  // Write the input & output energy
  energyInput_kWh = BTU_TO_KWH(input_BTUperHr * runtime_min / 60.0);
  energyOutput_kWh = BTU_TO_KWH(cap_BTUperHr * runtime_min / 60.0);
}



//the private functions
double HPWH::HeatSource::expitFunc(double x, double offset) {
  double val;
  val = 1 / (1 + exp(x - offset));
  return val;
}


void HPWH::HeatSource::normalize(std::vector<double> &distribution) {
  double sum_tmp = 0.0;

  for(unsigned int i = 0; i < distribution.size(); i++) {
    sum_tmp += distribution[i];
  }
  for(unsigned int i = 0; i < distribution.size(); i++) {
    distribution[i] /= sum_tmp;
    //this gives a very slight speed improvement (milliseconds per simulated year)
    if (distribution[i] < HEATDIST_MINVALUE) distribution[i] = 0;
  }
}


double HPWH::HeatSource::getCondenserTemp() {
  double condenserTemp_C = 0.0;
  static char outputString[MAXOUTSTRING];  //this is used for debugging outputs
  int tempNodesPerCondensityNode = hpwh->numNodes / CONDENSITY_SIZE;
  int j = 0;
  
  for(int i = 0; i < hpwh->numNodes; i++) {
    j = i / tempNodesPerCondensityNode;
    if (condensity[j] != 0) {
      condenserTemp_C += (condensity[j] / tempNodesPerCondensityNode) * hpwh->tankTemps_C[i];
      //the weights don't need to be added to divide out later because they should always sum to 1

      if (hpwh->hpwhVerbosity >= VRB_emetic){
        sprintf(outputString, "condenserTemp_C:\t %.2lf \ti:\t %d \tj\t %d \tcondensity[j]:\t %.2lf \ttankTemps_C[i]:\t %.2lf\n", condenserTemp_C, i, j, condensity[j], hpwh->tankTemps_C[i]);
        hpwh->sayMessage(string(outputString));
      }
    }
  }
  if (hpwh->hpwhVerbosity >= VRB_typical){
    sprintf(outputString, "condenser temp %.2lf \n", condenserTemp_C);
    hpwh->sayMessage(string(outputString));
  }
  return condenserTemp_C;
}


void HPWH::HeatSource::getCapacity(double externalT_C, double condenserTemp_C, double &input_BTUperHr, double &cap_BTUperHr, double &cop) {
  double COP_T1, COP_T2;    			   //cop at ambient temperatures T1 and T2
  double inputPower_T1_Watts, inputPower_T2_Watts; //input power at ambient temperatures T1 and T2	
  double externalT_F, condenserTemp_F;
  static char outputString[MAXOUTSTRING];  //this is used for debugging outputs

  // Convert Celsius to Fahrenheit for the curve fits
  condenserTemp_F = C_TO_F(condenserTemp_C);
  externalT_F = C_TO_F(externalT_C);

  // Calculate COP and Input Power at each of the two reference temepratures
  COP_T1 = COP_T1_constant;
  COP_T1 += COP_T1_linear * condenserTemp_F ;
  COP_T1 += COP_T1_quadratic * condenserTemp_F * condenserTemp_F;

  COP_T2 = COP_T2_constant;
  COP_T2 += COP_T2_linear * condenserTemp_F;
  COP_T2 += COP_T2_quadratic * condenserTemp_F * condenserTemp_F;

  inputPower_T1_Watts = inputPower_T1_constant_W;
  inputPower_T1_Watts += inputPower_T1_linear_WperF * condenserTemp_F;
  inputPower_T1_Watts += inputPower_T1_quadratic_WperF2 * condenserTemp_F * condenserTemp_F;

  if (hpwh->hpwhVerbosity >= VRB_emetic){
    sprintf(outputString, "inputPower_T1_constant_W   linear_WperF   quadratic_WperF2  \t%.2lf  %.2lf  %.2lf \n", inputPower_T1_constant_W, inputPower_T1_linear_WperF, inputPower_T1_quadratic_WperF2);
    hpwh->sayMessage(string(outputString));
  }
  
  inputPower_T2_Watts = inputPower_T2_constant_W;
  inputPower_T2_Watts += inputPower_T2_linear_WperF * condenserTemp_F;
  inputPower_T2_Watts += inputPower_T2_quadratic_WperF2 * condenserTemp_F * condenserTemp_F;

  if (hpwh->hpwhVerbosity >= VRB_emetic){
    sprintf(outputString, "inputPower_T2_constant_W   linear_WperF   quadratic_WperF2  \t%.2lf  %.2lf  %.2lf \n", inputPower_T2_constant_W, inputPower_T2_linear_WperF, inputPower_T2_quadratic_WperF2);
    hpwh->sayMessage(string(outputString));
    sprintf(outputString, "inputPower_T1_Watts:  %.2lf \tinputPower_T2_Watts:  %.2lf \n", inputPower_T1_Watts, inputPower_T2_Watts);
    hpwh->sayMessage(string(outputString));
  }
  // Interpolate to get COP and input power at the current ambient temperature
  cop = COP_T1 + (externalT_F - T1_F) * ((COP_T2 - COP_T1) / (T2_F - T1_F));
  input_BTUperHr = KWH_TO_BTU(  (inputPower_T1_Watts + (externalT_F - T1_F) *
                                  ( (inputPower_T2_Watts - inputPower_T1_Watts)
                                            / (T2_F - T1_F) )
                                  ) / 1000.0);  //1000 converts w to kw
  cap_BTUperHr = cop * input_BTUperHr;

  if (hpwh->hpwhVerbosity >= VRB_typical){
    sprintf(outputString, "cop: %.2lf \tinput_BTUperHr: %.2lf \tcap_BTUperHr: %.2lf \n", cop, input_BTUperHr, cap_BTUperHr);
    hpwh->sayMessage(string(outputString));
  }
/*
 *this is unimplemented as yet - possibly it may remain so
 * 
  //here is where the scaling for flow restriction goes
  //the input power doesn't change, we just scale the cop by a small percentage
  //that is based on the ducted flow rate the equation is a fit to three points,
  //measured experimentally - 12 percent reduction at 150 cfm, 10 percent at
  //200, and 0 at 375 it's slightly adjust to be equal to 1 at 375
  if(hpwh->ductingType != 0){
    cop_interpolated *= 0.00056*hpwh->fanFlow + 0.79;
  }
*/
}


void HPWH::HeatSource::calcHeatDist(std::vector<double> &heatDistribution) {
  double offset = 5.0 / 1.8;
  double temp = 0;  //temp for temporary not temperature
  int k;

  // Populate the vector of heat distribution
  for(int i = 0; i < hpwh->numNodes; i++) {
    if(i < lowestNode) {
      heatDistribution.push_back(0);
    }
    else {
      if(configuration == CONFIG_SUBMERGED) { // Inside the tank, no swoopiness required
        //intentional integer division
        k = i / int(hpwh->numNodes / CONDENSITY_SIZE);
        heatDistribution.push_back( condensity[k] );
      }
      else if(configuration == CONFIG_WRAPPED) { // Wrapped around the tank, send through the logistic function
        temp = expitFunc( (hpwh->tankTemps_C[i] - hpwh->tankTemps_C[lowestNode]) / this->shrinkage , offset);
        temp *= (hpwh->setpoint_C - hpwh->tankTemps_C[i]);
        heatDistribution.push_back( temp );
      }
    }
  }
  normalize(heatDistribution);

}


double HPWH::HeatSource::addHeatAboveNode(double cap_kJ, int node, double minutesToRun) {
  double Q_kJ, deltaT_C, targetTemp_C;
  int setPointNodeNum;
  static char outputString[MAXOUTSTRING];  //this is used for debugging outputs
  
  double volumePerNode_L = hpwh->tankVolume_L / hpwh->numNodes;

  if (hpwh->hpwhVerbosity >= VRB_emetic){
    sprintf(outputString, "node %2d   cap_kwh %.4lf \n", node, KJ_TO_KWH(cap_kJ));
    hpwh->sayMessage(string(outputString));
  }
  
  // find the first node (from the bottom) that does not have the same temperature as the one above it
  // if they all have the same temp., use the top node, hpwh->numNodes-1
  setPointNodeNum = node;
  for(int i = node; i < hpwh->numNodes-1; i++){
    if(hpwh->tankTemps_C[i] != hpwh->tankTemps_C[i+1]) {
      break;
    }
    else{
      setPointNodeNum = i+1;
    }
  }

  // maximum heat deliverable in this timestep
  while(cap_kJ > 0 && setPointNodeNum < hpwh->numNodes) {
    // if the whole tank is at the same temp, the target temp is the setpoint
    if(setPointNodeNum == (hpwh->numNodes-1)) {
      targetTemp_C = hpwh->setpoint_C;
    }
    //otherwise the target temp is the first non-equal-temp node
    else {
      targetTemp_C = hpwh->tankTemps_C[setPointNodeNum+1];
    }

    deltaT_C = targetTemp_C - hpwh->tankTemps_C[setPointNodeNum];
    
    //heat needed to bring all equal temp. nodes up to the temp of the next node. kJ
    Q_kJ = CPWATER_kJperkgC * volumePerNode_L * DENSITYWATER_kgperL * (setPointNodeNum+1 - node) * deltaT_C;

    //Running the rest of the time won't recover
    if(Q_kJ > cap_kJ){
      for(int j = node; j <= setPointNodeNum; j++) {
        hpwh->tankTemps_C[j] += cap_kJ / CPWATER_kJperkgC / volumePerNode_L / DENSITYWATER_kgperL / (setPointNodeNum + 1 - node);
      }
      cap_kJ = 0;
    }
    //temp will recover by/before end of timestep
    else{
      for(int j = node; j <= setPointNodeNum; j++){
        hpwh->tankTemps_C[j] = targetTemp_C;
      }
      setPointNodeNum++;
      cap_kJ -= Q_kJ;
    }
  }

  //return the unused capacity
  return cap_kJ;
}


double HPWH::HeatSource::addHeatExternal(double externalT_C, double minutesToRun, double &cap_BTUperHr,  double &input_BTUperHr, double &cop) {
  double heatingCapacity_kJ, deltaT_C, timeUsed_min, nodeHeat_kJperNode, nodeFrac;
  double inputTemp_BTUperHr = 0, capTemp_BTUperHr = 0, copTemp = 0;
  double volumePerNode_LperNode = hpwh->tankVolume_L / hpwh->numNodes;
  double timeRemaining_min = minutesToRun;
  static char outputString[MAXOUTSTRING];  //this is used for debugging outputs

  input_BTUperHr = 0;
  cap_BTUperHr   = 0;
  cop            = 0;

  do{
    if (hpwh->hpwhVerbosity >= VRB_emetic){
      sprintf(outputString, "bottom tank temp: %.2lf", hpwh->tankTemps_C[0]);
      hpwh->sayMessage(string(outputString));
    }
    
    //how much heat is available this timestep
    getCapacity(externalT_C, hpwh->tankTemps_C[0], inputTemp_BTUperHr, capTemp_BTUperHr, copTemp);
    heatingCapacity_kJ = BTU_TO_KJ(capTemp_BTUperHr * (minutesToRun / 60.0));
    if (hpwh->hpwhVerbosity >= VRB_emetic){
      sprintf(outputString, "\theatingCapacity_kJ stepwise: %.2lf \n", heatingCapacity_kJ);
      hpwh->sayMessage(string(outputString));
    }
  
    //adjust capacity for how much time is left in this step
    heatingCapacity_kJ = heatingCapacity_kJ * (timeRemaining_min / minutesToRun);
    if (hpwh->hpwhVerbosity >= VRB_emetic){
      sprintf(outputString, "\theatingCapacity_kJ remaining this node: %.2lf \n", heatingCapacity_kJ);
      hpwh->sayMessage(string(outputString));
    }
    
    //calculate what percentage of the bottom node can be heated to setpoint
    //with amount of heat available this timestep
    deltaT_C = hpwh->setpoint_C - hpwh->tankTemps_C[0];
    nodeHeat_kJperNode = volumePerNode_LperNode * DENSITYWATER_kgperL * CPWATER_kJperkgC * deltaT_C;
    nodeFrac = heatingCapacity_kJ / nodeHeat_kJperNode;
    if (hpwh->hpwhVerbosity >= VRB_emetic){
      sprintf(outputString, "nodeHeat_kJperNode: %.2lf nodeFrac: %.2lf \n\n", nodeHeat_kJperNode, nodeFrac);
      hpwh->sayMessage(string(outputString));
    }
    //if more than one, round down to 1 and subtract the amount of time it would
    //take to heat that node from the timeRemaining
    if(nodeFrac > 1){
      nodeFrac = 1;
      timeUsed_min = (nodeHeat_kJperNode / heatingCapacity_kJ)*timeRemaining_min;
      timeRemaining_min -= timeUsed_min;
    }
    //otherwise just the fraction available 
    //this should make heatingCapacity == 0  if nodeFrac < 1
    else{
      timeUsed_min = timeRemaining_min;
      timeRemaining_min = 0;
    }

    //move all nodes down, mixing if less than a full node
    for(int n = 0; n < hpwh->numNodes - 1; n++) {
      hpwh->tankTemps_C[n] = hpwh->tankTemps_C[n] * (1 - nodeFrac) + hpwh->tankTemps_C[n + 1] * nodeFrac;
    }
    //add water to top node, heated to setpoint
    hpwh->tankTemps_C[hpwh->numNodes - 1] = hpwh->tankTemps_C[hpwh->numNodes - 1] * (1 - nodeFrac) + hpwh->setpoint_C * nodeFrac;
    

    //track outputs - weight by the time ran
    input_BTUperHr  += inputTemp_BTUperHr*timeUsed_min;
    cap_BTUperHr    += capTemp_BTUperHr*timeUsed_min;
    cop             += copTemp*timeUsed_min;

  
  //if there's still time remaining and you haven't heated to the cutoff
  //specified in shutsOff logic, keep heating
  } while(timeRemaining_min > 0 && shutsOff(externalT_C) != true);

  //divide outputs by sum of weight - the total time ran
  input_BTUperHr  /= (minutesToRun - timeRemaining_min);
  cap_BTUperHr    /= (minutesToRun - timeRemaining_min);
  cop             /= (minutesToRun - timeRemaining_min);

  if (hpwh->hpwhVerbosity >= VRB_emetic){  	
    sprintf(outputString, "final remaining time: %.2lf \n", timeRemaining_min);
    hpwh->sayMessage(string(outputString));
  }  
  //return the time left
  return minutesToRun - timeRemaining_min;
}






void HPWH::HeatSource::setupAsResistiveElement(int node, double Watts) {
    int i;

    isOn = false;
    isVIP = false;
    for(i = 0; i < CONDENSITY_SIZE; i++) {
      condensity[i] = 0;
    }
    condensity[node] = 1;
    T1_F = 50;
    T2_F = 67;
    inputPower_T1_constant_W = Watts;
    inputPower_T1_linear_WperF = 0;
    inputPower_T1_quadratic_WperF2 = 0;
    inputPower_T2_constant_W = Watts;
    inputPower_T2_linear_WperF = 0;
    inputPower_T2_quadratic_WperF2 = 0;
    COP_T1_constant = 1;
    COP_T1_linear = 0;
    COP_T1_quadratic = 0;
    COP_T2_constant = 1;
    COP_T2_linear = 0;
    COP_T2_quadratic = 0;
    configuration = CONFIG_SUBMERGED; //immersed in tank

    depressesTemperature = false;  //no temp depression
}


void HPWH::HeatSource::addTurnOnLogic(ONLOGIC selector, double decisionPoint){
  this->turnOnLogicSet.push_back(HeatSource::heatingLogicPair<HeatSource::ONLOGIC>(selector, decisionPoint));
}
void HPWH::HeatSource::addShutOffLogic(OFFLOGIC selector, double decisionPoint){
  this->shutOffLogicSet.push_back(HeatSource::heatingLogicPair<HeatSource::OFFLOGIC>(selector, decisionPoint));
}

int HPWH::HPWHinit_presets(MODELS presetNum) {
  //return 0 on success, HPWH_ABORT for failure
  static char outputString[MAXOUTSTRING];  //this is used for debugging outputs

  //resistive with no UA losses for testing
  if (presetNum == MODELS_restankNoUA) {
    numNodes = 12;
    tankTemps_C = new double[numNodes];
    setpoint_C = 50;

    //start tank off at setpoint
    resetTankToSetpoint();
    
    tankVolume_L = 12; 
    tankUA_kJperHrC = 0; //0 to turn off
    
    doTempDepression = false;
    tankMixesOnDraw = false;

    numHeatSources = 2;
    setOfSources = new HeatSource[numHeatSources];

    //set up a resistive element at the bottom, 4500 kW
    HeatSource resistiveElementBottom(this);
    HeatSource resistiveElementTop(this);
    
    resistiveElementBottom.setupAsResistiveElement(0, 4500);
    resistiveElementTop.setupAsResistiveElement(9, 4500);

    //standard logic conditions
    resistiveElementBottom.addTurnOnLogic(HeatSource::ONLOGIC_bottomThird, 20);
    resistiveElementBottom.addTurnOnLogic(HeatSource::ONLOGIC_standby, 15);
    resistiveElementBottom.addShutOffLogic(HeatSource::OFFLOGIC_lowT, 3);

    resistiveElementTop.addTurnOnLogic(HeatSource::ONLOGIC_topThird, 20);
    resistiveElementTop.isVIP = true;
    
    //assign heat sources into array in order of priority
    setOfSources[0] = resistiveElementTop;
    setOfSources[1] = resistiveElementBottom;
  }

  //resistive tank with massive UA loss for testing
  else if (presetNum == MODELS_restankHugeUA) {
    numNodes = 12;
    tankTemps_C = new double[numNodes];
    setpoint_C = 50;

    //start tank off at setpoint
    resetTankToSetpoint();
    
    tankVolume_L = 120; 
    tankUA_kJperHrC = 500; //0 to turn off
    
    doTempDepression = false;
    tankMixesOnDraw = false;


    numHeatSources = 2;
    setOfSources = new HeatSource[numHeatSources];

    //set up a resistive element at the bottom, 4500 kW
    HeatSource resistiveElementBottom(this);
    HeatSource resistiveElementTop(this);
    
    resistiveElementBottom.setupAsResistiveElement(0, 4500);
    resistiveElementTop.setupAsResistiveElement(9, 4500);

    //standard logic conditions
    resistiveElementBottom.addTurnOnLogic(HeatSource::ONLOGIC_bottomThird, 20);
    resistiveElementBottom.addTurnOnLogic(HeatSource::ONLOGIC_standby, 15);

    resistiveElementTop.addTurnOnLogic(HeatSource::ONLOGIC_topThird, 20);
    resistiveElementTop.isVIP = true;
   
    
    //assign heat sources into array in order of priority
    setOfSources[0] = resistiveElementTop;
    setOfSources[1] = resistiveElementBottom;


  }

  //realistic resistive tank
  else if(presetNum == MODELS_restankRealistic) {
    numNodes = 12;
    tankTemps_C = new double[numNodes];
    setpoint_C = F_TO_C(127.0);

    //start tank off at setpoint
    resetTankToSetpoint();
    
    tankVolume_L = GAL_TO_L(50); 
    tankUA_kJperHrC = 10; //0 to turn off
    
    doTempDepression = false;
    //should eventually put tankmixes to true when testing progresses
    tankMixesOnDraw = false;

    numHeatSources = 2;
    setOfSources = new HeatSource[numHeatSources];

    HeatSource resistiveElementBottom(this);
    HeatSource resistiveElementTop(this);
    resistiveElementBottom.setupAsResistiveElement(0, 4500);
    resistiveElementTop.setupAsResistiveElement(9, 4500);

    //standard logic conditions
    resistiveElementBottom.addTurnOnLogic(HeatSource::ONLOGIC_bottomThird, 20);
    resistiveElementBottom.addTurnOnLogic(HeatSource::ONLOGIC_standby, 15);

    resistiveElementTop.addTurnOnLogic(HeatSource::ONLOGIC_topThird, 20);
    resistiveElementTop.isVIP = true;
   
    setOfSources[0] = resistiveElementTop;
    setOfSources[1] = resistiveElementBottom;
  }

  //basic compressor tank for testing
  else if (presetNum == MODELS_basicIntegrated) {
    numNodes = 12;
    tankTemps_C = new double[numNodes];
    setpoint_C = 50;

    //start tank off at setpoint
    resetTankToSetpoint();
    
    tankVolume_L = 120; 
    tankUA_kJperHrC = 10; //0 to turn off
    //tankUA_kJperHrC = 0; //0 to turn off
    
    doTempDepression = false;
    tankMixesOnDraw = false;

    numHeatSources = 3;
    setOfSources = new HeatSource[numHeatSources];

    HeatSource resistiveElementBottom(this);
    HeatSource resistiveElementTop(this);
    HeatSource compressor(this);

    resistiveElementBottom.setupAsResistiveElement(0, 4500);
    resistiveElementTop.setupAsResistiveElement(9, 4500);

    resistiveElementBottom.hysteresis_dC = dF_TO_dC(4);
    
    //standard logic conditions
    resistiveElementBottom.addTurnOnLogic(HeatSource::ONLOGIC_bottomThird, 20);
    resistiveElementBottom.addTurnOnLogic(HeatSource::ONLOGIC_standby, 15);
    resistiveElementBottom.addShutOffLogic(HeatSource::OFFLOGIC_lowTreheat, 0);

    resistiveElementTop.addTurnOnLogic(HeatSource::ONLOGIC_topThird, 20);
    resistiveElementTop.isVIP = true;
   

    compressor.isOn = false;
    compressor.isVIP = false;

    double oneSixth = 1.0/6.0;
    compressor.setCondensity(oneSixth, oneSixth, oneSixth, oneSixth, oneSixth, oneSixth, 0, 0, 0, 0, 0, 0);

    //GE tier 1 values
    compressor.T1_F = 47;
    compressor.T2_F = 67;

    compressor.inputPower_T1_constant_W = 0.290*1000;
    compressor.inputPower_T1_linear_WperF = 0.00159*1000;
    compressor.inputPower_T1_quadratic_WperF2 = 0.00000107*1000;
    compressor.inputPower_T2_constant_W = 0.375*1000;
    compressor.inputPower_T2_linear_WperF = 0.00121*1000;
    compressor.inputPower_T2_quadratic_WperF2 = 0.00000216*1000;
    compressor.COP_T1_constant = 4.49;
    compressor.COP_T1_linear = -0.0187;
    compressor.COP_T1_quadratic = -0.0000133;
    compressor.COP_T2_constant = 5.60;
    compressor.COP_T2_linear = -0.0252;
    compressor.COP_T2_quadratic = 0.00000254;
    compressor.hysteresis_dC = dF_TO_dC(4);
    compressor.configuration = HeatSource::CONFIG_WRAPPED; //wrapped around tank
    
    compressor.addTurnOnLogic(HeatSource::ONLOGIC_bottomThird, 20);
    compressor.addTurnOnLogic(HeatSource::ONLOGIC_standby, 15);

    //lowT cutoff
    compressor.addShutOffLogic(HeatSource::OFFLOGIC_lowT, 0);

    compressor.depressesTemperature = false;  //no temp depression

    //set everything in its places
    setOfSources[0] = resistiveElementTop;
    setOfSources[1] = compressor;
    setOfSources[2] = resistiveElementBottom;

    //and you have to do this after putting them into setOfSources, otherwise
    //you don't get the right pointers
    setOfSources[2].backupHeatSource = &setOfSources[1];
    setOfSources[1].backupHeatSource = &setOfSources[2];
   
  }

  //simple external style for testing
  else if (presetNum == MODELS_externalTest) {
    numNodes = 96;
    tankTemps_C = new double[numNodes];
    setpoint_C = 50;

    //start tank off at setpoint
    resetTankToSetpoint();
    
    tankVolume_L = 120; 
    //tankUA_kJperHrC = 10; //0 to turn off
    tankUA_kJperHrC = 0; //0 to turn off
    
    doTempDepression = false;
    tankMixesOnDraw = false;

    numHeatSources = 1;
    setOfSources = new HeatSource[numHeatSources];

    HeatSource compressor(this);

    compressor.isOn = false;
    compressor.isVIP = false;

    compressor.setCondensity(1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);

    //GE tier 1 values
    compressor.T1_F = 47;
    compressor.T2_F = 67;

    compressor.inputPower_T1_constant_W = 0.290*1000;
    compressor.inputPower_T1_linear_WperF = 0.00159*1000;
    compressor.inputPower_T1_quadratic_WperF2 = 0.00000107*1000;
    compressor.inputPower_T2_constant_W = 0.375*1000;
    compressor.inputPower_T2_linear_WperF = 0.00121*1000;
    compressor.inputPower_T2_quadratic_WperF2 = 0.00000216*1000;
    compressor.COP_T1_constant = 4.49;
    compressor.COP_T1_linear = -0.0187;
    compressor.COP_T1_quadratic = -0.0000133;
    compressor.COP_T2_constant = 5.60;
    compressor.COP_T2_linear = -0.0252;
    compressor.COP_T2_quadratic = 0.00000254;
    compressor.hysteresis_dC = 0;  //no hysteresis
    compressor.configuration = HeatSource::CONFIG_EXTERNAL;
    
    compressor.addTurnOnLogic(HeatSource::ONLOGIC_bottomThird, 20);
    compressor.addTurnOnLogic(HeatSource::ONLOGIC_standby, 15);

    //lowT cutoff
    compressor.addShutOffLogic(HeatSource::OFFLOGIC_bottomNodeMaxTemp, 20);

    compressor.depressesTemperature = false;  //no temp depression

    //set everything in its places
    setOfSources[0] = compressor;
  }
  //voltex 60 gallon
  else if (presetNum == MODELS_Voltex60) {
    numNodes = 12;
    tankTemps_C = new double[numNodes];
    setpoint_C = F_TO_C(127.0);

    //start tank off at setpoint
    resetTankToSetpoint();
    
    tankVolume_L = 215.8; 
    tankUA_kJperHrC = 7.31;
    
    doTempDepression = false;
    tankMixesOnDraw = true;

    numHeatSources = 3;
    setOfSources = new HeatSource[numHeatSources];

    HeatSource compressor(this);
    HeatSource resistiveElementBottom(this);
    HeatSource resistiveElementTop(this);

    //compressor values
    compressor.isOn = false;
    compressor.isVIP = false;

    double split = 1.0/5.0;
    compressor.setCondensity(split, split, split, split, split, 0, 0, 0, 0, 0, 0, 0);

    //voltex60 tier 1 values
    compressor.T1_F = 47;
    compressor.T2_F = 67;

    compressor.inputPower_T1_constant_W = 0.467*1000;
    compressor.inputPower_T1_linear_WperF = 0.00281*1000;
    compressor.inputPower_T1_quadratic_WperF2 = 0.0000072*1000;
    compressor.inputPower_T2_constant_W = 0.541*1000;
    compressor.inputPower_T2_linear_WperF = 0.00147*1000;
    compressor.inputPower_T2_quadratic_WperF2 = 0.0000176*1000;
    compressor.COP_T1_constant = 4.86;
    compressor.COP_T1_linear = -0.0222;
    compressor.COP_T1_quadratic = -0.00001;
    compressor.COP_T2_constant = 6.58;
    compressor.COP_T2_linear = -0.0392;
    compressor.COP_T2_quadratic = 0.0000407;
    compressor.hysteresis_dC = dF_TO_dC(4); 
    compressor.configuration = HeatSource::CONFIG_WRAPPED;
    compressor.depressesTemperature = true;
    //true for compressors, however tempDepression is turned off so it won't depress

    //top resistor values
    resistiveElementTop.setupAsResistiveElement(8, 4250);
    resistiveElementTop.isVIP = true;

    //bottom resistor values
    resistiveElementBottom.setupAsResistiveElement(0, 2000);
    resistiveElementBottom.hysteresis_dC = dF_TO_dC(4);
   
    //logic conditions
    double compStart = dF_TO_dC(43.6);
    double lowTcutoff = F_TO_C(40.0);
    double standby = dF_TO_dC(23.8);
    compressor.addTurnOnLogic(HeatSource::ONLOGIC_bottomThird, compStart);
    compressor.addTurnOnLogic(HeatSource::ONLOGIC_standby, standby);
    compressor.addShutOffLogic(HeatSource::OFFLOGIC_lowT, lowTcutoff);
    
    resistiveElementBottom.addTurnOnLogic(HeatSource::ONLOGIC_bottomThird, compStart);
    resistiveElementBottom.addShutOffLogic(HeatSource::OFFLOGIC_lowTreheat, lowTcutoff);

    resistiveElementTop.addTurnOnLogic(HeatSource::ONLOGIC_topThird, dF_TO_dC(36.0));


    //set everything in its places
    setOfSources[0] = resistiveElementTop;
    setOfSources[1] = compressor;
    setOfSources[2] = resistiveElementBottom;

    //and you have to do this after putting them into setOfSources, otherwise
    //you don't get the right pointers
    setOfSources[2].backupHeatSource = &setOfSources[1];
    setOfSources[1].backupHeatSource = &setOfSources[2];

  }
  else if (presetNum == MODELS_Voltex80) {
    numNodes = 12;
    tankTemps_C = new double[numNodes];
    setpoint_C = F_TO_C(127.0);

    //start tank off at setpoint
    resetTankToSetpoint();
    
    tankVolume_L = 283.9; 
    tankUA_kJperHrC = 8.8;
    
    doTempDepression = false;
    tankMixesOnDraw = true;

    numHeatSources = 3;
    setOfSources = new HeatSource[numHeatSources];

    HeatSource compressor(this);
    HeatSource resistiveElementBottom(this);
    HeatSource resistiveElementTop(this);

    //compressor values
    compressor.isOn = false;
    compressor.isVIP = false;

    double split = 1.0/5.0;
    compressor.setCondensity(split, split, split, split, split, 0, 0, 0, 0, 0, 0, 0);

    //voltex60 tier 1 values
    compressor.T1_F = 47;
    compressor.T2_F = 67;

    compressor.inputPower_T1_constant_W = 0.467*1000;
    compressor.inputPower_T1_linear_WperF = 0.00281*1000;
    compressor.inputPower_T1_quadratic_WperF2 = 0.0000072*1000;
    compressor.inputPower_T2_constant_W = 0.541*1000;
    compressor.inputPower_T2_linear_WperF = 0.00147*1000;
    compressor.inputPower_T2_quadratic_WperF2 = 0.0000176*1000;
    compressor.COP_T1_constant = 4.86;
    compressor.COP_T1_linear = -0.0222;
    compressor.COP_T1_quadratic = -0.00001;
    compressor.COP_T2_constant = 6.58;
    compressor.COP_T2_linear = -0.0392;
    compressor.COP_T2_quadratic = 0.0000407;
    compressor.hysteresis_dC = dF_TO_dC(4);
    compressor.configuration = HeatSource::CONFIG_WRAPPED;
    compressor.depressesTemperature = true;
    //true for compressors, however tempDepression is turned off so it won't depress

    //top resistor values
    resistiveElementTop.setupAsResistiveElement(8, 4250);
    resistiveElementTop.isVIP = true;

    //bottom resistor values
    resistiveElementBottom.setupAsResistiveElement(0, 2000);
    resistiveElementBottom.hysteresis_dC = dF_TO_dC(4);

   
    //logic conditions
    double compStart = dF_TO_dC(43.6);
    double lowTcutoff = F_TO_C(40.0);
    double standby = dF_TO_dC(23.8);
    compressor.addTurnOnLogic(HeatSource::ONLOGIC_bottomThird, compStart);
    compressor.addTurnOnLogic(HeatSource::ONLOGIC_standby, standby);
    compressor.addShutOffLogic(HeatSource::OFFLOGIC_lowT, lowTcutoff);
    
    resistiveElementBottom.addTurnOnLogic(HeatSource::ONLOGIC_bottomThird, compStart);
    resistiveElementBottom.addShutOffLogic(HeatSource::OFFLOGIC_lowTreheat, lowTcutoff);

    resistiveElementTop.addTurnOnLogic(HeatSource::ONLOGIC_topThird, dF_TO_dC(36.0));


    //set everything in its places
    setOfSources[0] = resistiveElementTop;
    setOfSources[1] = compressor;
    setOfSources[2] = resistiveElementBottom;

    //and you have to do this after putting them into setOfSources, otherwise
    //you don't get the right pointers
    setOfSources[2].backupHeatSource = &setOfSources[1];
    setOfSources[1].backupHeatSource = &setOfSources[2];

  }
  else if (presetNum == MODELS_GEGeospring) {
    numNodes = 12;
    tankTemps_C = new double[numNodes];
    setpoint_C = F_TO_C(127.0);

    //start tank off at setpoint
    resetTankToSetpoint();
    
    tankVolume_L = 172; 
    tankUA_kJperHrC = 6.8;
    
    doTempDepression = false;
    tankMixesOnDraw = true;

    numHeatSources = 3;
    setOfSources = new HeatSource[numHeatSources];

    HeatSource compressor(this);
    HeatSource resistiveElementBottom(this);
    HeatSource resistiveElementTop(this);

    //compressor values
    compressor.isOn = false;
    compressor.isVIP = false;

    double split = 1.0/5.0;
    compressor.setCondensity(split, split, split, split, split, 0, 0, 0, 0, 0, 0, 0);

    compressor.T1_F = 47;
    compressor.T2_F = 67;

    compressor.inputPower_T1_constant_W = 0.247*1000;
    compressor.inputPower_T1_linear_WperF = 0.00159*1000;
    compressor.inputPower_T1_quadratic_WperF2 = 0.00000107*1000;
    compressor.inputPower_T2_constant_W = 0.328*1000;
    compressor.inputPower_T2_linear_WperF = 0.00121*1000;
    compressor.inputPower_T2_quadratic_WperF2 = 0.00000216*1000;
    compressor.COP_T1_constant = 4.92;
    compressor.COP_T1_linear = -0.0210;
    compressor.COP_T1_quadratic = 0.0;
    compressor.COP_T2_constant = 5.03;
    compressor.COP_T2_linear = -0.0167;
    compressor.COP_T2_quadratic = 0.0;
    compressor.hysteresis_dC = dF_TO_dC(4);
    compressor.configuration = HeatSource::CONFIG_WRAPPED;
    compressor.depressesTemperature = true;
    //true for compressors, however tempDepression is turned off so it won't depress

    //top resistor values
    resistiveElementTop.setupAsResistiveElement(8, 4250);
    resistiveElementTop.isVIP = true;

    //bottom resistor values
    resistiveElementBottom.setupAsResistiveElement(0, 2000);
    resistiveElementBottom.hysteresis_dC = dF_TO_dC(4);

   
    //logic conditions
    double compStart = dF_TO_dC(24.4);
    double lowTcutoff = F_TO_C(40.0);
    double standby = dF_TO_dC(29.1);
    compressor.addTurnOnLogic(HeatSource::ONLOGIC_bottomThird, compStart);
    compressor.addTurnOnLogic(HeatSource::ONLOGIC_standby, standby);
    compressor.addShutOffLogic(HeatSource::OFFLOGIC_lowT, lowTcutoff);
    compressor.addShutOffLogic(HeatSource::OFFLOGIC_largeDraw, F_TO_C(90));
    
    resistiveElementBottom.addTurnOnLogic(HeatSource::ONLOGIC_bottomThird, compStart);
    //resistiveElementBottom.addShutOffLogic(HeatSource::OFFLOGIC_lowTreheat, lowTcutoff);
    //GE element never turns off?

    resistiveElementTop.addTurnOnLogic(HeatSource::ONLOGIC_topThird, dF_TO_dC(30.0));


    //set everything in its places
    setOfSources[0] = resistiveElementTop;
    setOfSources[1] = compressor;
    setOfSources[2] = resistiveElementBottom;

    //and you have to do this after putting them into setOfSources, otherwise
    //you don't get the right pointers
    setOfSources[2].backupHeatSource = &setOfSources[1];
    setOfSources[1].backupHeatSource = &setOfSources[2];

  }
  else {
    if (hpwhVerbosity >= VRB_reluctant) sayMessage("You have tried to select a preset model which does not exist.  \n");
    return HPWH_ABORT;
  }



  //now, calculate some of the derived values

  //condentropy/shrinkage
  double condentropy = 0;
  double alpha = 1, beta = 2;  // Mapping from condentropy to shrinkage
  for (int i = 0; i < numHeatSources; i++) {
    if (hpwhVerbosity >= VRB_emetic){
      sprintf(outputString, "Heat Source %d \n", i);
      sayMessage(string(outputString));
    }    
    // Calculate condentropy and ==> shrinkage
    condentropy = 0;
    for(int j = 0; j < CONDENSITY_SIZE; j++) {
      if(setOfSources[i].condensity[j] > 0) {
        condentropy -= setOfSources[i].condensity[j] * log(setOfSources[i].condensity[j]);
        if (hpwhVerbosity >= VRB_emetic){
          sprintf(outputString, "condentropy %.2lf \n", condentropy);
          sayMessage(string(outputString));
        }
      }
    }
    setOfSources[i].shrinkage = alpha + condentropy * beta;
    if (hpwhVerbosity >= VRB_emetic){
      sprintf(outputString, "shrinkage %.2lf \n\n", setOfSources[i].shrinkage);
      sayMessage(string(outputString));
    }
  }


    //lowest node
  int lowest = 0;
  for (int i = 0; i < numHeatSources; i++) {
    lowest = 0;
    if (hpwhVerbosity >= VRB_emetic){
      sprintf(outputString, "Heat Source %d \n", i);
      sayMessage(string(outputString));
    } 
    for(int j = 0; j < numNodes; j++) {
      if (hpwhVerbosity >= VRB_emetic){
        sprintf(outputString, "j: %d  j/ (numNodes/CONDENSITY_SIZE) %d \n", j, j/ (numNodes/CONDENSITY_SIZE));
        sayMessage(string(outputString));
      }
      if(setOfSources[i].condensity[ (j/ (numNodes/CONDENSITY_SIZE) ) ] > 0) {
        lowest = j;
        break;
      }
    }
    if (hpwhVerbosity >= VRB_emetic){
      sprintf(outputString, " lowest : %d \n", lowest);
      sayMessage(string(outputString));
    }
    setOfSources[i].lowestNode = lowest;
  }


  
  if (hpwhVerbosity >= VRB_emetic){
     for (int i = 0; i < numHeatSources; i++) {
      sprintf(outputString, "heat source %d: %p \n", i , &setOfSources[i]);
      sayMessage(string(outputString));
    }
    sayMessage("\n\n");
  }
  return 0;  //successful init returns 0
}  //end HPWHinit_presets

