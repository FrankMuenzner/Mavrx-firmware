/*!
\file Beacon/params.c
\brief Tuning parameters

\author Yuan Gao

*/

#include "all.h"

/*!
\brief This large array contains all the tuning parameter defaults.

Tuning parameters are stored in EEPROM, this array contains the defaults, 
which are overwritten by the EEPROM values on startup if the EEPROM checksum
is valid.
*/
struct paramStorage_struct paramStorage[] = {
	[ 0] = {"MAV_ID",         0.0f},
};

unsigned char paramSendSingle = 0; /*!< boolean to control whether to send a single param or not */
unsigned int paramCount = sizeof(paramStorage)/(PARAMNAMELEN+4);  /*!< contains the number of parametrs */

/*! \brief used to count the ID of parametr to be sent
Needs to be initialised to paramCount so that parameters aren't sent on startup
*/
unsigned int paramSendCount = sizeof(paramStorage)/(PARAMNAMELEN+4);

/*!
\brief Loads all parametrs from EEPRM
*/
void eeprom_load_all(void) {
	unsigned char chkA, chkB;
	unsigned int i;
	unsigned char * ptr;
	float tempStorage[1];
	
    chkA=EEPROM_VERSION;
    chkB=EEPROM_VERSION;
    
    // Verify EEPRM
    for(i=0; i<paramCount; i++) {
        EEPROMRead(EEPROM_OFFSET + i*4, (unsigned char *)&(tempStorage[0]), 4);
        
        // Calculate the Fletcher-16 checksum
        ptr = (unsigned char *)&(tempStorage[0]);
		chkA += ptr[0];
		chkB += chkA;
		chkA += ptr[1];
		chkB += chkA;
		chkA += ptr[2];
		chkB += chkA;
		chkA += ptr[3];
		chkB += chkA;
    }
	
	// If the checksum is valid, read out values
	if(chkA == EEPROMReadByte(EEPROM_OFFSET + paramCount*4) && chkB == EEPROMReadByte(EEPROM_OFFSET + paramCount*4 + 1)) {
		// For valid data, load into parameters in RAM
		for(i=0; i<paramCount; i++) {
            EEPROMRead(EEPROM_OFFSET + i*4, (unsigned char *)&(paramStorage[i].value), 4);
		}
	}
}

/*!
\brief Saves all the parameters to EEPROM
*/
void eeprom_save_all(void) {
	unsigned char chkA, chkB;
	unsigned int i;
	unsigned char * ptr;
	float tempStorage[1];
	
	chkA=EEPROM_VERSION;
	chkB=EEPROM_VERSION;
    
	for(i=0; i<paramCount; i++) {
        EEPROMRead(EEPROM_OFFSET + i*4, (unsigned char *)&(tempStorage[0]), 4);
        if(tempStorage[0] != paramStorage[i].value) EEPROMWrite(EEPROM_OFFSET + i*4, (unsigned char *)&(paramStorage[i].value), 4);
		ptr = (unsigned char *)&(paramStorage[i].value);
		chkA += ptr[0];
		chkB += chkA;
		chkA += ptr[1];
		chkB += chkA;
		chkA += ptr[2];
		chkB += chkA;
		chkA += ptr[3];
		chkB += chkA;
	}
	
	// for some reason single-byte writing does not work
	ptr = (unsigned char *)&(tempStorage[0]);
	ptr[0] = chkA;
	ptr[1] = chkB;
	
	EEPROMWrite(EEPROM_OFFSET + paramCount * 4, (unsigned char *)&tempStorage, 2);
}
