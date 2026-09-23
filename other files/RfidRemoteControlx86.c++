//============================================================================

//============================================================================

#include <iostream>
using namespace std;
/*
#define USECANCELTIMEOUT
#include <x86/monoTimer.h>
#include <x86/monoTicker.h>

static inline uint64_t millis64(){	return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();};

void digitalWrite(unsigned int pin, unsigned int value){};

#define x86BUILD



#include <x86/SimpleFSx86.h>

#include <x86/CurWebServerx86.h>
#include <x86/WifiManx86.h>
#include <x86/Sensorx86.h>

#define boolean bool
*/
#include "RfidRemoteControl.ino"




int main() {
	std::cout << "RfidRemoteControlx86  v0.01 "<<std::endl;

	setup();

	// x86 timer and server maintaining
	bool first=true;
	while(1){
		if(first) {std::cout << "RfidRemoteControlx86 loop started " << monomillis() << std::endl;first=false;}
		//or IOFactory::yield();
		loop();
		//	 std::cout << ". " << monomillis() << std::endl;
		delay(100);
		//		*	std::cout << "millis 2 " << monomillis() << std::endl;
		//	std::cout << std::endl;
	}

	std::cout << "RfidRemoteControlx86 ended"<<std::endl;
	//end();	//cleanup
	return 0;
}
