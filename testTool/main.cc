/*This is not a substitute for a proper HPWH Test Tool, it is merely a short program
 * to aid in the testing of the new HPWH.cc as it is being written.
 * 
 * -NDK
 *
 * Bring on the HPWH Test Tool!!! -MJL
 *
 * 
 * 
 */
#include "HPWH.hh"
#include <iostream>
#include <fstream>
#include <sstream>

#define MAX_DIR_LENGTH 255
#define DEBUG 0

using std::cout;
using std::endl;
using std::string;
using std::ifstream;
using std::ofstream;


#define F_TO_C(T) ((T-32.0)*5.0/9.0)
#define GAL_TO_L(GAL) (GAL*3.78541)

typedef std::vector<double> schedule;


int readSchedule(schedule &scheduleArray, string scheduleFileName, int minutesOfTest);
int getSimTcouples(HPWH &hpwh, std::vector<double> &tcouples);
int getHeatSources(HPWH &hpwh, std::vector<double> &inputs, std::vector<double> &outputs);

int main(int argc, char *argv[])
{
  HPWH hpwh;

  std::vector<double> simTCouples(6);
  std::vector<double> heatSourcesEnergyInput, heatSourcesEnergyOutput;

  // Schedule stuff  
  std::vector<string> scheduleNames;
  std::vector<schedule> allSchedules(5);

  string testDirectory, fileToOpen, scheduleName, var1, input1, input2;
  string inputVariableName;
  int i, j, minutesToRun, outputCode, nSources;

  ofstream outputFile;
  ifstream controlFile;

  //.......................................
  //process command line arguments
  //.......................................  


  //Obvious wrong number of command line arguments
  if ((argc > 3)) {
    // printf("Invalid input.  This program takes a single argument.  Help is on the way:\n\n");
    cout << "Invalid input. This program takes two arguments. test name and model name\n";
    exit(1);
  }
  //Help message
  if(argc > 1) {
    input1 = argv[1];
    input2 = argv[2];
  } else {
    input1 = "asdf"; // Makes the next conditional not crash... a little clumsy but whatever
    input2 = "def";
  }
  if ((argc != 3) || (input1 == "?") || (input1 == "help")) {
    cout << "Standard usage: \"hpwhTestTool.x test_name model_name\"\n";
    cout << "All input files should be located in the test directory, with these names:\n"; 
    cout << "drawschedule.csv DRschedule.csv ambientTschedule.csv evaporatorTschedule.csv inletTschedule.csv hpwhProperties.csv\n"; 
    cout << "An output file, `modelname'Output.csv, will be written in the test directory\n";
    exit(1);
  }

  //Only input file specified -- don't suffix with .csv
  testDirectory = "tests/" + input1;

  // Read the test control file
  fileToOpen = testDirectory + "/" + "testInfo.txt";
  controlFile.open(fileToOpen.c_str());
  while(controlFile >> var1 >> minutesToRun) {
    if(var1 == "length_of_test") break;
  }

  if(var1 != "length_of_test") {
    cout << "Error, first line of testInfo.txt should have length_of_test\n";
    exit(1);
  }

  
  // ------------------------------------- Read Schedules--------------------------------------- //
  scheduleNames.push_back("inletT");
  scheduleNames.push_back("draw");
  scheduleNames.push_back("ambientT");
  scheduleNames.push_back("evaporatorT");
  scheduleNames.push_back("DR");
  for(i = 0; (unsigned)i < scheduleNames.size(); i++) {
    fileToOpen = testDirectory + "/" + scheduleNames[i] + "schedule.csv";
    outputCode = readSchedule(allSchedules[i], fileToOpen, minutesToRun);
    if(outputCode != 0) {
      cout << "readSchedule returns an error on " << scheduleNames[i] << " schedule!\n";
      exit(1);
    }
  }

  // Set the hpwh properties. I'll need to update this to select the appropriate model
  hpwh.HPWHinit_presets(4);
  nSources = hpwh.getNumHeatSources();
  for(i = 0; i < nSources; i++) {
    heatSourcesEnergyInput.push_back(0.0);
    heatSourcesEnergyOutput.push_back(0.0);
  } 


  // ----------------------Open the Output File and Print the Header---------------------------- //
  fileToOpen = testDirectory + "/" + input2 + "TestToolOutput.csv";
  outputFile.open(fileToOpen.c_str());
  outputFile << "minutes,Ta,inletT,draw";
  for(i = 0; i < hpwh.getNumHeatSources(); i++) {
    outputFile << ",input_kWh" << i + 1 << ",output_kWh" << i + 1;
  }
  outputFile << ",simTcouples1,simTcouples2,simTcouples3,simTcouples4,simTcouples5,simTcouples6\n";


  // ------------------------------------- Simulate --------------------------------------- //

  // Loop over the minutes in the test
  cout << "Now Simulating " << minutesToRun << " Minutes of the Test\n";
  for(i = 0; i < minutesToRun; i++) {
    // Run the step
    hpwh.runOneStep(F_TO_C(allSchedules[0][i]),    // Inlet water temperature (C)
                      allSchedules[1][i],          // Flow in gallons
                      F_TO_C(allSchedules[2][i]),  // Ambient Temp (C)
                      F_TO_C(allSchedules[3][i]),  // External Temp (C)
                      allSchedules[4][i], 1.0);    // DDR Status (1 or 0)

    // Grab the current status
    getHeatSources(hpwh, heatSourcesEnergyInput, heatSourcesEnergyOutput);
    getSimTcouples(hpwh, simTCouples);
    /*for(int k = 0; k < 6; k++) simTCouples[k] = 25;
    for(int k = 0; k < nSources; k++) {
      heatSourcesEnergyInput[k] = 0;
      heatSourcesEnergyOutput[k] = 0;
    }*/

    // Copy current status into the output file
    outputFile << i << "," << allSchedules[2][i] << "," << allSchedules[0][i] << "," << allSchedules[1][i];
    for(j = 0; j < hpwh.getNumHeatSources(); j++) {
      outputFile << "," << heatSourcesEnergyInput[j] << "," << heatSourcesEnergyOutput[j];
    }
    outputFile << "," << simTCouples[0] << "," << simTCouples[1] << "," << simTCouples[2] << 
      "," << simTCouples[3] << "," << simTCouples[4] << "," << simTCouples[5] << "\n";
  }

  controlFile.close();
  outputFile.close();


  return 0;

}




  


// this function reads the named schedule into the provided array
int readSchedule(schedule &scheduleArray, string scheduleFileName, int minutesOfTest) {
  int i, minuteTmp;
  string line, snippet, s;
  double valTmp;
  ifstream inputFile(scheduleFileName.c_str());  
  //open the schedule file provided
  cout << "Opening " << scheduleFileName << '\n';

  if(!inputFile.is_open()) {
    cout << "Unable to open file" << '\n';
    return 1;
  } 

  inputFile >> snippet >> valTmp;
  if(snippet != "default") {
    cout << "First line of " << scheduleFileName << " must specify default\n";
    return 1;
  }
  // cout << valTmp << " minutes = " << minutesOfTest << "\n";
  // cout << "size " << scheduleArray.size() << "\n";
  // Fill with the default value
  for(i = 0; i < minutesOfTest; i++) {
    scheduleArray.push_back(valTmp);
    // scheduleArray[i] = valTmp;
  }

  // Burn the first two lines
  std::getline(inputFile, line);
  std::getline(inputFile, line);

  // Read all the exceptions
  while(std::getline(inputFile, line)) {
    std::stringstream ss(line); // Will parse with a stringstream

    // Grab the first token, which is the minute
    std::getline(ss, s, ',');
    std::istringstream(s) >> minuteTmp;

    // Grab the second token, which is the value
    std::getline(ss, s, ',');
    std::istringstream(s) >> valTmp;

    // Update the value
    scheduleArray[minuteTmp - 1] = valTmp;
  }

  //print out the whole schedule
  if(DEBUG == 1){
    for(i = 0; (unsigned)i < scheduleArray.size(); i++){
      cout << "scheduleArray[" << i << "] = " << scheduleArray[i] << "\n";
    }
  }

  inputFile.close();

  return 0;
  
}


int getSimTcouples(HPWH &hpwh, std::vector<double> &tcouples) {
  int i;

  tcouples.clear();
  for(i = 0; i < 6; i++) {
    tcouples[i] = hpwh.getNthSimTcouple(i + 1); 
  }
  return 0;
}

int getHeatSources(HPWH &hpwh, std::vector<double> &inputs, std::vector<double> &outputs) {
  int i;
  inputs.clear();
  outputs.clear();
  for(i = 0; i < hpwh.getNumHeatSources(); i++) {
    inputs[i] = hpwh.getNthHeatSourceEnergyInput(i);
    outputs[i] = hpwh.getNthHeatSourceEnergyOutput(i);
  }
  return 0;
}


