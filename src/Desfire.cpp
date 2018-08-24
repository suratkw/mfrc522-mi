#include "Desfire.h"

MFRC522::StatusCode DESFire::PICC_RequestATS(byte *atsBuffer, byte *atsLength)
{
	MFRC522::StatusCode result;

	// Build command buffer
	atsBuffer[0] = 0xE0; //PICC_CMD_RATS;
	atsBuffer[1] = 0x50; // FSD=64, CID=0

	// Calculate CRC_A
	result = PCD_CalculateCRC(atsBuffer, 2, &atsBuffer[2]);
	if (result != STATUS_OK) {
		return result;
	}

	// Transmit the buffer and receive the response, validate CRC_A.
	result = PCD_TransceiveData(atsBuffer, 4, atsBuffer, atsLength, NULL, 0, true);
	if (result != STATUS_OK) {
		PICC_HaltA();
		printf("WTF???\n");
		return result;
	}

	return result;
} // End PICC_RequestATS()

  /**
  * Transmits Protocol and Parameter Selection Request (PPS)
  *
  * @return STATUS_OK on success, STATUS_??? otherwise.
  */
MFRC522::StatusCode DESFire::PICC_ProtocolAndParameterSelection(byte cid,	///< The lower nibble indicates the CID of the selected PICC in the range of 0x00 and 0x0E
                                                                byte pps0,	///< PPS0
	                                                            byte pps1	///< PPS1
) {
	MFRC522::StatusCode result;

	byte ppsBuffer[5];
	byte ppsBufferSize = 5;
	ppsBuffer[0] = 0xD0 | (cid & 0x0F);
	ppsBuffer[1] = pps0;
	ppsBuffer[2] = pps1;

	// Calculate CRC_A
	result = PCD_CalculateCRC(ppsBuffer, 3, &ppsBuffer[3]);
	if (result != STATUS_OK) {
		return result;
	}

	// Transmit the buffer and receive the response, validate CRC_A.
	result = PCD_TransceiveData(ppsBuffer, 5, ppsBuffer, &ppsBufferSize, NULL, 0, true);
	if (result == STATUS_OK) {
		// This is how my MFRC522 is by default.
		// Reading https://www.nxp.com/documents/data_sheet/MFRC522.pdf it seems CRC generation can only be disabled in this mode.
		if (pps1 == 0x00) {
			PCD_WriteRegister(TxModeReg, 0x00);
			PCD_WriteRegister(RxModeReg, 0x00);
		}
	}

	return result;
} // End PICC_ProtocolAndParameterSelection()

/**
 * @see MIFARE_BlockExchangeWithData()
 */
DESFire::StatusCode DESFire::MIFARE_BlockExchange(mifare_desfire_tag *tag, byte cmd, byte *backData, byte *backLen)
{
	return MIFARE_BlockExchangeWithData(tag, cmd, NULL, NULL, backData, backLen);
} // End MIFARE_BlockExchange()

/**
 *
 * Frame Format for DESFire APDUs
 * ==============================
 *
 * The frame format for DESFire APDUs is based on only the ISO 14443-4 specifications for block formats.
 * This is the format used by the example firmware, and seen in Figure 3.
 *  - PCB ñ Protocol Control Byte, this byte is used to transfer format information about each PDU block.
 *  - CID ñ Card Identifier field, this byte is used to identify specific tags. It contains a 4 bit CID value as well
 *          as information on the signal strength between the reader and the tag.
 *  - NAD ñ Node Address field, the example firmware does not support the use of NAD.
 *  - DESFire Command Code ñ This is discussed in the next section.
 *  - Data Bytes ñ This field contains all of the Data Bytes for the command
 *
 *  |-----|-----|-----|---------|------|----------|
 *  | PCB | CID | NAD | Command | Data | Checksum |
 *  |-----|-----|-----|---------|------|----------|
 *
 * Documentation: http://read.pudn.com/downloads64/ebook/225463/M305_DESFireISO14443.pdf
 *                http://www.ti.com.cn/cn/lit/an/sloa213/sloa213.pdf
 */
DESFire::StatusCode DESFire::MIFARE_BlockExchangeWithData(mifare_desfire_tag *tag, byte cmd, byte *sendData, byte *sendLen, byte *backData, byte *backLen)
{
	StatusCode result;

	byte buffer[64];
	byte bufferSize = 64;
	byte sendSize = 3;

	buffer[0] = tag->pcb;
	buffer[1] = tag->cid;
	buffer[2] = cmd;

	// Append data if available
	if (sendData != NULL && sendLen != NULL) {
		if (*sendLen > 0) {
			memcpy(&buffer[3], sendData, *sendLen);
			sendSize = sendSize + *sendLen;
		}
	}

	// Update the PCB
	if (tag->pcb == 0x0A)
		tag->pcb = 0x0B;
	else
		tag->pcb = 0x0A;

	// Calculate CRC_A
	result.mfrc522 = PCD_CalculateCRC(buffer, sendSize, &buffer[sendSize]);
	if (result.mfrc522 != STATUS_OK) {
		return result;
	}

	result.mfrc522 = PCD_TransceiveData(buffer, sendSize + 2, buffer, &bufferSize);
	if (result.mfrc522 != STATUS_OK) {
		return result;
	}

	// Set the DESFire status code
	result.desfire = (DesfireStatusCode)(buffer[2]);

	// Copy data to backData and backLen
	if (backData != NULL && backLen != NULL) {
		memcpy(backData, &buffer[3], bufferSize - 5);
		*backLen = bufferSize - 5;
	}

	return result;
} // End MIFARE_BlockExchangeWithData()

DESFire::StatusCode DESFire::MIFARE_DESFIRE_GetVersion(mifare_desfire_tag *tag, MIFARE_DESFIRE_Version_t *versionInfo)
{
	StatusCode result;
	byte versionBuffer[64];
	byte versionBufferSize = 64;

	result = MIFARE_BlockExchange(tag, 0x60, versionBuffer, &versionBufferSize);
	if (result.mfrc522 == STATUS_OK) {
		byte hardwareVersion[2];
		byte storageSize;

		versionInfo->hardware.vendor_id = versionBuffer[0];
		versionInfo->hardware.type = versionBuffer[1];
		versionInfo->hardware.subtype = versionBuffer[2];
		versionInfo->hardware.version_major = versionBuffer[3];
		versionInfo->hardware.version_minor = versionBuffer[4];
		versionInfo->hardware.storage_size = versionBuffer[5];
		versionInfo->hardware.protocol = versionBuffer[6];

		if (result.desfire == MF_ADDITIONAL_FRAME) {
			result = MIFARE_BlockExchange(tag, 0xAF, versionBuffer, &versionBufferSize);
			if (result.mfrc522 == STATUS_OK) {
				versionInfo->software.vendor_id = versionBuffer[0];
				versionInfo->software.type = versionBuffer[1];
				versionInfo->software.subtype = versionBuffer[2];
				versionInfo->software.version_major = versionBuffer[3];
				versionInfo->software.version_minor = versionBuffer[4];
				versionInfo->software.storage_size = versionBuffer[5];
				versionInfo->software.protocol = versionBuffer[6];
			} else {
				printf("Failed to send AF: ");
				printf("%s \n",GetStatusCodeName(result));
			}

			if (result.desfire == MF_ADDITIONAL_FRAME) {
				byte nad = 0x60;
				result = MIFARE_BlockExchange(tag, 0xAF, versionBuffer, &versionBufferSize);
				if (result.mfrc522 == STATUS_OK) {
					memcpy(versionInfo->uid, &versionBuffer[0], 7);
					memcpy(versionInfo->batch_number, &versionBuffer[7], 5);
					versionInfo->production_week = versionBuffer[12];
					versionInfo->production_year = versionBuffer[13];
				} else {
					printf("Failed to send AF: ");
					printf("%s \n",GetStatusCodeName(result));
				}
			}

			if (result.desfire == MF_ADDITIONAL_FRAME) {
				printf("GetVersion(): More data???\n");
			}
		}
	}
	else {
		printf("Version(): Failure.\n");
	}

	return result;
} // End MIFARE_DESFIRE_GetVersion

DESFire::StatusCode DESFire::MIFARE_DESFIRE_SelectApplication(mifare_desfire_tag *tag, mifare_desfire_aid_t *aid)
{
	StatusCode result;

	byte buffer[64];
	byte bufferSize = MIFARE_AID_SIZE;
	
	for (byte i = 0; i < MIFARE_AID_SIZE; i++) {
		buffer[i] = aid->data[i];
	}
	
	result = MIFARE_BlockExchangeWithData(tag, 0x5A, buffer, &bufferSize, buffer, &bufferSize);
	if (IsStatusCodeOK(result)) {
		// keep track of the application
		memcpy(tag->selected_application, aid->data, MIFARE_AID_SIZE);
	}

	return result;
}

DESFire::StatusCode DESFire::MIFARE_DESFIRE_GetFileIDs(mifare_desfire_tag *tag, byte *files, byte *filesCount)
{
	StatusCode result;
	
	byte bufferSize = MIFARE_MAX_FILE_COUNT + 5;
	byte buffer[bufferSize];

	result = MIFARE_BlockExchange(tag, 0x6F, buffer, &bufferSize);
	if (IsStatusCodeOK(result)) {
		*filesCount = bufferSize;
		memcpy(files, &buffer, *filesCount);
	}

	return result;
} // End MIFARE_DESFIRE_GetFileIDs

DESFire::StatusCode DESFire::MIFARE_DESFIRE_GetFileSettings(mifare_desfire_tag *tag, byte *file, mifare_desfire_file_settings_t *fileSettings)
{
	StatusCode result;

	byte buffer[21];
	byte bufferSize = 21;
	byte sendLen = 1;

	buffer[0] = *file;

	result = MIFARE_BlockExchangeWithData(tag, 0xF5, buffer, &sendLen, buffer, &bufferSize);
	if (IsStatusCodeOK(result)) {
		fileSettings->file_type = buffer[0];
		fileSettings->communication_settings = buffer[1];
		fileSettings->access_rights = ((uint16_t)(buffer[2]) << 8) | (buffer[3]);

		switch (buffer[0]) {
			case MDFT_STANDARD_DATA_FILE:
			case MDFT_BACKUP_DATA_FILE:
				fileSettings->settings.standard_file.file_size = ((uint32_t)(buffer[4])) | ((uint32_t)(buffer[5]) << 8) | ((uint32_t)(buffer[6])  << 16);
				break;

			case MDFT_VALUE_FILE_WITH_BACKUP:
				fileSettings->settings.value_file.lower_limit = ((uint32_t)(buffer[4])) | ((uint32_t)(buffer[5]) << 8) | ((uint32_t)(buffer[6]) << 16) | ((uint32_t)(buffer[7]) << 24);
				fileSettings->settings.value_file.upper_limit = ((uint32_t)(buffer[8])) | ((uint32_t)(buffer[9]) << 8) | ((uint32_t)(buffer[10]) << 16) | ((uint32_t)(buffer[11]) << 24);
				fileSettings->settings.value_file.limited_credit_value = ((uint32_t)(buffer[12])) | ((uint32_t)(buffer[13]) << 8) | ((uint32_t)(buffer[14]) << 16) | ((uint32_t)(buffer[15]) << 24);
				fileSettings->settings.value_file.limited_credit_enabled = buffer[16];
				break;

			case MDFT_LINEAR_RECORD_FILE_WITH_BACKUP:
			case MDFT_CYCLIC_RECORD_FILE_WITH_BACKUP:
				fileSettings->settings.record_file.record_size = ((uint32_t)(buffer[4])) | ((uint32_t)(buffer[5]) << 8) | ((uint32_t)(buffer[6]) << 16);
				fileSettings->settings.record_file.max_number_of_records = ((uint32_t)(buffer[7])) | ((uint32_t)(buffer[8]) << 8) | ((uint32_t)(buffer[9]) << 16);
				fileSettings->settings.record_file.current_number_of_records = ((uint32_t)(buffer[10])) | ((uint32_t)(buffer[11]) << 8) | ((uint32_t)(buffer[12]) << 16);
				break;

			default:
				//return FAIL;
				result.mfrc522 = STATUS_ERROR;
				return result;
		}
	}

	return result;
} // End MIFARE_DESFIRE_GetFileSettings

DESFire::StatusCode DESFire::MIFARE_DESFIRE_GetKeySettings(mifare_desfire_tag *tag, byte *settings, byte *maxKeys)
{
	StatusCode result;

	byte buffer[7];
	byte bufferSize = 7;

	result = MIFARE_BlockExchange(tag, 0x45, buffer, &bufferSize);
	if (IsStatusCodeOK(result)) {
		*settings = buffer[0];
		*maxKeys = buffer[1];
	}

	return result;
} // End MIFARE_DESFIRE_GetKeySettings()

DESFire::StatusCode DESFire::MIFARE_DESFIRE_GetKeyVersion(mifare_desfire_tag *tag, byte key, byte *version)
{
	StatusCode result;

	byte buffer[6];
	byte bufferSize = 6;
	byte sendLen = 1;

	buffer[0] = key;

	result = MIFARE_BlockExchangeWithData(tag, 0x64, buffer, &sendLen, buffer, &bufferSize);
	if (IsStatusCodeOK(result)) {
		*version = buffer[0];
	}

	return result;
}

DESFire::StatusCode DESFire::MIFARE_DESFIRE_ReadData(mifare_desfire_tag *tag, byte fid, uint32_t offset, uint32_t length, byte *backData, size_t *backLen)
{
	StatusCode result;

	byte buffer[64];
	byte bufferSize = 64;
	byte sendLen = 7;
	size_t outSize = 0;

	// file ID
	buffer[0] = fid;
	// offset
	buffer[1] = (offset & 0x00000F);
	buffer[2] = (offset & 0x00FF00) >> 8;
	buffer[3] = (offset & 0xFF0000) >> 16;
	// length
	buffer[4] = (length & 0x0000FF);
	buffer[5] = (length & 0x00FF00) >> 8;
	buffer[6] = (length & 0xFF0000) >> 16;
	
	result = MIFARE_BlockExchangeWithData(tag, 0xBD, buffer, &sendLen, buffer, &bufferSize);
	if (result.mfrc522 == STATUS_OK) {
		do {
			// Copy the data
			memcpy(backData + outSize, buffer, bufferSize);
			outSize += bufferSize;
			*backLen = outSize;

			if (result.desfire == MF_ADDITIONAL_FRAME) {
				result = MIFARE_BlockExchange(tag, 0xAF, buffer, &bufferSize);
			}
		}  while (result.mfrc522 == STATUS_OK && result.desfire == MF_ADDITIONAL_FRAME);
	}

	return result;
}

DESFire::StatusCode DESFire::MIFARE_DESFIRE_GetValue(mifare_desfire_tag *tag, byte fid, int32_t *value)
{
	StatusCode result;

	byte buffer[MFRC522::FIFO_SIZE];
	byte bufferSize = MFRC522::FIFO_SIZE;
	byte sendLen = 1;
	size_t outSize = 0;

	buffer[0] = fid;

	result = MIFARE_BlockExchangeWithData(tag, 0x6C, buffer, &sendLen, buffer, &bufferSize);
	if (IsStatusCodeOK(result)) {
		*value = ((uint32_t)buffer[0] | ((uint32_t)buffer[1] << 8) | ((uint32_t)buffer[2] << 16) | ((uint32_t)buffer[3] << 24));
	}

	return result;
} // End MIFARE_DESFIRE_GetValue()

DESFire::StatusCode DESFire::MIFARE_DESFIRE_GetApplicationIds(mifare_desfire_tag *tag, mifare_desfire_aid_t *aids, byte *applicationCount)
{
	StatusCode result;
	
	// MIFARE_MAX_APPLICATION_COUNT * MIFARE_AID_SIZE + PCB (1 byte) + CID (1 byte) + Checksum (2 bytes)
	// I also add an extra byte in case NAD is needed
	byte bufferSize = (MIFARE_MAX_APPLICATION_COUNT * MIFARE_AID_SIZE) + 5;
	byte buffer[bufferSize]; 
	byte aidBuffer[MIFARE_MAX_APPLICATION_COUNT * MIFARE_AID_SIZE];
	byte aidBufferSize = 0;

	result = MIFARE_BlockExchange(tag, 0x6A, buffer, &bufferSize);
	if (result.mfrc522 != STATUS_OK)
		return result;
		
	// MIFARE_MAX_APPLICATION_COUNT (28) * MIFARE_AID_SIZE + PCB (1) + CID (1) + Checksum (2) = 88
	// Even if the NAD byte is not present we could GET a 0xAF response.
	if (result.desfire == MF_OPERATION_OK && bufferSize == 0x00) {
		// Empty application list
		*applicationCount = 0;
		return result;
	}

	memcpy(aidBuffer, buffer, bufferSize);
	aidBufferSize = bufferSize;

	while (result.desfire == MF_ADDITIONAL_FRAME) {
		bufferSize = (MIFARE_MAX_APPLICATION_COUNT * MIFARE_AID_SIZE) + 5;
		result = MIFARE_BlockExchange(tag, 0xAF, buffer, &bufferSize);
		if (result.mfrc522 != STATUS_OK)
			return result;

		// Make sure we have space (Just in case)
		if ((aidBufferSize + bufferSize) > (MIFARE_MAX_APPLICATION_COUNT * MIFARE_AID_SIZE)) {
			result.mfrc522 = STATUS_NO_ROOM;
			return result;
		}

		// Append the new data
		memcpy(aidBuffer + aidBufferSize, buffer, bufferSize);
	}
	

	// Applications are identified with a 3 byte application identifier(AID)
	// we also received the status byte:
	if ((aidBufferSize % 3) != 0) {
		printf("MIFARE_DESFIRE_GetApplicationIds(): Data is not a modulus of 3.");
		// TODO: Some kind of failure
		result.mfrc522 = STATUS_ERROR;
		return result;
	}

	*applicationCount = aidBufferSize / 3;
		
	for (byte i = 0; i < *applicationCount; i++) {
		aids[i].data[0] = aidBuffer[(i * 3)];
		aids[i].data[1] = aidBuffer[1 + (i * 3)];
		aids[i].data[2] = aidBuffer[2 + (i * 3)];
	}

	return result;
} // End MIFARE_DESFIRE_GetApplicationIds()

/**
 * Returns a __FlashStringHelper pointer to a status code name.
 *
 * @return const __FlashStringHelper *
 */
const char *DESFire::GetStatusCodeName(StatusCode code)
{
	if (code.mfrc522 != MFRC522::STATUS_OK) {
		return MFRC522::GetStatusCodeName(code.mfrc522);
	}

	switch (code.desfire) {
		case MF_OPERATION_OK:			return "Successful operation.";
		case MF_NO_CHANGES:				return "No changes done to backup files.";
		case MF_OUT_OF_EEPROM_ERROR:	return "Insufficient NV-Mem. to complete cmd.";
		case MF_ILLEGAL_COMMAND_CODE:	return "Command code not supported.";
		case MF_INTEGRITY_ERROR:		return "CRC or MAC does not match data.";
		case MF_NO_SUCH_KEY:			return "Invalid key number specified.";
		case MF_LENGTH_ERROR:			return "Length of command string invalid.";
		case MF_PERMISSION_ERROR:		return "Curr conf/status doesnt allow cmd.";
		case MF_PARAMETER_ERROR:		return "Value of the parameter(s) invalid.";
		case MF_APPLICATION_NOT_FOUND:	return "Requested AID not present on PICC.";
		case MF_APPL_INTEGRITY_ERROR:	return "Unrecoverable err within app.";
		case MF_AUTHENTICATION_ERROR:	return "Current authentication status doesn't allow requested command.";
		case MF_ADDITIONAL_FRAME:		return "Additional data frame to be sent.";
		case MF_BOUNDARY_ERROR:			return "Attempt to read/write beyond limits.";
		case MF_PICC_INTEGRITY_ERROR:	return "Unrecoverable error within PICC.";
		case MF_COMMAND_ABORTED:		return "Previous command not fully completed.";
		case MF_PICC_DISABLED_ERROR:	return "PICC disabled by unrecoverable error.";
		case MF_COUNT_ERROR:			return "Cant create more apps, already @ 28.";
		case MF_DUPLICATE_ERROR:		return "Cant create dup. file/app.";
		case MF_EEPROM_ERROR:			return "Couldnt complete NV-write operation.";
		case MF_FILE_NOT_FOUND:			return "Specified file number doesnt exist.";
		case MF_FILE_INTEGRITY_ERROR:	return "Unrecoverable error within file.";
		default:						return "Unknown error";
	}
} // End GetStatusCodeName()

const char *DESFire::GetFileTypeName(mifare_desfire_file_types fileType)
{
	switch (fileType) {
		case MDFT_STANDARD_DATA_FILE:				return "Standard data file.";
		case MDFT_BACKUP_DATA_FILE:					return "Backup data file.";
		case MDFT_VALUE_FILE_WITH_BACKUP:			return "Value file with backup.";
		case MDFT_LINEAR_RECORD_FILE_WITH_BACKUP:	return "Linear record file with backup.";
		case MDFT_CYCLIC_RECORD_FILE_WITH_BACKUP:	return "Cyclic record file with backup.";
		default:									return "Unknown file type.";
	}
} // End GetFileTypeName()

const char *DESFire::GetCommunicationModeName(mifare_desfire_communication_modes communicationMode)
{
	switch (communicationMode) {
		case MDCM_PLAIN:		return "Plain Communication.";
		case MDCM_MACED:        return "Plain Comm secured by DES/3DES MACing.";
		case MDCM_ENCIPHERED:   return "Fully DES/3DES enciphered comm.";
		default:				return "Unknown communication mode.";
	}
} // End GetCommunicationModeName()

bool DESFire::IsStatusCodeOK(StatusCode code)
{
	if (code.mfrc522 != STATUS_OK)
		return false;
	if (code.desfire != MF_OPERATION_OK)
		return false;

	return true;
} // End IsStatusCodeOK();
