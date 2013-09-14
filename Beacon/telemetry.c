/*!
\file Beacon/telemetry.c
\brief Deals with telemetry and MAVLink

\author Yuan Gao
*/


#include "all.h"

unsigned char mavlinkID;	/*!< This contains the craft's MAVID, by default it is generated from the chip ID, but is adjusted in parameters */

// Sent messages
mavlink_message_t mavlink_tx_msg;								/*!< Transmitted message  */

mavlink_heartbeat_t mavlink_heartbeat_tx;						/*!< Heartbeat  */
mavlink_command_long_t mavlink_command_long;					/*!< Commands  */
mavlink_manual_control_t mavlink_manual_control;				/*!< Manual control  */
mavlink_global_position_int_t mavlink_global_position_int;      /*!< Local GPS position */
mavlink_set_mode_t mavlink_set_mode;							/*!< Set craft mode  */

// Received messages
mavlink_ping_t mavlink_ping;                                    /*!< Ping */
mavlink_message_t mavlink_rx_msg;								/*!< Receieved message  */
mavlink_status_t mavlink_status;								/*!< Received message status */
mavlink_heartbeat_t mavlink_heartbeat_rx;						/*!< Heartbeat  */
mavlink_sys_status_t mavlink_sys_status;						/*!< System status  */
mavlink_gps_raw_int_t mavlink_gps_raw_int;					    /*!< Craft GPS  */
mavlink_statustext_t mavlink_statustext;				        /*!< Debug text */

unsigned char mavlink_message_buf[MAVLINK_MAX_PACKET_LEN];		/*!< Mavlink message buffer */
unsigned short mavlink_message_len;								/*!< Mavlink message buffer length */

craft_data_t craft={0}; /*!< craft data */

/*!
\brief Initialise mavlink stuff
*/
void MAVLinkInit() {
    mavlinkID = 255; // ground control
    mavlink_heartbeat_tx.type = MAV_TYPE_GCS;
    mavlink_heartbeat_tx.autopilot = MAV_AUTOPILOT_INVALID;
    mavlink_heartbeat_tx.base_mode = MAV_MODE_MANUAL_DISARMED;
    mavlink_heartbeat_tx.system_status = MAV_STATE_STANDBY;
    mavlink_heartbeat_tx.mavlink_version = MAVLINK_VERSION;
}

/*!
\brief Deals with receiving Mavlink

This function is triggered by the Mavlink parser, which is triggered by the
Xbee parser, which is ultimately triggered by the UART RX ISR
*/
void MAVSendHeartbeat(void) {
    mavlink_msg_heartbeat_encode(mavlinkID, MAV_COMP_ID_SYSTEM_CONTROL, &mavlink_tx_msg, &mavlink_heartbeat_tx);
    mavlink_message_len = mavlink_msg_to_send_buffer(mavlink_message_buf, &mavlink_tx_msg);
    XBeeInhibit(); // XBee input needs to be inhibited before transmitting as some incomming messages cause UART responses which could disrupt XBeeWriteCoordinator if it is interrupted.
    if(craft.valid == 1) XBeeWrite(craft.sourceAddress, craft.networkAddress, mavlink_message_buf, mavlink_message_len);
    else XBeeWriteBroadcast(mavlink_message_buf, mavlink_message_len);
    XBeeAllow();
}

/*!
\brief XBee interrupt, this is triggered by the UART ISR, and decodes XBee packets
*/
void XBeeMessage(unsigned char id, unsigned char * buffer, unsigned short length) {
    unsigned char * ptr = 0;
    unsigned int j;
    
    switch(id) {
        case ID_XBEE_MODEMSTATUS:
            ptr = (unsigned char *) &xbee_modem_status;
            xbee_modem_status.isNew = 1;
            break;
        case ID_XBEE_ATRESPONSE:
            ptr = (unsigned char *) &xbee_at_response;
            xbee_at_response.isNew = 1;
            xbee_at_response.varLen = length - 4;
            break;
        case ID_XBEE_RECEIVEPACKET:
            /*ptr = (unsigned char *) &xbee_receive_packet;
            xbee_receive_packet.isNew = 1;
            xbee_receive_packet.varLen = length - 11;*/
            
            // bypass copy, send direct to MAVLINK Parse
            for(j=11; j<length; j++) {
                MAVLinkParse(buffer[j]);
            }
        case IX_XBEE_NODEIDENTIFICATIONINDICATOR:
            ptr = (unsigned char *) &xbee_node_identification_indicator;
            xbee_node_identification_indicator.isNew = 1;
            break;
    }
    
    if(ptr) {
        for(j=0; j<length; j++) {
            ptr[j] = buffer[j];
        }
    }
    
    switch(id) {
        case IX_XBEE_NODEIDENTIFICATIONINDICATOR: // new node connected
            if(craft.valid == 0) {
                if(xbee_node_identification_indicator.sourceEvent == 0x02) { // if a join event
                    XBeeStopJoin(); // disallow joining
                    flashVLED = 3;
                    PRGMode = 0;
                }
                craft.valid = 1;
                craft.networkAddress = xbee_node_identification_indicator.remoteNetworkAddress;
                craft.sourceAddress = xbee_node_identification_indicator.remoteSourceAddress;
            }
        break;
    }
}

/*!
\brief Parses MAVLink data
*/
void MAVLinkParse(unsigned char UARTData) {
    unsigned int i;
    if(mavlink_parse_char(MAVLINK_COMM_0, UARTData, &mavlink_rx_msg, &mavlink_status)) {
        //MAVSendInt("ID", mavlink_rx_msg.msgid);
        if(craft.IDValid == 0) {
            craft.ID = mavlink_rx_msg.sysid;
            craft.IDValid = 1;
        }
        
        if(craft.IDValid == 1 && craft.ID == mavlink_rx_msg.sysid) {
            switch(mavlink_rx_msg.msgid) {
                case MAVLINK_MSG_ID_HEARTBEAT:
                    craft.connected = 1;
                    heartbeatWatchdog = 0;
                    mavlink_msg_heartbeat_decode(&mavlink_rx_msg, &mavlink_heartbeat_rx);
                    craft.baseMode = mavlink_heartbeat_rx.base_mode;
                    craft.systemStatus = mavlink_heartbeat_rx.system_status;
                    break;
                case MAVLINK_MSG_ID_SYS_STATUS:
                    craft.battVoltage = mavlink_sys_status.voltage_battery;
                    break;
                case MAVLINK_MSG_ID_PING:
                    // PING is a special case, as only pings with target system set to zero is responded to,
                    // and results are breadcast on mesh instead of to coordinator.
                    if(mavlink_msg_ping_get_target_system(&mavlink_rx_msg) == 0) { // only respond to pings with target system set to zero
                        mavlink_msg_ping_decode(&mavlink_rx_msg, &mavlink_ping);
                        mavlink_ping.time_usec = sysUS;
                        mavlink_ping.target_component = mavlinkID;
                        mavlink_ping.target_component = MAV_COMP_ID_SYSTEM_CONTROL;
                        XBeeWriteBroadcast(mavlink_message_buf, mavlink_message_len); // BROADCAST
                    }
                    break;
                case MAVLINK_MSG_ID_GPS_RAW_INT:
                    craft.lat = mavlink_gps_raw_int.lat;
                    craft.lon = mavlink_gps_raw_int.lon;
                    craft.alt = mavlink_gps_raw_int.alt;
                    craft.fix = mavlink_gps_raw_int.fix_type;
                    break;
                 
                case MAVLINK_MSG_ID_STATUSTEXT:
                    craft.statusTextSeverity = mavlink_statustext.severity;
                    for(i=0; i<MAVLINK_MSG_STATUSTEXT_FIELD_TEXT_LEN; i++) {
                        craft.statusText[i] = mavlink_statustext.text[i];
                        if(mavlink_statustext.text[i] == '\0') break;
                    }
                    craft.statusTextNew = 1;
                default:
                    //MAVSendInt("CMDIGNORE", mavlink_rx_msg.msgid);
                    break;
            }
        }
    }
}