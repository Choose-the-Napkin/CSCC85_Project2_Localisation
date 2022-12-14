/*

  CSC C85 - Embedded Systems - Project # 1 - EV3 Robot Localization
  
 This file provides the implementation of all the functionality required for the EV3
 robot localization project. Please read through this file carefully, and note the
 sections where you must implement functionality for your bot. 
 
 You are allowed to change *any part of this file*, not only the sections marked
 ** TO DO **. You are also allowed to add functions as needed (which must also
 be added to the header file). However, *you must clearly document* where you 
 made changes so your work can be properly evaluated by the TA.

 NOTES on your implementation:

 * It should be free of unreasonable compiler warnings - if you choose to ignore
   a compiler warning, you must have a good reason for doing so and be ready to
   defend your rationale with your TA.
 * It must be free of memory management errors and memory leaks - you are expected
   to develop high wuality, clean code. Test your code extensively with valgrind,
   and make sure its memory management is clean.
 
 In a nutshell, the starter code provides:
 
 * Reading a map from an input image (in .ppm format). The map is bordered with red, 
   must have black streets with yellow intersections, and buildings must be either
   blue, green, or be left white (no building).
   
 * Setting up an array with map information which contains, for each intersection,
   the colours of the buildings around it in ** CLOCKWISE ** order from the top-left.
   
 * Initialization of the EV3 robot (opening a socket and setting up the communication
   between your laptop and your bot)
   
 What you must implement:
 
 * All aspects of robot control:
   - Finding and then following a street
   - Recognizing intersections
   - Scanning building colours around intersections
   - Detecting the map boundary and turning around or going back - the robot must not
     wander outside the map (though of course it's possible parts of the robot will
     leave the map while turning at the boundary)

 * The histogram-based localization algorithm that the robot will use to determine its
   location in the map - this is as discussed in lecture.

 * Basic robot exploration strategy so the robot can scan different intersections in
   a sequence that allows it to achieve reliable localization
   
 * Basic path planning - once the robot has found its location, it must drive toward a 
   user-specified position somewhere in the map.

 --- OPTIONALLY but strongly recommended ---
 
  The starter code provides a skeleton for implementing a sensor calibration routine,
 it is called when the code receives -1  -1 as target coordinates. The goal of this
 function should be to gather informatin about what the sensor reads for different
 colours under the particular map/room illumination/battery level conditions you are
 working on - it's entirely up to you how you want to do this, but note that careful
 calibration would make your work much easier, by allowing your robot to more
 robustly (and with fewer mistakes) interpret the sensor data into colours. 
 
   --> The code will exit after calibration without running localization (no target!)
       SO - your calibration code must *save* the calibration information into a
            file, and you have to add code to main() to read and use this
            calibration data yourselves.
   
 What you need to understand thoroughly in order to complete this project:
 
 * The histogram localization method as discussed in lecture. The general steps of
   probabilistic robot localization.

 * Sensors and signal management - your colour readings will be noisy and unreliable,
   you have to handle this smartly
   
 * Robot control with feedback - your robot does not perform exact motions, you can
   assume there will be error and drift, your code has to handle this.
   
 * The robot control API you will use to get your robot to move, and to acquire 
   sensor data. Please see the API directory and read through the header files and
   attached documentation
   
 Starter code:
 F. Estrada, 2018 - for CSC C85 
 
*/

#include "EV3_Localization.h"
#include <signal.h>
#include <time.h>
#include <stdio.h>

#define COLOUR_INPUT PORT_1
#define GYRO_INPUT PORT_2
#define BACK_TOUCH_INPUT PORT_3
#define TOP_TOUCH_INPUT PORT_4
#define RIGHT_WHEEL_OUTPUT MOTOR_A
#define LEFT_WHEEL_OUTPUT MOTOR_D
#define SENSOR_WHEEL_OUTPUT MOTOR_B

#define COLOUR_BLACK 1
#define COLOUR_BLUE 2
#define COLOUR_GREEN 3
#define COLOUR_YELLOW 4
#define COLOUR_RED 5
#define COLOUR_WHITE 6
#define COLOUR_UNKNOWN 7

#define SENSOR_WHEEL_POWER 50
#define FORWARD_POWER 15
#define TURN_POWER 10
#define whiteMax 305.0
#define THRESHOLD_OF_CERTAINTY 0.8

typedef struct {
  int r;
  int g;
  int b;
  int color;
} colorReading;

colorReading calibration_readings[30*6]; // 30 samples * 6 colors
#define col_ptr(color) calibration_readings+(30*(color-1))

int map[400][4];            // This holds the representation of the map, up to 20x20
                            // intersections, raster ordered, 4 building colours per
                            // intersection.
int sx, sy;                 // Size of the map (number of intersections along x and y)
double beliefs[400][4];     // Beliefs for each location and motion direction

void handle_out_of_bounds();

void playBeep(int mode){
  int tone_data[50][3];
  // Reset tone data information
  for (int i=0;i<50; i++) 
  {
    tone_data[i][0]=-1;
    tone_data[i][1]=-1;
    tone_data[i][2]=-1;
  }

 tone_data[0][0]=50;
 tone_data[0][1]=500;
 tone_data[0][2]=1;

 if (mode == COLOUR_BLUE) tone_data[0][0]=600;
 else if (mode == COLOUR_GREEN) tone_data[0][0]=1150;
 else if (mode == 100){ // Finished localization or pathing
    tone_data[0][1]=250;

    tone_data[1][0]=600;
    tone_data[1][1]=250;
    tone_data[1][2]=1;

    tone_data[2][0]=1150;
    tone_data[2][1]=250;
    tone_data[2][2]=1;
    
    tone_data[3][0]=150;
    tone_data[3][1]=500;
    tone_data[3][2]=1;
 }
 
 BT_play_tone_sequence(tone_data);
}


int main(int argc, char *argv[])
{
 char mapname[1024];
 int dest_x, dest_y, rx, ry;
 unsigned char *map_image;
 
 memset(&map[0][0],0,400*4*sizeof(int));
 sx=0;
 sy=0;
 
 if (argc<4)
 {
  fprintf(stderr,"Usage: EV3_Localization map_name dest_x dest_y\n");
  fprintf(stderr,"    map_name - should correspond to a properly formatted .ppm map image\n");
  fprintf(stderr,"    dest_x, dest_y - target location for the bot within the map, -1 -1 calls calibration routine\n");
  exit(1);
 }
 strcpy(&mapname[0],argv[1]);
 dest_x=atoi(argv[2]);
 dest_y=atoi(argv[3]);

 if (dest_x==-1&&dest_y==-1)
 {
  calibrate_sensor();
  exit(1);
 }

 /******************************************************************************************************************
  * OPTIONAL TO DO: If you added code for sensor calibration, add just below this comment block any code needed to
  *   read your calibration data for use in your localization code. Skip this if you are not using calibration
  * ****************************************************************************************************************/
 FILE* f = fopen("./calibration", "r");
 fread(calibration_readings, sizeof(colorReading), 30*6, f);
 fclose(f);
 
 // Your code for reading any calibration information should not go below this line //
 
 map_image=readPPMimage(&mapname[0],&rx,&ry);
 if (map_image==NULL)
 {
  fprintf(stderr,"Unable to open specified map image\n");
  exit(1);
 }
 
 if (parse_map(map_image, rx, ry)==0)
 { 
  fprintf(stderr,"Unable to parse input image map. Make sure the image is properly formatted\n");
  free(map_image);
  exit(1);
 }

 if (dest_x<0||dest_x>=sx||dest_y<0||dest_y>=sy)
 {
  fprintf(stderr,"Destination location is outside of the map\n");
  free(map_image);
  exit(1);
 }

 // Initialize beliefs - uniform probability for each location and direction
 for (int j=0; j<sy; j++)
  for (int i=0; i<sx; i++)
  {
   beliefs[i+(j*sx)][0]=1.0/(double)(sx*sy*4);
   beliefs[i+(j*sx)][1]=1.0/(double)(sx*sy*4);
   beliefs[i+(j*sx)][2]=1.0/(double)(sx*sy*4);
   beliefs[i+(j*sx)][3]=1.0/(double)(sx*sy*4);
  }

 // Open a socket to the EV3 for remote controlling the bot.
 if (BT_open(HEXKEY)!=0)
 {
  fprintf(stderr,"Unable to open comm socket to the EV3, make sure the EV3 kit is powered on, and that the\n");
  fprintf(stderr," hex key for the EV3 matches the one in EV3_Localization.h\n");
  free(map_image);
  exit(1);
 }
  
 fprintf(stderr,"All set, ready to go!\n");
 
/*******************************************************************************************************************************
 *
 *  TO DO - Implement the main localization loop, this loop will have the robot explore the map, scanning intersections and
 *          updating beliefs in the beliefs array until a single location/direction is determined to be the correct one.
 * 
 *          The beliefs array contains one row per intersection (recall that the number of intersections in the map_image
 *          is given by sx, sy, and that the map[][] array contains the colour indices of buildings around each intersection.
 *          Indexing into the map[][] and beliefs[][] arrays is by raster order, so for an intersection at i,j (with 0<=i<=sx-1
 *          and 0<=j<=sy-1), index=i+(j*sx)
 *  
 *          In the beliefs[][] array, you need to keep track of 4 values per intersection, these correspond to the belief the
 *          robot is at that specific intersection, moving in one of the 4 possible directions as follows:
 * 
 *          beliefs[i][0] <---- belief the robot is at intersection with index i, facing UP
 *          beliefs[i][1] <---- belief the robot is at intersection with index i, facing RIGHT
 *          beliefs[i][2] <---- belief the robot is at intersection with index i, facing DOWN
 *          beliefs[i][3] <---- belief the robot is at intersection with index i, facing LEFT
 * 
 *          Initially, all of these beliefs have uniform, equal probability. Your robot must scan intersections and update
 *          belief values based on agreement between what the robot sensed, and the colours in the map. 
 * 
 *          You have two main tasks these are organized into two major functions:
 * 
 *          robot_localization()    <---- Runs the localization loop until the robot's location is found
 *          go_to_target()          <---- After localization is achieved, takes the bot to the specified map location
 * 
 *          The target location, read from the command line, is left in dest_x, dest_y
 * 
 *          Here in main(), you have to call these two functions as appropriate. But keep in mind that it is always possible
 *          that even if your bot managed to find its location, it can become lost again while driving to the target
 *          location, or it may be the initial localization was wrong and the robot ends up in an unexpected place - 
 *          a very solid implementation should give your robot the ability to determine it's lost and needs to 
 *          run localization again.
 *
 *******************************************************************************************************************************/  

 // HERE - write code to call robot_localization() and go_to_target() as needed, any additional logic required to get the
 //        robot to complete its task should be here.
 int x = -1;
 int y = -1;
 int dir = -1;
 robot_localization(&x, &y, &dir);
 BT_all_stop(0);
 playBeep(1000);

 go_to_target(x, y, dir, dest_x, dest_y);
 BT_all_stop(0);
 playBeep(1000);

 // Cleanup and exit - DO NOT WRITE ANY CODE BELOW THIS LINE
 BT_close();
 free(map_image);
 exit(0);
}


/*
int colourFromRGB(int RGB[3], int lastChosen){
  if (RGB[0] < 0 || RGB[0] > 1020 || RGB[1] < 0 || RGB[1] > 1020 || RGB[2] < 0 || RGB[2] > 1020){
    if (lastChosen == COLOUR_UNKNOWN) return COLOUR_UNKNOWN;
    return colourFromRGB(RGB, COLOUR_UNKNOWN); 
  }
  
  // Check Black/White
  if (RGB[0] > 150 && RGB[1] > 150 && RGB[2] > 150) return COLOUR_WHITE;
  if (RGB[0] < 50 && RGB[1] < 50 && RGB[2] < 50){
    int c = BT_read_colour_sensor(COLOUR_INPUT);  // Built in reader is good at figuring black vs green
    return c == COLOUR_GREEN ? COLOUR_GREEN : COLOUR_BLACK;
  }
  
  int bel;
  // Check yellow proportions
  if (RGB[0] > RGB[2] && RGB[1] > RGB[2] && RGB[0] + RGB[1] > RGB[2]*2) bel = COLOUR_YELLOW;
  
  // Check R/G/B (return highest)
  else if (RGB[0] > RGB[1]*1.1 && RGB[0] > RGB[2]*1.1) bel = COLOUR_RED;
  else if (RGB[1] > RGB[0]*1.1 && RGB[1] > RGB[2]*1.1) bel = COLOUR_GREEN;
  else if (RGB[2] > RGB[0]*1.1 && RGB[2] > RGB[1]*1.1) bel = COLOUR_BLUE;
  bel = COLOUR_UNKNOWN;
  
  // Double check that odd color was seen twice
  if (bel == lastChosen) return bel;
  return colourFromRGB(RGB, bel);
}

 */

int colourFromRGB2(int buf[3]) {
  int min_sqdiff = 100, min_color = 7, curr_sqdiff;
  for (int i = 0; i < 30*6; i++) {
    curr_sqdiff = pow(buf[0] - calibration_readings[i].r, 2);
    curr_sqdiff += pow(buf[1] - calibration_readings[i].g, 2);
    curr_sqdiff += pow(buf[2] - calibration_readings[i].b, 2);
    
    if (curr_sqdiff < min_sqdiff) {
      min_sqdiff = curr_sqdiff;
      min_color = calibration_readings[i].color;
    }
  }

  return min_color;
}

int colourFromRGB(int RGB[3]){
  
  if (RGB[0] < 0 || RGB[0] > 1020 || RGB[1] < 0 || RGB[1] > 1020 || RGB[2] < 0 || RGB[2] > 1020) return COLOUR_UNKNOWN;
  if (RGB[0] > 150 && RGB[1] > 150 && RGB[2] > 150) return COLOUR_WHITE;
  if (RGB[0] > 200 && RGB[1] < 100 && RGB[2] < 100) return COLOUR_RED;
  if (RGB[0] > 100 && RGB[1] > 100 && RGB[2] < 100) return COLOUR_YELLOW;
  if (RGB[0] < 50 && RGB[1] > 40 && RGB[2] < 60) return COLOUR_GREEN;
  if (RGB[2] > 75) return COLOUR_BLUE;
  if (RGB[0] < 50 && RGB[1] < 50 && RGB[2] < 50){
    int c = BT_read_colour_sensor(COLOUR_INPUT);
    return c == COLOUR_GREEN ? COLOUR_GREEN : COLOUR_BLACK;
  }
  return COLOUR_UNKNOWN;
  //return BT_read_colour_sensor(COLOUR_INPUT);
}

void normalized_color_read(int* buf) {
  BT_read_colour_sensor_RGB(COLOUR_INPUT, buf);

  for (int i = 0; i < 3; i++){
    buf[i] = (int) ((double)buf[i] * 256.0 / whiteMax);
  }
}

int getColourFromSensor(){
  int RGB[3];
  normalized_color_read(RGB); // populate RGB
  int colour = colourFromRGB(RGB);
  //printf("Reading %d %d %d as colour %d \n", RGB[0], RGB[1], RGB[2], colour);
  return colour;
}

int read_touch_robust(int port) {
  for (int i = 0; i < 3; i++) { // Too many bluetooth calls slows down tha program
    if (BT_read_touch_sensor(port) == 0) return 0;
  }
  return 1;
}

void shift_color_sensor(int shift_mode) {
  // shift_mode 1: Extended, 0: Retracted
  //printf("Shifting robot color sensor\n");
  //fflush(stdout);
  int flag = 0; // Result of last touch sensor read
  int touch_port = shift_mode == 0 ? BACK_TOUCH_INPUT : TOP_TOUCH_INPUT;
  int power_direction = shift_mode == 0 ? 1 : -1;
  BT_motor_port_start(SENSOR_WHEEL_OUTPUT, SENSOR_WHEEL_POWER * power_direction);
  while (!read_touch_robust(touch_port)) {}
  BT_all_stop(0);
  //usleep(1000*100);
  //BT_timed_motor_port_start_v2(SENSOR_WHEEL_OUTPUT, SENSOR_WHEEL_POWER * -power_direction, 50);
}

void shift_sensor_until_color(int color, int shift_mode) {
  // shift_mode 1: Extended, 0: Retracted
  int flag = getColourFromSensor() == color;
  int touch_port = shift_mode == 0 ? BACK_TOUCH_INPUT : TOP_TOUCH_INPUT;
  int power_direction = shift_mode == 0 ? 1 : -1;
  int color_read;
  while (getColourFromSensor() != color && read_touch_robust(touch_port) == 0) {
    BT_motor_port_start(SENSOR_WHEEL_OUTPUT, SENSOR_WHEEL_POWER * power_direction);
    usleep(1000*25);
    BT_all_stop(0);
    usleep(1000*100);
  }
}

void slight_robot_turn(int amount){
    BT_motor_port_start(LEFT_WHEEL_OUTPUT, amount);
    BT_motor_port_start(RIGHT_WHEEL_OUTPUT, -amount);
    usleep(1000 * 125);
    BT_all_stop(0);
    usleep(1000 * 125);
}

int check_line_is_black(){
  // Case 1: Black - not black - Black  -> Not valid
  // Case 2: Black (extended) - not black (retracted); 
  // -> we probably got offset while rotating, push until we're on the black that we're looking for and stay aligned
  
  // Step 1: Scan line and see if there is a white in between black
  int isOnRoad = 1;
  while (read_touch_robust(BACK_TOUCH_INPUT) == 0){
    BT_motor_port_start(SENSOR_WHEEL_OUTPUT, SENSOR_WHEEL_POWER);
    usleep(1000*50);
    BT_all_stop(0);
    usleep(1000*100);
    
    int color_read = getColourFromSensor();
    int nowOnRoad = (color_read == COLOUR_BLACK || color_read == COLOUR_YELLOW);
    //if (!isOnRoad && nowOnRoad) return 0; // We shifted back onto the road, this is probably an intersection looking 45 degrees
    //nowOnRoad = isOnRoad;
    if (!isOnRoad) return 0;
  }
  return 1;
  
  /*
  // Stay retracted, move up if black offset case
  shift_color_sensor(0);
  while (1){
    int color_read = getColourFromSensor();
    int nowOnRoad = (color_read == COLOUR_BLACK || color_read == COLOUR_YELLOW);
    
    if (nowOnRoad) break;
    BT_drive(LEFT_WHEEL_OUTPUT, RIGHT_WHEEL_OUTPUT, FORWARD_POWER);
    usleep(1000*50);
    BT_all_stop(0);
    usleep(1000*75);
  }
  return 1;
  */
}

int is_lined_up_on_black(void){
  int seen = getColourFromSensor();
  if (seen != COLOUR_BLACK && seen != COLOUR_YELLOW){
    return 0;
  }
  
  if (check_line_is_black()) return 1;
  shift_color_sensor(1);
  return 0;
}

int isfirstStreet = 1;
int setup_black_lineup(int checkBothWays){
    // Extends the color sensor and rotates until the extended sensor is on black
    // Returns 0 if already on line, or -1/1 for the dir we used to reach the black
    if (is_lined_up_on_black()) {
        isfirstStreet = 0;
        return 0;
    }

    int initialAngle = BT_read_gyro_sensor(GYRO_INPUT);

    // Rotate slowly until alligned; checkBothWays=1 means switch directions if d_angle > 30
    int dir = 1;
    while (1){
        slight_robot_turn(dir * TURN_POWER);
        if (is_lined_up_on_black()){
            usleep(1000 * 125);
            isfirstStreet = 0;
            
            return dir;
        }

        if (checkBothWays && initialAngle != 1000){
            int newAngle = BT_read_gyro_sensor(GYRO_INPUT);
            if (abs(newAngle - initialAngle) > 30){
                initialAngle = 1000;
                dir *=-1;
            }
        }

        usleep(1000 * 25);
    }

}

int get_angle_for_black_in_dir(int dir){
    printf("Getting angle for d %d\n", dir);
    int cur_colour = getColourFromSensor();

    // Keep shifting until off black
    while (cur_colour == COLOUR_BLACK){
        slight_robot_turn(dir * TURN_POWER); 
        cur_colour = getColourFromSensor();
    }

    // retract a bit until back on black
    while (cur_colour != COLOUR_BLACK){
        slight_robot_turn(-dir * TURN_POWER);
        cur_colour = getColourFromSensor();
    }

    // Return that angle
    int ans = BT_read_gyro_sensor(GYRO_INPUT);
    printf("Angle is %d\n", ans);
    return ans;
}

void align_robot(int checkBothWays, int initialOrientationMode, int ignoreCentralization){
    // (checkBothWays, initialOrientationMode, ignoreCentralization) lines up robot along black line
    // checkBothWays = 0 means find the line by rotating one way, = 1 means try both, = 2 means we're already on black line
    // initialOrientationMode = 0 means we dont have one, -1/1 mean we're already on the right/left-most angle of the line
    // ignoreCentralization = 1 means dont bother centering the robot on the black line

    printf("Alligning robot\n");
    fflush(stdout);
    shift_color_sensor(1);
    
    int curOrientation = 0;
    if (checkBothWays < 2){
       curOrientation = setup_black_lineup(checkBothWays);
    }

    if (ignoreCentralization) return; // We dont care about averaging the angle here (probably going to turn)


    printf("Starting centralization\n");
    shift_color_sensor(1);
    int leftAngle = NULL; // Most counter-clockwise angle that retains this black line
    int rightAngle = NULL; // Most clockwise angle that retains this black line
    int curAngle = BT_read_gyro_sensor(GYRO_INPUT);

    // Figure out if we are already on an edge so we dont need to calculate it
    if (curOrientation == 1) leftAngle = curAngle;
    else if (curOrientation == -1) rightAngle = curAngle;
    else{
        if (initialOrientationMode == 1) leftAngle = curAngle;
        else if (initialOrientationMode == -1) rightAngle = curAngle;
    }
    
    if (leftAngle == NULL) leftAngle = get_angle_for_black_in_dir(-1);
    if (rightAngle == NULL) rightAngle = get_angle_for_black_in_dir(1);

    curAngle = BT_read_gyro_sensor(GYRO_INPUT);
    int wantedAngle =  (leftAngle + rightAngle)/2;

    while (abs(curAngle - wantedAngle) > 1){
        printf("Centralizing robot on black line, cur angle %d and wanted %d \n", curAngle, wantedAngle);
        int turnDir = wantedAngle > curAngle ? 1 : -1;
        slight_robot_turn(turnDir * TURN_POWER);
        curAngle = BT_read_gyro_sensor(GYRO_INPUT);
    }
}

void handle_out_of_bounds() {
  // Retract then keep moving back while extended is hitting red
  printf("Out of bounds - stage 1\n");
  fflush(stdout);
  
  BT_all_stop(0);
  shift_color_sensor(0);
  while (read_touch_robust(TOP_TOUCH_INPUT) == 0) {
    shift_sensor_until_color(COLOUR_RED, 1);
    BT_drive(LEFT_WHEEL_OUTPUT, RIGHT_WHEEL_OUTPUT, -FORWARD_POWER);
    usleep(1000*150);
    BT_all_stop(0);
    usleep(1000*150);
  }
  
  printf("Out of bounds - stage 2\n");
  fflush(stdout);
  
  // Move up until the extended is lined up with red
  shift_color_sensor(1);
  while (getColourFromSensor() != COLOUR_RED){
    BT_drive(LEFT_WHEEL_OUTPUT, RIGHT_WHEEL_OUTPUT, FORWARD_POWER/2);
    usleep(1000*150);
    BT_all_stop(0);
    usleep(1000*150);
  }
  
  printf("Out of bounds - stage 2.5\n");
  fflush(stdout);
  BT_drive(LEFT_WHEEL_OUTPUT, RIGHT_WHEEL_OUTPUT, FORWARD_POWER/3);
  usleep(1000*20);
  BT_all_stop(0);
  
  // Rotate until aligned with road
  printf("Out of bounds - stage 3\n");
  fflush(stdout);
  align_robot(0, 0, 0);
}


int find_street(void)   
{
 /*
  * This function gets your robot onto a street, wherever it is placed on the map. You can do this in many ways, but think
  * about what is the most effective and reliable way to detect a street and stop your robot once it's on it.
  * 
  * You can use the return value to indicate success or failure, or to inform the rest of your code of the state of your
  * bot after calling this function
  */   
 
  // Retract colour sensor, go backwards until we reach black
  printf("Looking for road or intersection\n");
  fflush(stdout);

  shift_color_sensor(0);
  if (getColourFromSensor() != COLOUR_BLACK && getColourFromSensor() != COLOUR_YELLOW){
    BT_drive(LEFT_WHEEL_OUTPUT, RIGHT_WHEEL_OUTPUT, -FORWARD_POWER);
    while (getColourFromSensor() != COLOUR_BLACK && getColourFromSensor() != COLOUR_YELLOW){}
  }
  BT_all_stop(0);

  printf("Finished finding road\n");
  fflush(stdout);
  return(0);
}

int drive_along_street(int lineUP)
{
 /*
  * This function drives your bot along a street, making sure it stays on the street without straying to other pars of
  * the map. It stops at an intersection.
  * 
  * You can implement this in many ways, including a controlled (PID for example), a neural network trained to track and
  * follow streets, or a carefully coded process of scanning and moving. It's up to you, feel free to consult your TA
  * or the course instructor for help carrying out your plan.
  * 
  * You can use the return value to indicate success or failure, or to inform the rest of your code of the state of your
  * bot after calling this function.
  */   

  // Goes to nearest street and lines up
  if (lineUP==1){
    find_street();
    align_robot(1 - isfirstStreet, 0, 0);
  }

  shift_color_sensor(0);
  printf("Driving on road\n");
  fflush(stdout);
  BT_drive(LEFT_WHEEL_OUTPUT, RIGHT_WHEEL_OUTPUT, FORWARD_POWER);
  
  while (1){
    int col = getColourFromSensor();
    if (col == COLOUR_BLACK || col == COLOUR_UNKNOWN) continue;
    
    // we're on something else, stop and figure it out
    BT_all_stop(0);
    
    // Check more rigorously 
    if (!(col == getColourFromSensor() && col == getColourFromSensor())){
      printf("Stopping because of colour but might be too early to tell\n");
      usleep(1000 * 100);
      BT_drive(LEFT_WHEEL_OUTPUT, RIGHT_WHEEL_OUTPUT, FORWARD_POWER);
      usleep(1000 * 10);
      continue;
    }
    
    // Act according to colour
    printf("Found something other than black/unknown\n");
    fflush(stdout);

    if (col == COLOUR_YELLOW) return 1; // we have reached an intersection
    if (col == COLOUR_RED) return 2; // we have reached an edge

    // we went out of bounds, fix up then go back on road
    find_street(); 
    align_robot(1, 0, 0);
    shift_color_sensor(0);
    BT_drive(LEFT_WHEEL_OUTPUT, RIGHT_WHEEL_OUTPUT, FORWARD_POWER);
  }

  return 0; // something is wrong
}

int turn_at_intersection(int turn_direction)
{
 /*
  * This function is used to have the robot turn either left or right at an intersection (obviously your bot can not just
  * drive forward!). 
  * 
  * If turn_direction=0, turn right, else if turn_direction=1, turn left.
  * 
  * You're free to implement this in any way you like, but it should reliably leave your bot facing the correct direction
  * and on a street it can follow. 
  * 
  * You can use the return value to indicate success or failure, or to inform your code of the state of the bot
  */

  // Allign robot then extend sensor and rotate until we're on black again and return intermediate colour
  //shift_color_sensor(1);

  printf("Initiating Turn\n");
  fflush(stdout);

  int expectedColour = COLOUR_BLACK;
  int seenWhite = 0;
  int seenBlue = 0;
  int seenGreen = 0;
  while (1){
    BT_all_stop(1);
    usleep(1000*150);
    int newReading;
    while (1){
      newReading = getColourFromSensor();
      if (newReading == getColourFromSensor()){
        break;
      }
    }
    

    //printf("Scanned colour %d, expected %d\n", newReading, expectedColour);
    //fflush(stdout);
    if (newReading == COLOUR_GREEN || newReading == COLOUR_BLUE || newReading == COLOUR_WHITE){
      if (newReading == COLOUR_GREEN) seenGreen+=1;
      if (newReading == COLOUR_BLUE) seenBlue+=1;
      if (newReading == COLOUR_WHITE) seenWhite+=1;
      expectedColour = newReading;
    }else if (newReading == COLOUR_BLACK && expectedColour != COLOUR_BLACK){
      int i = -1;
      if (seenGreen >= seenBlue && seenGreen >= seenWhite) i = COLOUR_GREEN;
      if (seenBlue >= seenGreen && seenBlue >= seenWhite) i = COLOUR_BLUE;
      if (seenWhite >= seenBlue && seenWhite >= seenGreen) i = COLOUR_WHITE;

      printf("Finsihed turn with mid colour %d\n", i);
      fflush(stdout);
      //BT_all_stop(0);
      return i;
    }

    //lastReading = newReading;
    BT_motor_port_start(LEFT_WHEEL_OUTPUT, TURN_POWER * turn_direction);
    BT_motor_port_start(RIGHT_WHEEL_OUTPUT, TURN_POWER * turn_direction * -1);
    usleep(1000*350);
  }

  return(0);
}

int scan_intersection(int *tl, int *tr, int *br, int *bl)
{
 /*
  * This function carries out the intersection scan - the bot should (obviously) be placed at an intersection for this,
  * and the specific set of actions will depend on how you designed your bot and its sensor. Whatever the process, you
  * should make sure the intersection scan is reliable - i.e. the positioning of the sensor is reliably over the buildings
  * it needs to read, repeatably, and as the robot moves over the map.
  * 
  * Use the APIs sensor reading calls to poll the sensors. You need to remember that sensor readings are noisy and 
  * unreliable so * YOU HAVE TO IMPLEMENT SOME KIND OF SENSOR / SIGNAL MANAGEMENT * to obtain reliable measurements.
  * 
  * Recall your lectures on sensor and noise management, and implement a strategy that makes sense. Document your process
  * in the code below so your TA can quickly understand how it works.
  * 
  * Once your bot has read the colours at the intersection, it must return them using the provided pointers to 4 integer
  * variables:
  * 
  * tl - top left building colour
  * tr - top right building colour
  * br - bottom right building colour
  * bl - bottom left building colour
  * 
  * The function's return value can be used to indicate success or failure, or to notify your code of the bot's state
  * after this call.
  */
 
  /************************************************************************************************************************
   *   TO DO  -   Complete this function
   ***********************************************************************************************************************/

  // Rotate 360 to record colours
  int colourScans[4];
  shift_color_sensor(1);
  for (int i =0; i<4; i++){
    colourScans[i] = turn_at_intersection(1);
    playBeep(colourScans[i]);
  }

  BT_motor_port_start(LEFT_WHEEL_OUTPUT, TURN_POWER);
  BT_motor_port_start(RIGHT_WHEEL_OUTPUT, TURN_POWER * -1);
  usleep(1000*500);
  BT_all_stop(1);

 // Return invalid colour values, and a zero to indicate failure (you will replace this with your code)
 *(tl)=colourScans[0];
 *(tr)=colourScans[1];
 *(br)=colourScans[2];
 *(bl)=colourScans[3];
 return(0);
}

int getIndexFromCoord(int x, int y) {
  return (x-1) + (sx*(y-1));
}

typedef struct coord {
  int x;
  int y;
  int invalid;
} coord;

typedef struct shiftdiff {
  int x;
  int y;
  double weight;
} shiftdiff;

double shiftBelief(int x, int y, int direction, int turn, double buff[][4]) {
  //default coords are vertical 
  shiftdiff main = {0, 1}, ld = {-1, 1}, rd = {1, 1}, df = {0, 2}; // main, left diag, right diag, double forward

  main.weight = 0.85;
  ld.weight = 0.05;
  rd.weight = 0.05;
  df.weight = 0.05; // add up to 1

  int shiftdir = (direction+turn)%4; 
  // Note the diffs are opposite to what we expect
  // This is since lower indicies mean higher and to the left!
  int coordswap, mult;
  // mult is what to multiply the diffs by, and coordswap is a flag to swap coords
  // note that all directions can be made using at most a swap and a sign flip
  if (shiftdir == 0) {
    mult = -1;
    coordswap = 0;
  } else if (shiftdir == 1) {
    mult = 1;
    coordswap = 1;
  } else if (shiftdir == 2) {
    mult = 1;
    coordswap = 0;
  } else {
    mult = -1;
    coordswap = 1;
  }

  shiftdiff diffs[4] = {main, ld, rd, df}; // Since these are copied by value, no more using the variable names

  int temp; // Swap var
  double sum = 0; // Sum of probs added to the buffer
  for (int i = 0; i < 4; i++) {
    if (coordswap) { // swap coords
      temp = diffs[i].x;
      diffs[i].x = diffs[i].y;
      diffs[i].y = temp;
    }

    diffs[i].x *= mult; // effects of mult
    diffs[i].y *= mult;

    if (x+diffs[i].x < 1 || y+diffs[i].y < 1 || y+diffs[i].y > sy || x+diffs[i].x > sx) {
      // out of bounds
    } else {
      // Applying the effects of weight here
      // This on turn applies the direction of the turn as wanted
      buff[getIndexFromCoord(x+diffs[i].x, y+diffs[i].y)][shiftdir] += beliefs[getIndexFromCoord(x,y)][direction]*diffs[i].weight;
      sum += beliefs[getIndexFromCoord(x,y)][direction]*diffs[i].weight; // add the weight to sum
    }
  }
  
  return sum; // total added to buffer
}

void shiftBeliefs(int turn) {
  double buff[400][4]; // use buffer since we dont want to iterate over already shifted values
  for (int i = 0; i < 400; i++) { // Clear contents
    for(int j = 0; j < 4; j++) {
      buff[i][j] = 0;
    }
  }

  double n = 0; // normalisation factor
  for (int d = 0; d < 4; d++) {
    for (int x = 1; x <= sx; x++) {
      for (int y = 1; y <= sy; y++) {
        n += shiftBelief(x, y, d, turn, buff);
      }
    }
  }

  for (int i = 0; i < 400; i++) { // Copy buff onto beliefs
    for(int j = 0; j < 4; j++) {
      beliefs[i][j] = buff[i][j]/n; // normalize
    }
  }
}

void printBeliefs(double b[][4]) {
  for (int d = 0; d < 4; d++) {
    printf("d: %d\n", d);
    for (int y = 1; y <= sy; y++) {
      for (int x = 1; x <= sx; x++) {
        //printf(beliefs[getIndexFromCoord(x,y)][d] == 0 ? " x " : " o ");
        printf(" %f ", b[getIndexFromCoord(x,y)][d]);
      }
      printf("\n");
    }
    printf("\n\n");
  }
}

void intHandler(int dummy) {
  BT_all_stop(0);
  exit(0);
}

void pushOffIntersection(void){
  //find_street();
  shift_color_sensor(0);
  while (1){
    BT_drive(LEFT_WHEEL_OUTPUT, RIGHT_WHEEL_OUTPUT, FORWARD_POWER);
    usleep(1000 * 100);
    BT_all_stop(0);
    usleep(1000 * 50);

    int pass = 0;
    for (int i=0; i<3; i++){
      if (!(getColourFromSensor() == COLOUR_YELLOW || getColourFromSensor() == COLOUR_UNKNOWN)) pass += 1;
    }
    if (pass==3)break;
    

  }
  BT_all_stop(0);
}

void pushBackOntoIntersection(void){
  //find_street();
  shift_color_sensor(0);
  while (1){
    BT_drive(LEFT_WHEEL_OUTPUT, RIGHT_WHEEL_OUTPUT, FORWARD_POWER);
    usleep(1000 * 100);
    BT_all_stop(0);
    usleep(1000 * 50);

    int pass = 0;
    for (int i=0; i<3; i++){
      if (getColourFromSensor() == COLOUR_YELLOW ) pass += 1;
    }
    if (pass==3)break;
    

  }
  BT_all_stop(0);
}


int check_still_on_intersect(int last_turn_dir){
    // After we've scanned all 4 colours, make sure we're still on the yellow
    // If we are, line up for the next one movement then push until off yellow
    // If we are not, get back on yellow/black and reallign then push until off yellow
    
    shift_color_sensor(0);
    if (getColourFromSensor() == COLOUR_YELLOW){
        // (checkBothWays, initialOrientationMode, ignoreCentralization) lines up robot along black line
        // checkBothWays = 0 means find the line by rotating one way, = 1 means try both, = 2 means we're already on black line
        // initialOrientationMode = 0 means we dont have one, -1/1 mean we're already on the right/left-most angle of the line
        // ignoreCentralization = 1 means dont bother centering the robot on the black line

        align_robot(2, last_turn_dir == 0 ? -1 : 1, 0);
        pushOffIntersection();
        return 1;
    }else{
        // It got skewed, restart
        printf("ROBOT SKEWED\n");
        pushBackOntoIntersection();
        align_robot(1, 0, 0);
        pushOffIntersection();
        return 1;
    }
}

int updateLocation(int *colours, int lastCommand, int *robot_x, int *robot_y, int *direction, int isFirst){
    if (!isFirst){
      shiftBeliefs(lastCommand);
    }

    // Calculate current probabilities 
    double colourPosibilities[sx * sy][4];
    for (int i = 0; i < sx * sy * 4; i++) colourPosibilities[i / 4][i % 4] = 0.01;
    
    printf("Determining location based on readings\n");
    for (int i = 0; i < sx; i++){
        for (int j = 0; j < sy; j++){
            int index = i + j*sx;

            // check if the colouring matches this
            // since we go counter clockwise, offset = 0 is LEFT, 1 is UP, 2 is RIGHT, 3 is DOWN
            // Shift this by 1, where 0 = UP, 1 = RIGHT, 2 = DOWN, 3 = LEFT
            for (int off = 0; off  < 4; off++){
                int matches = 1;
                for (int check = 0; check < 4; check++){
                    if (map[index][(check + off) % 4] != colours[check]){
                        matches = 0;
                        break;
                    }
                }

                if (matches){
                    //int off_ind = off == 0 ? 3 : off - 1;
                    colourPosibilities[index][off] += 0.95;
                    for (int extra_pos = 1; extra_pos < 4; extra_pos++){
                        colourPosibilities[index][(off + extra_pos)%4] += 0.05;
                    }

                    printf("MATCH: %d %d %d\n", i, j, off);
                }
            }
        }
    }


    // multiply probabilities
    for (int i = 0; i < sx; i++){
        for (int j = 0; j < sy; j++){
            int index = i + j*sx;
            for (int d = 0; d < 4; d++){
                beliefs[index][d] *= colourPosibilities[index][d];
            } 
        }
    }

    // Normalize the new beliefs
    double total = 0;
    for (int i = 0; i < sx * sy * 4; i++) total += beliefs[i / 4][i % 4];
    for (int i = 0; i < sx; i++){
        for (int j = 0; j < sy; j++){
            int index = i + j*sx;
            for (int d = 0; d < 4; d++){
                beliefs[index][d] = beliefs[index][d]/total;

                // Check if any belief is above the threshold of certainty
                if (beliefs[index][d] > THRESHOLD_OF_CERTAINTY){
                    printf("FINAL MATCH: %d %d %d\n", i, j, d);
                    *(robot_x) = i;
                    *(robot_y) = j;
                    *(direction) = d;

                    return 1;
                }
            } 
        }
    }

    return 0;
}


int robot_localization(int *robot_x, int *robot_y, int *direction)
{
 /*  This function implements the main robot localization process. You have to write all code that will control the robot
  *  and get it to carry out the actions required to achieve localization.
  *
  *  Localization process:
  *
  *  - Find the street, and drive along the street toward an intersection
  *  - Scan the colours of buildings around the intersection
  *  - Update the beliefs in the beliefs[][] array according to the sensor measurements and the map data
  *  - Repeat the process until a single intersection/facing direction is distintly more likely than all the rest
  * 
  *  * We have provided headers for the following functions:
  * 
  *  find_street()
  *  drive_along_street()
  *  scan_intersection()
  *  turn_at_intersection()
  * 
  *  You *do not* have to use them, and can write your own to organize your robot's work as you like, they are
  *  provided as a suggestion.
  * 
  *  Note that *your bot must explore* the map to achieve reliable localization, this means your intersection
  *  scanning strategy should not rely exclusively on moving forward, but should include turning and exploring
  *  other streets than the one your bot was initially placed on.
  * 
  *  For each of the control functions, however, you will need to use the EV3 API, so be sure to become familiar with
  *  it.
  * 
  *  In terms of sensor management - the API allows you to read colours either as indexed values or RGB, it's up to
  *  you which one to use, and how to interpret the noisy, unreliable data you're likely to get from the sensor
  *  in order to update beliefs.
  * 
  *  HOWEVER: *** YOU must document clearly both in comments within this function, and in your report, how the
  *               sensor is used to read colour data, and how the beliefs are updated based on the sensor readings.
  * 
  *  DO NOT FORGET - Beliefs should always remain normalized to be a probability distribution, that means the
  *                  sum of beliefs over all intersections and facing directions must be 1 at all times.
  * 
  *  The function receives as input pointers to three integer values, these will be used to store the estimated
  *   robot's location and facing direction. The direction is specified as:
  *   0 - UP
  *   1 - RIGHT
  *   2 - BOTTOM
  *   3 - LEFT
  * 
  *  The function's return value is 1 if localization was successful, and 0 otherwise.
  */
 
  /************************************************************************************************************************
   *   TO DO  -   Complete this function
   ***********************************************************************************************************************/

  int c = 0;
  int firstCall = 1;
  int lastAction = 0;
  signal(SIGINT, intHandler);
  
  int alreadyAligned = 0;

  while (1){
    // Drives until next intersection
    int status;
    if (alreadyAligned){
      alreadyAligned = 0;
      status = drive_along_street(0);
    }else{
      status = drive_along_street(1);

    }
    printf("Finished street with code %d\n", status);
    fflush(stdout);
    
    if (status == 1){
      align_robot(1, 0, 1); // Make sure we're properly lined up

      int tl, tr, br, bl;
      scan_intersection(&tl, &tr, &br, &bl);
      printf("Finished scanning with codes %d %d %d %d\n", tl, tr, br, bl);
      
      int colours[] = {bl, tl, tr, br};
      if (lastAction > -1){
        if (updateLocation(colours, lastAction, robot_x, robot_y, direction, firstCall)){
            // We're done!
            return 1;
        }
        firstCall = 0;
        printBeliefs(beliefs);
      }

      c = (c + 1)%2;
      if (c==0){
        // Rotate to keep scanning
        turn_at_intersection(-1);
        lastAction = 3;
      }else{
        lastAction = 0;
      }
    
      // Line up and get off intersection
      int result = check_still_on_intersect(c);
      if (result==1) alreadyAligned=1;
      
    }else if (status == 2){
      handle_out_of_bounds();
      lastAction = -1;

      // maybe do something for localization
    }
  }
  
  
 // Return an invalid location/direction and notify that localization was unsuccessful (you will delete this and replace it
 // with your code).
 *(robot_x)=-1;
 *(robot_y)=-1;
 *(direction)=-1;
 return(0);
 
}

void go_x_intersections(int nums){
  int c = -1;
  while (1){
    c+= 1;
    printf("INTERSECT #%d\n", c);
    if (c == nums){
      return;
    }

    align_robot(1, 0, 0); // Make sure we're properly lined up
    // Push off yellow
    pushOffIntersection();
    int status;
    status = drive_along_street(1);
    printf("Finished street with code %d\n", status);
    fflush(stdout);
  }
}

int go_to_target(int robot_x, int robot_y, int direction, int target_x, int target_y)
{
 /*
  * This function is called once localization has been successful, it performs the actions required to take the robot
  * from its current location to the specified target location. 
  *
  * You have to write the code required to carry out this task - once again, you can use the function headers provided, or
  * write your own code to control the bot, but document your process carefully in the comments below so your TA can easily
  * understand how everything works.
  *
  * Your code should be able to determine if the robot has gotten lost (or if localization was incorrect), and your bot
  * should be able to recover.
  * 
  * Inputs - The robot's current location x,y (the intersection coordinates, not image pixel coordinates)
  *          The target's intersection location
  * 
  * Return values: 1 if successful (the bot reached its target destination), 0 otherwise
  */   

  /************************************************************************************************************************
   *   TO DO  -   Complete this function
   ***********************************************************************************************************************/
  if (robot_x != target_x){
    int wantedDir;
    if (robot_x > target_x){
      wantedDir = 3;
    }else{
      wantedDir = 1;
    }

    while (direction != wantedDir){
      direction = (direction + 1) % 4;
      turn_at_intersection(1);
    }

    go_x_intersections(fabs(robot_x - target_x));
  }

  if (robot_y != target_y){
    int wantedDir;
    if (robot_y > target_y){
      wantedDir = 0;
    }else{
      wantedDir = 2;
    }

    while (direction != wantedDir){
      direction = (direction + 1) % 4;
      turn_at_intersection(1);
    }

    go_x_intersections(fabs(robot_y - target_y));
  }
  
  return(0);  
}

const char* color_from_int(int color) {
  switch (color)
  {
    case 1:
      return "black";
    case 2:
      return "blue";
    case 3:
      return "green";
    case 4:
      return "yellow";
    case 5:
      return "red";
    case 6:
      return "white";
    case 7:
      return "unknown";
  }

  return NULL;
}

#define MAX_COLOR_READING 500

void calibrate_sensor(void)
{
 /*
  * This function is called when the program is started with -1  -1 for the target location. 
  *
  * You DO NOT NEED TO IMPLEMENT ANYTHING HERE - but it is strongly recommended as good calibration will make sensor
  * readings more reliable and will make your code more resistent to changes in illumination, map quality, or battery
  * level.
  * 
  * The principle is - Your code should allow you to sample the different colours in the map, and store representative
  * values that will help you figure out what colours the sensor is reading given the current conditions.
  * 
  * Inputs - None
  * Return values - None - your code has to save the calibration information to a file, for later use (see in main())
  * 
  * How to do this part is up to you, but feel free to talk with your TA and instructor about it!
  */   

  /************************************************************************************************************************
   *   OIPTIONAL TO DO  -   Complete this function
   ***********************************************************************************************************************/
  fprintf(stderr,"Calibration function called!\n");  
  BT_open(HEXKEY);
  FILE* f = fopen("./calibration", "w");
  colorReading c[10];
  int val[3], n;
  for (int i = 1; i < 7; i++) {
    printf("Scanning color %s\n", color_from_int(i));
    
    for (int k = 0; k < 3; k++) { // for each spot
      n = 0; // init

      printf("Push button when ready\n");
      while (!read_touch_robust(TOP_TOUCH_INPUT)){} // wait for push
      while (n < 10){ // 10 samples per spot
        normalized_color_read(val);
        if (val[0] > MAX_COLOR_READING || val[1] > MAX_COLOR_READING || val[2] > MAX_COLOR_READING) { continue; }
        
        c[n].r = val[0];
        c[n].g = val[1];
        c[n].b = val[2];
        c[n].color = i;
        
        n++; // update sums

        printf("Values: %d %d %d\n", val[0], val[1], val[2]);
        usleep(1000*100); // 100 ms
      }

      fwrite(c, sizeof(colorReading), 10, f); 
    } // 30 per color
  }

  
  fclose(f);
}

int parse_map(unsigned char *map_img, int rx, int ry)
{
 /*
   This function takes an input image map array, and two integers that specify the image size.
   It attempts to parse this image into a representation of the map in the image. The size
   and resolution of the map image should not affect the parsing (i.e. you can make your own
   maps without worrying about the exact position of intersections, roads, buildings, etc.).

   However, this function requires:
   
   * White background for the image  [255 255 255]
   * Red borders around the map  [255 0 0]
   * Black roads  [0 0 0]
   * Yellow intersections  [255 255 0]
   * Buildings that are pure green [0 255 0], pure blue [0 0 255], or white [255 255 255]
   (any other colour values are ignored - so you can add markings if you like, those 
    will not affect parsing)

   The image must be a properly formated .ppm image, see readPPMimage below for details of
   the format. The GIMP image editor saves properly formatted .ppm images, as does the
   imagemagick image processing suite.
   
   The map representation is read into the map array, with each row in the array corrsponding
   to one intersection, in raster order, that is, for a map with k intersections along its width:
   
    (row index for the intersection)
    
    0     1     2    3 ......   k-1
    
    k    k+1   k+2  ........    
    
    Each row will then contain the colour values for buildings around the intersection 
    clockwise from top-left, that is
    
    
    top-left               top-right
            
            intersection
    
    bottom-left           bottom-right
    
    So, for the first intersection (at row 0 in the map array)
    map[0][0] <---- colour for the top-left building
    map[0][1] <---- colour for the top-right building
    map[0][2] <---- colour for the bottom-right building
    map[0][3] <---- colour for the bottom-left building
    
    Color values for map locations are defined as follows (this agrees with what the
    EV3 sensor returns in indexed-colour-reading mode):
    
    1 -  Black
    2 -  Blue
    3 -  Green
    4 -  Yellow
    5 -  Red
    6 -  White
    
    If you find a 0, that means you're trying to access an intersection that is not on the
    map! Also note that in practice, because of how the map is defined, you should find
    only Green, Blue, or White around a given intersection.
    
    The map size (the number of intersections along the horizontal and vertical directions) is
    updated and left in the global variables sx and sy.

    Feel free to create your own maps for testing (you'll have to print them to a reasonable
    size to use with your bot).
    
 */    
 
 int last3[3];
 int x,y;
 unsigned char R,G,B;
 int ix,iy;
 int bx,by,dx,dy,wx,wy;         // Intersection geometry parameters
 int tgl;
 int idx;
 
 ix=iy=0;       // Index to identify the current intersection
 
 // Determine the spacing and size of intersections in the map
 tgl=0;
 for (int i=0; i<rx; i++)
 {
  for (int j=0; j<ry; j++)
  {
   R=*(map_img+((i+(j*rx))*3));
   G=*(map_img+((i+(j*rx))*3)+1);
   B=*(map_img+((i+(j*rx))*3)+2);
   if (R==255&&G==255&&B==0)
   {
    // First intersection, top-left pixel. Scan right to find width and spacing
    bx=i;           // Anchor for intersection locations
    by=j;
    for (int k=i; k<rx; k++)        // Find width and horizontal distance to next intersection
    {
     R=*(map_img+((k+(by*rx))*3));
     G=*(map_img+((k+(by*rx))*3)+1);
     B=*(map_img+((k+(by*rx))*3)+2);
     if (tgl==0&&(R!=255||G!=255||B!=0))
     {
      tgl=1;
      wx=k-i;
     }
     if (tgl==1&&R==255&&G==255&&B==0)
     {
      tgl=2;
      dx=k-i;
     }
    }
    for (int k=j; k<ry; k++)        // Find height and vertical distance to next intersection
    {
     R=*(map_img+((bx+(k*rx))*3));
     G=*(map_img+((bx+(k*rx))*3)+1);
     B=*(map_img+((bx+(k*rx))*3)+2);
     if (tgl==2&&(R!=255||G!=255||B!=0))
     {
      tgl=3;
      wy=k-j;
     }
     if (tgl==3&&R==255&&G==255&&B==0)
     {
      tgl=4;
      dy=k-j;
     }
    }
    
    if (tgl!=4)
    {
     fprintf(stderr,"Unable to determine intersection geometry!\n");
     return(0);
    }
    else break;
   }
  }
  if (tgl==4) break;
 }
  fprintf(stderr,"Intersection parameters: base_x=%d, base_y=%d, width=%d, height=%d, horiz_distance=%d, vertical_distance=%d\n",bx,by,wx,wy,dx,dy);

  sx=0;
  for (int i=bx+(wx/2);i<rx;i+=dx)
  {
   R=*(map_img+((i+(by*rx))*3));
   G=*(map_img+((i+(by*rx))*3)+1);
   B=*(map_img+((i+(by*rx))*3)+2);
   if (R==255&&G==255&&B==0) sx++;
  }

  sy=0;
  for (int j=by+(wy/2);j<ry;j+=dy)
  {
   R=*(map_img+((bx+(j*rx))*3));
   G=*(map_img+((bx+(j*rx))*3)+1);
   B=*(map_img+((bx+(j*rx))*3)+2);
   if (R==255&&G==255&&B==0) sy++;
  }
  
  fprintf(stderr,"Map size: Number of horizontal intersections=%d, number of vertical intersections=%d\n",sx,sy);

  // Scan for building colours around each intersection
  idx=0;
  for (int j=0; j<sy; j++)
   for (int i=0; i<sx; i++)
   {
    x=bx+(i*dx)+(wx/2);
    y=by+(j*dy)+(wy/2);
    
    fprintf(stderr,"Intersection location: %d, %d\n",x,y);
    // Top-left
    x-=wx;
    y-=wy;
    R=*(map_img+((x+(y*rx))*3));
    G=*(map_img+((x+(y*rx))*3)+1);
    B=*(map_img+((x+(y*rx))*3)+2);
    if (R==0&&G==255&&B==0) map[idx][0]=3;
    else if (R==0&&G==0&&B==255) map[idx][0]=2;
    else if (R==255&&G==255&&B==255) map[idx][0]=6;
    else fprintf(stderr,"Colour is not valid for intersection %d,%d, Top-Left RGB=%d,%d,%d\n",i,j,R,G,B);

    // Top-right
    x+=2*wx;
    R=*(map_img+((x+(y*rx))*3));
    G=*(map_img+((x+(y*rx))*3)+1);
    B=*(map_img+((x+(y*rx))*3)+2);
    if (R==0&&G==255&&B==0) map[idx][1]=3;
    else if (R==0&&G==0&&B==255) map[idx][1]=2;
    else if (R==255&&G==255&&B==255) map[idx][1]=6;
    else fprintf(stderr,"Colour is not valid for intersection %d,%d, Top-Right RGB=%d,%d,%d\n",i,j,R,G,B);

    // Bottom-right
    y+=2*wy;
    R=*(map_img+((x+(y*rx))*3));
    G=*(map_img+((x+(y*rx))*3)+1);
    B=*(map_img+((x+(y*rx))*3)+2);
    if (R==0&&G==255&&B==0) map[idx][2]=3;
    else if (R==0&&G==0&&B==255) map[idx][2]=2;
    else if (R==255&&G==255&&B==255) map[idx][2]=6;
    else fprintf(stderr,"Colour is not valid for intersection %d,%d, Bottom-Right RGB=%d,%d,%d\n",i,j,R,G,B);
    
    // Bottom-left
    x-=2*wx;
    R=*(map_img+((x+(y*rx))*3));
    G=*(map_img+((x+(y*rx))*3)+1);
    B=*(map_img+((x+(y*rx))*3)+2);
    if (R==0&&G==255&&B==0) map[idx][3]=3;
    else if (R==0&&G==0&&B==255) map[idx][3]=2;
    else if (R==255&&G==255&&B==255) map[idx][3]=6;
    else fprintf(stderr,"Colour is not valid for intersection %d,%d, Bottom-Left RGB=%d,%d,%d\n",i,j,R,G,B);
    
    fprintf(stderr,"Colours for this intersection: %d, %d, %d, %d\n",map[idx][0],map[idx][1],map[idx][2],map[idx][3]);
    
    idx++;
   }

 return(1);  
}

unsigned char *readPPMimage(const char *filename, int *rx, int *ry)
{
 // Reads an image from a .ppm file. A .ppm file is a very simple image representation
 // format with a text header followed by the binary RGB data at 24bits per pixel.
 // The header has the following form:
 //
 // P6
 // # One or more comment lines preceded by '#'
 // 340 200
 // 255
 //
 // The first line 'P6' is the .ppm format identifier, this is followed by one or more
 // lines with comments, typically used to inidicate which program generated the
 // .ppm file.
 // After the comments, a line with two integer values specifies the image resolution
 // as number of pixels in x and number of pixels in y.
 // The final line of the header stores the maximum value for pixels in the image,
 // usually 255.
 // After this last header line, binary data stores the RGB values for each pixel
 // in row-major order. Each pixel requires 3 bytes ordered R, G, and B.
 //
 // NOTE: Windows file handling is rather crotchetty. You may have to change the
 //       way this file is accessed if the images are being corrupted on read
 //       on Windows.
 //

 FILE *f;
 unsigned char *im;
 char line[1024];
 int i;
 unsigned char *tmp;
 double *fRGB;

 im=NULL;
 f=fopen(filename,"rb+");
 if (f==NULL)
 {
  fprintf(stderr,"Unable to open file %s for reading, please check name and path\n",filename);
  return(NULL);
 }
 fgets(&line[0],1000,f);
 if (strcmp(&line[0],"P6\n")!=0)
 {
  fprintf(stderr,"Wrong file format, not a .ppm file or header end-of-line characters missing\n");
  fclose(f);
  return(NULL);
 }
 fprintf(stderr,"%s\n",line);
 // Skip over comments
 fgets(&line[0],511,f);
 while (line[0]=='#')
 {
  fprintf(stderr,"%s",line);
  fgets(&line[0],511,f);
 }
 sscanf(&line[0],"%d %d\n",rx,ry);                  // Read image size
 fprintf(stderr,"nx=%d, ny=%d\n\n",*rx,*ry);

 fgets(&line[0],9,f);  	                // Read the remaining header line
 fprintf(stderr,"%s\n",line);
 im=(unsigned char *)calloc((*rx)*(*ry)*3,sizeof(unsigned char));
 if (im==NULL)
 {
  fprintf(stderr,"Out of memory allocating space for image\n");
  fclose(f);
  return(NULL);
 }
 fread(im,(*rx)*(*ry)*3*sizeof(unsigned char),1,f);
 fclose(f);

 return(im);    
}
