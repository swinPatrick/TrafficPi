#include <stdio.h>
#include <pigpio.h>
/*
 * PROGRAM OVERVIEW
 * 	Each control input is set up with an Alert function which will call
 * 		gpioSetTimerFunc() with the desired interval the lights will
 * 		change, and will also change the step taken after each iteration.
 * 		The end result is a responsive system that uses a simple method
 * 		to change the current light.
*/

/*
    CHANGE SPEED VALUES HERE
    (time between changes in milliseconds)
*/
#define slowSpeed 5000 // 5 seconds
#define mediumSpeed 2000 // 2 seconds
#define fastSpeed 1000 // 1 second

/*
TrafficPi control settings:
(left -> right)

0) random(party mode?)
1) downwards roation slow
2) downwards rotation medium
3) downwards rotation fast
4) upwards rotation slow
5) upwards rotation medium
6) upwards rotation fast
7) flashing slow
8) flashing medium
9) flashing fast
*/

/*
 ***************************************
 ******** GPIO port connections ********
 ***************************************
 * 	Connection		 GPIO	Connection
 * 	[3V3]			-	-	[5V]
 * 	redLight		2	-	[5V]
 * 	amberLight		3	-	[GROUND]
 * 	greenLight		4	14
 * 	[GROUND]		-	15
 * 	redBiasOn		17	18
 * 	redBiasOff		27	-	[GROUND]
 * 	amberBiasOn		22	23
 * 	[GROUND]		-	24
 * 	amberBiasOff		10	-	[GROUND]
 * 	greenBiasOn		9	25
 * 	greenBiasOff		11	8
 * 	[GROUND]		-	7
 * 	modeRand		0	1
 * 	modeDownSlow		5	-	[GROUND]
 * 	modeDownMedium		6	12	modeUpFast
 * 	modeDownFast		13	-	[GROUND]
 * 	modeUpSlow		19	16	modeFlashSlow
 * 	modeUpMedium		26	20	modeFlashMedium
 * 	[GROUND]		-	21	modeFlashFast
*/

#define redLight 2
#define amberLight 3
#define greenLight 4
int Lights[] = { redLight, amberLight, greenLight };

#define redOnBias 17
#define redOffBias 27
#define amberOnBias 22
#define amberOffBias 10
#define greenOnBias 9
#define greenOffBias 11
int OnBias[] = { redOnBias, amberOnBias, greenOnBias };
int OffBias[] = { redOffBias, amberOffBias, greenOffBias };



#define modeRand 0
#define modeDownSlow 5
#define modeDownMedium 6
#define modeDownFast 13
#define modeUpSlow 19
#define modeUpMedium 26
#define modeUpFast 12
#define modeFlashSlow 16
#define modeFlashMedium 20
#define modeFlashFast 21
int Modes{
    modeRand, 
    modeDownSlow, modeDownMedium, modeDownFast,
    modeUpSlow, modeUpMedium, modeUpFast,
    modeFlashSlow, modeFlashMedium, modeFlashFast
    }

/*
	gpioSetTimerFunc(); //request a regular timed callback
	gpioSetAlertFunc(); //request a GPIO level change callback

* TIMER
* 	To set a timer for the first time simply use
* 		gpioSetTimerFunc(timer, time, function)
* 	To give a timer a new function/time, first cancel the timer function
* 		by using NULL as the function, then create the new timer with
* 		the desired parameters.
*
* ALERT
* 	To set up an alert functio simply use gpioAlertFunction(input, callback)
* 	The callback will be passed the GPIO, the new level, and the tick.
* 	int GPIO: the GPIO which has changed state (0-31)
* 	int level: 	0 = change to low (falling edge)
* 				1 = change to high (rising edge)
* 				2 = no level change (a watchdog timeout)
* 	uint32_t tick: the number of microseconds since boot
*/

/*
 * These masks reflect the state of the bias switches and 
 * are updates from updateOnBias() and updateOffBias()
*/
int BIAS_ON_MASK = 0b000; 
int BIAS_OFF_MASK = 0b111;

// Keeps track of which stage the light cycle is at
int Sequence_Stage = 0b100; //start on red

/*
 * Function called when an ON bias switch is changed
 * we can ignore what pin called the function and simply update the mask
*/
void updateOnBias(int _pin, int _level, uint32_t _tick) {
    switch (_level)
    {
    case 0:
    case 1: // bias switch turned on or off
        // for the ON bias, Mask should match request (ON = 1)
        BIAS_ON_MASK = gpioRead(redOnBias) << gpioRead(amberOnBias) << gpioRead(greenOnBias);
        break;

    default:
    case 2: // no change
        break;
    }
}

/*
 * Function called when an OFF bias switch is changed
 * we can ignore what pin called the function and simply update the mask.
 * each bit should be the opposite of the bias switch reads.
*/
void updateOffBias(int _pin, int _level, uint32_t _tick) {
    switch (_level)
    {
    case 0:
    case 1: // bias switch turned on or off
        // for the OFF bias, Mask should be opposite of request (ON = 0)
        BIAS_OFF_MASK = ~gpioRead(redOffBias) << ~gpioRead(amberOffBias) << ~gpioRead(greenOffBias);
        break;

    default:
    case 2: // no change
        break;
    }
}

/*
 * use last 3 bits of request to turn lights on/off
 * red << amber << green
*/
void updateLights(uint8_t _outputRequest)
{
    _outputRequest = _outputRequest & 0b111; // only get last 3 bits
    gpioWrite(redLight, (_outputRequest & 0b100) >> 2); //mask for red then move bit
    gpioWrite(amberLight, (_outputRequest & 0b010) >> 1); //mask for amber then moe bit
    gpioWrite(greenLight, (_outputRequest & 0b001)); //mask for green
}

// define a type (functions without inputs)
typedef void (*DirectionFunction) (void);

// position of RotateDown() in the dirction funtion array
#define rotateDown 0
// RotateDown() rotates the lights downwards, turning one on at a time before bias
// Sequence_Stage is a global variable that keeps trck of what the last position was so that other rotate functions can use it
void RotateDown(void) {
    Sequence_Stage = rightRotate(Sequence_Step); //take a step
    int output = Sequence_Step; //use copy of current step
    output = output | BIAS_ON_MASK; //apply ON bias(s)
    output = output & BIAS_OFF_MASK;//apply OFF bias(s)
    updateLights(output); //send to lights
}

// position of RotateUp() in the dirction funtion array
#define rotateUp 1
// RotateUp() rotates the lights downwards, turning one on at a time before bias
// Sequence_Stage is a global variable that keeps trck of what the last position was so that other rotate functions can use it
void RotateUp(void) {
    Sequence_Stage = leftRotate(Sequence_Step); //take a step
    int output = Sequence_Step; //use copy of current step
    output = output | BIAS_ON_MASK; //apply ON bias(s)
    output = output & BIAS_OFF_MASK;//apply OFF bias(s)
    updateLights(output); //send to lights
}

// position of RotateNone() in the dirction funtion array
#define rotateNone 2
// RotateNone() is flashing all lights on then off, so need to keep track of what it was last
void RotateNone(void) {
    static int flash_step = 0b111; 
    flash_step = ~flash_step; //take step
    int output = flash_step; // take copy
    output = output | BIAS_ON_MASK; //apply ON bias
    output = output & BIAS_OFF_MASK; //apply OFF bias
    updateLights(output); //update lights
}

/*
 * Called when the mode dial has changed. resets timer to new condition when called
*/
void updateTimerMode(int _pin, int _level, uint32_t _tick) 
{
    static int _speed = mediumSpeed; // controlls time between changes
    static int _rotation = rotateDown; // controlls what function is called at interval
    // create array of functions the options can choose from
    DirectionFunction _directionFunction[] = { RotateDown, RotateUp, RotateNone };

    gpioSetTimerFunc(0, 10, NULL); //Stop current timer

    // Only looking for _level == 1 since that means a new mode has been selected
    if (_level != 1)
        return;

    // defines what each mode changes
    switch (_pin)
    {
    default:
        break;
    case modeRand:
        break;

    case modeDownSlow:
        _speed = slowSpeed;
        _rotation = rotateDown;
        break;
    case modeDownMedium:
        _speed = mediumSpeed;
        _rotation = rotateDown;
        break;
    case modeDownFast:
        _speed = fastSpeed;
        _rotation = rotateDown;
        break;

    case modeUpSlow:
        _speed = slowSpeed;
        _rotation = rotateUp;
        break;
    case modeUpMedium:
        _speed = mediumSpeed;
        _rotation = rotateUp;
        break;
    case modeUpFast:
        _speed = fastSpeed;
        _rotation = rotateUp;
        break;

    case modeFlashSlow:
        _speed = slowSpeed;
        _rotation = rotateNone;
        break;
    case modeFlashMedium:
        _speed = mediumSpeed;
        _rotation = rotateNone;
        break;
    case modeFlashFast:
        _speed = fastSpeed;
        _rotation = rotateNone;
        break;
    }
    
    gpioSetTimerFunc(0, _speed, _directionFunction[_rotation]); //start timer with new conditions
}



void setup() {
    // set Lights as outputs
    for (int i = 0; i < sizeof(Lights); i++) {
        gpioSetMode(Lights[i], PI_OUTPUT);
        gpioSetPullUpDown(Lights[i], PI_PUD_UP)
    }

    // bias switches (input, pin pull up, call function to apply changes)
    for (int i = 0; i < sizeof(OnBias); i++) {
        gpioSetMode(OnBias[i], PI_INPUT);
        gpioSetPullUpDown(OnBias[i], PI_PUD_UP);
        gpioSetAlertFunc(OnBias[i], updateOnBias);

        // since sizeof(OnBias) == sizeof(OffBias)
        gpioSetMode(OffBias[i], PI_INPUT);
        gpioSetPullUpDown(OffBias[i], PI_PUD_UP);
        gpioSetAlertFunc(OffBias[i], updateOffBias);
    }

    // sequence mode selecters (input, pin pull up, function changes timer)
    for (int i = 0; i < sizeof(Modes); i++) {
        gpioSetMode(Modes[i], PI_INPUT);
        gpioSetPullUpDown(Modes[i], PI_PUD_UP);
        gpioSetAlertFunc(Modes[i], updateTimerMode);
    }

    //Set current mode to reflect selecter
    for each (int i in Modes) //read input pins for mode selection
    {
        if (!gpioRead(i))
            continue;
        else //only 1 pin that is high can be selected
        {
            updateTimerMode(i, 1, 1234);
            break;
        }
    }
}

void loop() {
	// this is an inturrpt based program.
    __noop; //no operation
}


int main(void)//(int argc, char **argv)
{
	if (wiringPiSetup() < 0) {
		cout << "setup wiring pi failed" << endl;
		return 1;
	}
	setup();
	while (1) { //keep program running
		loop();
	}
	gpioTerminate();

	return 0;
}


// rotate bits right by 1
int rightRotate(int n)
{
    unsigned int d = 1;
    /* In n>>d, first d bits are 0.
    To put last 3 bits of at
    first, do bitwise or of n>>d
    with n <<(INT_BITS - d) */
    return (n >> d) | (n << (INT_BITS - d));
}


// rotate bits left by 1
int leftRotate(int n)
{
    unsigned int d = 1
        /* In n<<d, last d bits are 0. To
         put first 3 bits of n at
        last, do bitwise or of n<<d
        with n >>(INT_BITS - d) */
        return (n << d) | (n >> (INT_BITS - d));
}
