var rc522 = require('./build/Release/mfrc522-mi');

rc522(function(rfidTagSerialNumber) {
	console.log(rfidTagSerialNumber);
});