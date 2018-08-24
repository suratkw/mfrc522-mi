#include <node.h>
#include <v8.h>
#include <unistd.h>

#include <stdio.h>
#include <iostream>
#include <errno.h>
#include <wiringPiSPI.h>
#include <unistd.h>
//#include "Desfire.h"
#include "MFRC522.h"


uint8_t initRfidReader(void);
char uid[23];
char last_uid[23];
char *p;

using namespace v8;
using namespace std;

#define RST_PIN         6          // Configurable, see typical pin layout above
#define SS_PIN          10         // Configurable, see typical pin layout above

MFRC522 mfrc522(SS_PIN, RST_PIN);  // Create MFRC522 instance


void RunCallback(const FunctionCallbackInfo<Value>& args) {
    Isolate* isolate = Isolate::GetCurrent();
    HandleScope scope(isolate);

    Local<Function> callback = Local<Function>::Cast(args[0]);
    const unsigned argc = 1;

    for (;;) {
    	usleep(500000);
    	p = uid;
		// Look for new cards, and select one if present
		if ( ! mfrc522.PICC_IsNewCardPresent()){
			//printf(">>>>>>>>>>>>>>nocard\n");
			sprintf(uid, "nocard");
		}else{

			if (! mfrc522.PICC_ReadCardSerial()) {
	//			printf(">>>>>>>>>>>>>>read error %d\n",c);
				sprintf(uid, "nocard");
			}else{
				// Dump UID
				for (byte i = 0; i < mfrc522.uid.size; i++) {
					sprintf(p, "%02X", mfrc522.uid.uidByte[i]);
					p += 2;
				}			
			}
		
		}



		//printf("uid:%s  last_uid:%s %d\n", uid, last_uid, strcmp(last_uid, uid));
		
		mfrc522.PICC_IsNewCardPresent();    
    
    
        // Only when the serial number of the currently detected tag differs from the
        // recently detected tag the callback will be executed with the serial number
        
        
        if (strcmp(last_uid, uid) != 0) {
            Local<Value> argv[argc] = {
                String::NewFromUtf8(isolate, &uid[0])
            };

            callback->Call(isolate->GetCurrentContext()->Global(), argc, argv);
        }

        // Preserves the current detected serial number, so that it can be used
        // for future evaluations
        strcpy(last_uid, uid);
        *(p++) = 0;
    }

}

void Init(Handle<Object> exports, Handle<Object> module) {
    initRfidReader();
    NODE_SET_METHOD(module, "exports", RunCallback);
}

uint8_t initRfidReader(void) {
    wiringPiSetup () ;
	mfrc522.PCD_Init();   // Init MFRC522
//	mfrc522.PCD_DumpVersionToSerial();  // Show details of PCD - MFRC522 Card Reader details
	
    return 0;
}

NODE_MODULE(rc522, Init)