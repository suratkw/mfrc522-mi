cmd_Release/obj.target/mfrc522-mi.node := g++ -shared -pthread -rdynamic  -Wl,-soname=mfrc522-mi.node -o Release/obj.target/mfrc522-mi.node -Wl,--start-group Release/obj.target/mfrc522-mi/src/MFRC522.o Release/obj.target/mfrc522-mi/src/accessor.o -Wl,--end-group -lwiringPi